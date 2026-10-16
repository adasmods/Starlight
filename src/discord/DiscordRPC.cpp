#include "DiscordRPC.hpp"

#include "Config.hpp"

#include <Geode/Geode.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>

using namespace geode::prelude;

namespace starlight::discord {

namespace {

struct Snapshot {
    bool enabled = false;
    std::string clientId;
    std::string details;
    std::string state;
    long long start = 0;

    bool sameActivity(Snapshot const& o) const {
        return details == o.details && state == o.state && start == o.start;
    }
};

std::mutex s_mutex;
Snapshot s_pending;
std::string s_status = "Off";
bool s_threadStarted = false;
float s_tickTimer = 0.f;

// scene tracking (main thread only)
int s_lastScene = -1;
long long s_sceneStart = 0;

void setStatus(std::string text) {
    std::lock_guard lock(s_mutex);
    s_status = std::move(text);
}

std::string escape(std::string const& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default:
                if (static_cast<unsigned char>(ch) >= 0x20) out += ch;
        }
    }
    return out;
}

// ---- IPC (worker thread only)
HANDLE s_pipe = INVALID_HANDLE_VALUE;
std::string s_connectedId;
int s_nonce = 0;

bool writeFrame(uint32_t op, std::string const& json) {
    if (s_pipe == INVALID_HANDLE_VALUE) return false;
    std::string buf(8 + json.size(), '\0');
    uint32_t len = static_cast<uint32_t>(json.size());
    std::memcpy(buf.data(), &op, 4);
    std::memcpy(buf.data() + 4, &len, 4);
    std::memcpy(buf.data() + 8, json.data(), json.size());
    DWORD written = 0;
    return WriteFile(s_pipe, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) && written == buf.size();
}

void drain() {
    if (s_pipe == INVALID_HANDLE_VALUE) return;
    DWORD avail = 0;
    while (PeekNamedPipe(s_pipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        char tmp[4096];
        DWORD read = 0;
        if (!ReadFile(s_pipe, tmp, std::min<DWORD>(avail, sizeof(tmp)), &read, nullptr) || read == 0) break;
    }
}

void disconnect() {
    if (s_pipe != INVALID_HANDLE_VALUE) CloseHandle(s_pipe);
    s_pipe = INVALID_HANDLE_VALUE;
    s_connectedId.clear();
}

bool connect(std::string const& clientId) {
    for (int i = 0; i < 10; i++) {
        auto name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        s_pipe = h;
        if (!writeFrame(0, fmt::format(R"({{"v":1,"client_id":"{}"}})", escape(clientId)))) {
            disconnect();
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        drain();
        s_connectedId = clientId;
        return true;
    }
    return false;
}

bool sendActivity(Snapshot const& snap, bool clear) {
    std::string activity = "null";
    if (!clear) {
        activity = fmt::format(R"({{"details":"{}","state":"{}","timestamps":{{"start":{}}}}})", escape(snap.details),
                               escape(snap.state), snap.start);
    }
    auto json = fmt::format(R"({{"cmd":"SET_ACTIVITY","args":{{"pid":{},"activity":{}}},"nonce":"{}"}})",
                            GetCurrentProcessId(), activity, ++s_nonce);
    bool ok = writeFrame(1, json);
    drain();
    return ok;
}

void workerLoop() {
    Snapshot sent;
    bool hasSent = false;
    auto lastSend = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    auto lastConnectTry = lastSend;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        Snapshot snap;
        {
            std::lock_guard lock(s_mutex);
            snap = s_pending;
        }
        auto now = std::chrono::steady_clock::now();

        if (!snap.enabled || snap.clientId.empty()) {
            if (s_pipe != INVALID_HANDLE_VALUE) {
                sendActivity(snap, true);
                disconnect();
            }
            hasSent = false;
            setStatus(snap.enabled ? "Enter your Discord application ID" : "Off");
            continue;
        }

        if (s_pipe != INVALID_HANDLE_VALUE && s_connectedId != snap.clientId) disconnect();
        if (s_pipe == INVALID_HANDLE_VALUE) {
            if (now - lastConnectTry < std::chrono::seconds(10)) continue;
            lastConnectTry = now;
            if (!connect(snap.clientId)) {
                setStatus("Discord is not running");
                continue;
            }
            hasSent = false;
        }

        drain();
        // Discord allows ~5 updates per 20 s
        if ((!hasSent || !snap.sameActivity(sent)) && now - lastSend > std::chrono::seconds(5)) {
            if (sendActivity(snap, false)) {
                sent = snap;
                hasSent = true;
                lastSend = now;
                setStatus("Connected");
            } else {
                disconnect();
                setStatus("Connection lost, retrying...");
            }
        }
    }
}

long long unixNow() { return static_cast<long long>(std::time(nullptr)); }

} // namespace

void tick(float dt) {
    auto& c = cfg();
    if (!c.discordRpc && !s_threadStarted) return;

    s_tickTimer -= dt;
    if (s_tickTimer > 0.f) return;
    s_tickTimer = 1.f;

    Snapshot snap;
    snap.enabled = c.discordRpc;
    snap.clientId = c.discordClientId;

    int scene = 0;
    if (auto pl = PlayLayer::get()) {
        scene = 1;
        std::string name = pl->m_level ? std::string(pl->m_level->m_levelName) : "a level";
        snap.details = c.discordShowLevel ? fmt::format("Playing {}", name) : "Playing a level";
        snap.state = pl->m_isPracticeMode ? "Practice mode" : fmt::format("Attempt {}", pl->m_attempts);
    } else if (LevelEditorLayer::get()) {
        scene = 2;
        snap.details = "Building a level";
        snap.state = "In the editor";
    } else {
        snap.details = "In menus";
        snap.state = "Starlight mod menu";
    }
    if (scene != s_lastScene) {
        s_lastScene = scene;
        s_sceneStart = unixNow();
    }
    snap.start = s_sceneStart;

    {
        std::lock_guard lock(s_mutex);
        s_pending = std::move(snap);
    }
    if (!s_threadStarted) {
        s_threadStarted = true;
        std::thread(workerLoop).detach();
    }
}

std::string status() {
    std::lock_guard lock(s_mutex);
    return s_status;
}

} // namespace starlight::discord
