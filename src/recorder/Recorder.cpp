#include "Recorder.hpp"

#include "Config.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <Geode/modify/CCEGLView.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace geode::prelude;

namespace starlight::recorder {

namespace {

using Clock = std::chrono::steady_clock;

// ---- state shared with the worker threads
std::atomic<bool> s_recording{false};
std::atomic<bool> s_finishing{false};
std::mutex s_statusMutex;
std::string s_status = "Idle";

int s_width = 0, s_height = 0, s_fps = 60;
Clock::time_point s_start;
long long s_written = 0;

HANDLE s_process = nullptr;
HANDLE s_pipe = nullptr;
std::thread s_writer;

struct QueuedFrame {
    std::shared_ptr<std::vector<uint8_t>> pixels;
    int repeat = 1; // duplicated frames keep audio and video in sync
};
std::mutex s_queueMutex;
std::condition_variable s_queueCv;
std::deque<QueuedFrame> s_queue;
bool s_writerStop = false;
constexpr size_t kMaxQueued = 16;

std::filesystem::path s_tmpVideo, s_tmpAudio, s_output;

// ---- audio capture
FMOD::DSP* s_dsp = nullptr;
std::atomic<bool> s_audioOn{false};
std::mutex s_audioMutex;
std::vector<float> s_audio;
int s_audioChannels = 2;
int s_audioRate = 44100;

void setStatus(std::string text) {
    std::lock_guard lock(s_statusMutex);
    s_status = std::move(text);
}

std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::wstring quote(std::filesystem::path const& p) { return L"\"" + p.wstring() + L"\""; }

bool spawn(std::wstring cmd, bool withStdin, HANDLE& process, HANDLE& stdinWrite) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (withStdin) {
        if (!CreatePipe(&readEnd, &writeEnd, &sa, 1 << 22)) return false;
        SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readEnd;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (readEnd) CloseHandle(readEnd);
    if (!ok) {
        if (writeEnd) CloseHandle(writeEnd);
        return false;
    }
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    stdinWrite = writeEnd;
    return true;
}

bool runAndWait(std::wstring cmd) {
    HANDLE process = nullptr, unused = nullptr;
    if (!spawn(std::move(cmd), false, process, unused)) return false;
    WaitForSingleObject(process, 10 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return code == 0;
}

void writerLoop() {
    while (true) {
        QueuedFrame frame;
        {
            std::unique_lock lock(s_queueMutex);
            s_queueCv.wait(lock, [] { return !s_queue.empty() || s_writerStop; });
            if (s_queue.empty()) break;
            frame = std::move(s_queue.front());
            s_queue.pop_front();
        }
        for (int r = 0; r < frame.repeat; r++) {
            auto data = frame.pixels->data();
            size_t left = frame.pixels->size();
            while (left > 0) {
                DWORD written = 0;
                if (!WriteFile(s_pipe, data, static_cast<DWORD>(std::min<size_t>(left, 1 << 20)), &written, nullptr) || written == 0) {
                    left = 0;
                    break;
                }
                data += written;
                left -= written;
            }
        }
    }
}

FMOD_RESULT F_CALLBACK dspRead(
    FMOD_DSP_STATE*, float* in, float* out, unsigned int length, int inChannels, int* outChannels
) {
    int outCh = outChannels ? *outChannels : inChannels;
    for (unsigned int i = 0; i < length; i++) {
        for (int ch = 0; ch < outCh; ch++) out[i * outCh + ch] = ch < inChannels ? in[i * inChannels + ch] : 0.f;
    }
    if (s_audioOn) {
        std::lock_guard lock(s_audioMutex);
        s_audioChannels = inChannels;
        s_audio.insert(s_audio.end(), in, in + static_cast<size_t>(length) * inChannels);
    }
    return FMOD_OK;
}

void ensureDsp() {
    if (s_dsp) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    FMOD_DSP_DESCRIPTION desc{};
    desc.pluginsdkversion = FMOD_PLUGIN_SDK_VERSION;
    std::strncpy(desc.name, "Starlight Recorder", sizeof(desc.name) - 1);
    desc.version = 0x00010000;
    desc.numinputbuffers = 1;
    desc.numoutputbuffers = 1;
    desc.read = dspRead;
    if (engine->m_system->createDSP(&desc, &s_dsp) != FMOD_OK) {
        s_dsp = nullptr;
        return;
    }
    FMOD::ChannelGroup* master = nullptr;
    engine->m_system->getMasterChannelGroup(&master);
    if (master) master->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, s_dsp);
    int rate = 44100;
    engine->m_system->getSoftwareFormat(&rate, nullptr, nullptr);
    s_audioRate = rate;
}

bool writeWav(std::filesystem::path const& path, std::vector<float> const& samples, int channels, int rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<char const*>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<char const*>(&v), 2); };
    uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(float));
    file.write("RIFF", 4);
    u32(36 + dataSize);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    u32(16);
    u16(3); // IEEE float
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate * channels * 4));
    u16(static_cast<uint16_t>(channels * 4));
    u16(32);
    file.write("data", 4);
    u32(dataSize);
    file.write(reinterpret_cast<char const*>(samples.data()), dataSize);
    return static_cast<bool>(file);
}

