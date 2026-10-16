#pragma once

#include <string>

// Discord Rich Presence over Discord's local IPC pipe (no extra libraries).
// Needs your own Discord application ID (discord.com/developers/applications).
namespace starlight::discord {

// call every frame from the main thread; snapshots the game state for the worker
void tick(float dt);

std::string status();

} // namespace starlight::discord
