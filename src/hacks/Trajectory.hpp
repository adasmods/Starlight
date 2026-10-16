#pragma once

#include <Geode/Geode.hpp>

// Show Trajectory: two hidden copies of player 1 (holding / not holding jump)
// are simulated forward every frame and their paths are drawn.
// While simulating, game side effects (triggers, orbs, pads, portals, pickups,
// deaths) are blocked so the real level is never touched.
namespace starlight::trajectory {

bool simulating();
void onSimulatedDeath();

// call from PlayLayer::init (drops the ghosts of the previous level)
void reset();
// call every frame from PlayLayer::postUpdate
void update(PlayLayer* pl);

} // namespace starlight::trajectory