std::string fileSafe(std::string const& s) {
    std::string out;
    for (char ch : s) out += (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
    return out.empty() ? "level" : out;
}

void notify(std::string text, bool ok) {
    defer([text = std::move(text), ok] {
        Notification::create(text.c_str(), ok ? NotificationIcon::Success : NotificationIcon::Error)->show();
    });
}

} // namespace

std::filesystem::path videoDir() {
    auto dir = Mod::get()->getSaveDir() / "videos";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string findFfmpeg() {
    std::error_code ec;
    auto& custom = cfg().ffmpegPath;
    if (!custom.empty() && std::filesystem::exists(widen(custom), ec)) return custom;

    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) return std::filesystem::path(buf).string();

    std::vector<std::filesystem::path> candidates;
    if (auto local = _wgetenv(L"LOCALAPPDATA")) {
        std::filesystem::path winget = std::filesystem::path(local) / "Microsoft" / "WinGet";
        candidates.push_back(winget / "Links" / "ffmpeg.exe");
        // winget install Gyan.FFmpeg -> Packages\Gyan.FFmpeg_*\ffmpeg-*\bin\ffmpeg.exe
        for (auto const& pkg : std::filesystem::directory_iterator(winget / "Packages", ec)) {
            if (pkg.path().filename().string().rfind("Gyan.FFmpeg", 0) != 0) continue;
            for (auto const& sub : std::filesystem::directory_iterator(pkg.path(), ec)) {
                candidates.push_back(sub.path() / "bin" / "ffmpeg.exe");
            }
        }
    }
    candidates.push_back(Mod::get()->getSaveDir() / "ffmpeg.exe");
    candidates.push_back("C:\\ffmpeg\\bin\\ffmpeg.exe");
    for (auto const& p : candidates) {
        if (std::filesystem::exists(p, ec)) return p.string();
    }
    return {};
}

bool isRecording() { return s_recording; }
bool isFinishing() { return s_finishing; }

std::string status() {
    if (s_recording) {
        auto secs = static_cast<int>(std::chrono::duration<double>(Clock::now() - s_start).count());
        return fmt::format("Recording {}:{:02}  ({}x{} @ {} FPS)", secs / 60, secs % 60, s_width, s_height, s_fps);
    }
    std::lock_guard lock(s_statusMutex);
    return s_status;
}

bool start(std::string& message) {
    if (s_recording || s_finishing) {
        message = "The recorder is busy.";
        return false;
    }
    auto ffmpeg = findFfmpeg();
    if (ffmpeg.empty()) {
        message = "ffmpeg not found. Install it (winget install Gyan.FFmpeg) or set its path.";
        return false;
    }

    auto frame = CCDirector::get()->getOpenGLView()->getFrameSize();
    s_width = static_cast<int>(frame.width) & ~1;
    s_height = static_cast<int>(frame.height) & ~1;
    if (s_width <= 0 || s_height <= 0) {
        message = "Could not read the window size.";
        return false;
    }
    s_fps = std::clamp(cfg().recordFps, 15, 240);

    std::string level = "Starlight";
    if (auto pl = PlayLayer::get(); pl && pl->m_level) level = std::string(pl->m_level->m_levelName);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    auto base = fmt::format("{} {:04}-{:02}-{:02} {:02}.{:02}.{:02}", fileSafe(level), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                            tm.tm_hour, tm.tm_min, tm.tm_sec);
    auto dir = videoDir();
    s_output = dir / (base + ".mp4");
    s_tmpVideo = dir / (base + ".video.mp4");
    s_tmpAudio = dir / (base + ".audio.wav");

    std::wstring cmd = quote(std::filesystem::path(widen(ffmpeg))) + L" -y -loglevel error -f rawvideo -pix_fmt rgba -s " +
                       std::to_wstring(s_width) + L"x" + std::to_wstring(s_height) + L" -r " + std::to_wstring(s_fps) +
                       L" -i - -vf vflip -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p " + quote(s_tmpVideo);
    if (!spawn(cmd, true, s_process, s_pipe)) {
        message = "Could not start ffmpeg.";
        return false;
    }

    {
        std::lock_guard lock(s_queueMutex);
        s_queue.clear();
        s_writerStop = false;
    }
    s_writer = std::thread(writerLoop);

    if (cfg().recordAudio) {
        ensureDsp();
        std::lock_guard lock(s_audioMutex);
        s_audio.clear();
        s_audioOn = s_dsp != nullptr;
    }

    s_written = 0;
    s_start = Clock::now();
    s_recording = true;
    message = "Recording started.";
    return true;
}

