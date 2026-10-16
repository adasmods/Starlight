#include "Bot.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace geode::prelude;

namespace starlight::bot {

namespace {

Mode s_mode = Mode::Disabled;
std::vector<Input> s_inputs;
size_t s_index = 0;
bool s_injecting = false;

std::filesystem::path replayDir() {
    auto dir = Mod::get()->getSaveDir() / "replays";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string sanitize(std::string const& name) {
    std::string out;
    for (char ch : name) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_') out += ch;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

uint32_t currentFrame(GJBaseGameLayer* layer) { return layer->m_gameState.m_currentProgress; }

} // namespace

Mode mode() { return s_mode; }

void setMode(Mode mode) {
    s_mode = mode;
    s_index = 0;
}

std::vector<Input> const& inputs() { return s_inputs; }

void clear() {
    s_inputs.clear();
    s_index = 0;
}

bool injecting() { return s_injecting; }

void recordInput(GJBaseGameLayer* layer, int button, bool down, bool player1) {
    if (s_mode != Mode::Record || !layer) return;
    s_inputs.push_back({currentFrame(layer), button, down, !player1});
}

void playbackTick(GJBaseGameLayer* layer) {
    if (s_mode != Mode::Playback || !layer) return;
    uint32_t frame = currentFrame(layer);
    s_injecting = true;
    while (s_index < s_inputs.size() && s_inputs[s_index].frame <= frame) {
        auto const& in = s_inputs[s_index];
        layer->handleButton(in.down, in.button, !in.player2);
        s_index++;
    }
    s_injecting = false;
}

void onReset(GJBaseGameLayer* layer) {
    if (!layer) return;
    uint32_t frame = currentFrame(layer);

    if (s_mode == Mode::Record) {
        // cut everything recorded after the point we respawned at
        std::erase_if(s_inputs, [frame](Input const& in) { return in.frame >= frame; });

        // the game releases every button on respawn, so close any press left open
        std::map<std::pair<int, bool>, bool> held;
        for (auto const& in : s_inputs) held[{in.button, in.player2}] = in.down;
        for (auto const& [key, isDown] : held) {
            if (isDown) s_inputs.push_back({frame, key.first, false, key.second});
        }
    }

    if (s_mode == Mode::Playback) {
        s_index = static_cast<size_t>(
            std::lower_bound(s_inputs.begin(), s_inputs.end(), frame, [](Input const& in, uint32_t f) { return in.frame < f; }) -
            s_inputs.begin()
        );
    }
}

bool save(std::string const& rawName, std::string& message) {
    auto name = sanitize(rawName);
    if (name.empty()) {
        message = "Enter a replay name first.";
        return false;
    }
    if (s_inputs.empty()) {
        message = "Nothing recorded yet.";
        return false;
    }
    std::ofstream file(replayDir() / (name + ".slr"), std::ios::trunc);
    if (!file) {
        message = "Could not write the replay file.";
        return false;
    }
    file << "starlight-replay 1\n";
    for (auto const& in : s_inputs) file << in.frame << ' ' << in.button << ' ' << in.down << ' ' << in.player2 << '\n';
    message = fmt::format("Saved {} inputs to {}.slr", s_inputs.size(), name);
    return true;
}

bool load(std::string const& rawName, std::string& message) {
    auto name = sanitize(rawName);
    std::ifstream file(replayDir() / (name + ".slr"));
    if (!file) {
        message = "Replay not found.";
        return false;
    }
    std::string header;
    std::getline(file, header);
    if (header.rfind("starlight-replay", 0) != 0) {
        message = "Not a Starlight replay.";
        return false;
    }
    std::vector<Input> loaded;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        Input in{};
        int down = 0, p2 = 0;
        if (ss >> in.frame >> in.button >> down >> p2) {
            in.down = down != 0;
            in.player2 = p2 != 0;
            loaded.push_back(in);
        }
    }
    std::stable_sort(loaded.begin(), loaded.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    s_inputs = std::move(loaded);
    s_index = 0;
    message = fmt::format("Loaded {} inputs from {}.slr", s_inputs.size(), name);
    return true;
}

std::vector<std::string> listReplays() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(replayDir(), ec)) {
        if (entry.path().extension() == ".slr") out.push_back(entry.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace starlight::bot
