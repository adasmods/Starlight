#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Replay bot: records your inputs per physics tick and plays them back exactly.
// Inputs are keyed by GJGameState::m_currentProgress (the tick counter), so
// playback is frame-perfect. Practice mode checkpoints are handled by cutting
// the recording back to the checkpoint's tick.
namespace starlight::bot {

enum class Mode { Disabled = 0, Record = 1, Playback = 2 };

struct Input {
    uint32_t frame;
    int button; // 1 jump, 2 left, 3 right
    bool down;
    bool player2;
};

Mode mode();
void setMode(Mode mode);

std::vector<Input> const& inputs();
void clear();

// true while the bot itself is pressing buttons (so the hooks don't record them)
bool injecting();

// called from GJBaseGameLayer::handleButton for real inputs while recording
void recordInput(GJBaseGameLayer* layer, int button, bool down, bool player1);
// called every physics tick (GJBaseGameLayer::processCommands) while playing back
void playbackTick(GJBaseGameLayer* layer);
// called after PlayLayer::resetLevel (also covers practice checkpoint respawns)
void onReset(GJBaseGameLayer* layer);

// replay files in <save dir>/replays/<name>.slr
bool save(std::string const& name, std::string& message);
bool load(std::string const& name, std::string& message);
std::vector<std::string> listReplays();

} // namespace starlight::bot