void stop() {
    if (!s_recording) return;
    s_recording = false;
    s_audioOn = false;
    s_finishing = true;
    setStatus("Finishing video...");

    std::vector<float> audio;
    {
        std::lock_guard lock(s_audioMutex);
        audio.swap(s_audio);
    }
    int channels = s_audioChannels, rate = s_audioRate;
    auto ffmpeg = findFfmpeg();

    std::thread([audio = std::move(audio), channels, rate, ffmpeg]() mutable {
        {
            std::lock_guard lock(s_queueMutex);
            s_writerStop = true;
        }
        s_queueCv.notify_all();
        if (s_writer.joinable()) s_writer.join();
        if (s_pipe) CloseHandle(s_pipe);
        s_pipe = nullptr;
        if (s_process) {
            WaitForSingleObject(s_process, 10 * 60 * 1000);
            CloseHandle(s_process);
            s_process = nullptr;
        }

        std::error_code ec;
        bool ok = std::filesystem::exists(s_tmpVideo, ec);
        if (ok && !audio.empty() && writeWav(s_tmpAudio, audio, channels, rate)) {
            setStatus("Adding audio...");
            std::wstring mux = quote(std::filesystem::path(widen(ffmpeg))) + L" -y -loglevel error -i " + quote(s_tmpVideo) + L" -i " +
                               quote(s_tmpAudio) + L" -c:v copy -c:a aac -b:a 192k -shortest " + quote(s_output);
            if (runAndWait(mux)) {
                std::filesystem::remove(s_tmpVideo, ec);
            } else {
                std::filesystem::rename(s_tmpVideo, s_output, ec);
            }
            std::filesystem::remove(s_tmpAudio, ec);
        } else if (ok) {
            std::filesystem::rename(s_tmpVideo, s_output, ec);
        }

        auto msg = ok ? fmt::format("Saved {}", s_output.filename().string()) : std::string("Recording failed (ffmpeg error).");
        setStatus(msg);
        s_finishing = false;
        notify(msg, ok);
    }).detach();
}

void toggle() {
    if (s_recording) {
        stop();
        return;
    }
    std::string message;
    bool ok = start(message);
    notify(message, ok);
}

void captureFrame() {
    if (!s_recording) return;
    double elapsed = std::chrono::duration<double>(Clock::now() - s_start).count();
    auto expected = static_cast<long long>(elapsed * s_fps) + 1;
    if (s_written >= expected) return;
    int repeat = static_cast<int>(std::min<long long>(expected - s_written, s_fps)); // at most 1s of duplicates
    s_written = expected;

    std::unique_lock lock(s_queueMutex);
    if (s_queue.size() >= kMaxQueued) {
        // encoder is behind: repeat the last frame instead of reading a new one
        s_queue.back().repeat += repeat;
        return;
    }
    lock.unlock();

    auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(s_width) * s_height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, s_width, s_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());

    lock.lock();
    s_queue.push_back({std::move(pixels), repeat});
    lock.unlock();
    s_queueCv.notify_one();
}

} // namespace starlight::recorder

class $modify(StarlightRecorderView, CCEGLView) {
    void swapBuffers() {
        starlight::recorder::captureFrame();
        CCEGLView::swapBuffers();
    }
};
