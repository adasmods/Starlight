#include "Trajectory.hpp"

#include "Config.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace starlight::trajectory {

namespace {

bool s_simulating = false;
bool s_died = false;
Ref<PlayerObject> s_ghosts[2];
Ref<CCDrawNode> s_draw;

// physics step used by GD 2.2 at 240 TPS (dt is in 60 fps units)
constexpr float kStep = 0.25f;

} // namespace

bool simulating() { return s_simulating; }
void onSimulatedDeath() { s_died = true; }

void reset() {
    s_ghosts[0] = nullptr;
    s_ghosts[1] = nullptr;
    s_draw = nullptr;
    s_simulating = false;
}

void update(PlayLayer* pl) {
    auto& c = cfg();
    bool active = c.showTrajectory && pl && pl->m_player1 && pl->m_objectLayer && !pl->m_player1->m_isDead &&
                  !pl->m_hasCompletedLevel;
    if (!active) {
        if (s_draw) s_draw->clear();
        return;
    }

    if (!s_draw) {
        s_draw = CCDrawNode::create();
        s_draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        pl->m_objectLayer->addChild(s_draw, 2000);
    }
    for (auto& ghost : s_ghosts) {
        if (ghost) continue;
        ghost = PlayerObject::create(1, 1, pl, pl->m_objectLayer, false);
        ghost->setVisible(false);
        ghost->m_isHidden = true;
        pl->m_objectLayer->addChild(ghost, -100);
    }
    s_draw->clear();

    auto real = pl->m_player1;
    int steps = std::clamp(c.trajectoryLength, 30, 960);
    ccColor4F colors[2] = {
        {0.80f, 0.55f, 1.f, 1.f}, // holding jump: violet
        {0.45f, 0.75f, 1.f, 1.f}, // released: blue
    };

    for (int i = 0; i < 2; i++) {
        auto ghost = s_ghosts[i].data();
        ghost->copyAttributes(real);
        ghost->m_position = real->m_position;
        ghost->setPosition(real->getPosition());
        if (i == 0) ghost->pushButton(PlayerButton::Jump);
        else ghost->releaseButton(PlayerButton::Jump);

        s_simulating = true;
        s_died = false;
        CCPoint prev = ghost->getPosition();
        for (int k = 0; k < steps; k++) {
            ghost->update(kStep);
            pl->checkCollisions(ghost, kStep, false);
            CCPoint cur = ghost->getPosition();
            float fade = 1.f - static_cast<float>(k) / steps;
            ccColor4F col = colors[i];
            col.a = 0.25f + 0.65f * fade;
            s_draw->drawSegment(prev, cur, 0.8f, col);
            prev = cur;
            if (s_died) {
                s_draw->drawDot(cur, 3.5f, {1.f, 0.3f, 0.45f, 0.95f});
                break;
            }
        }
        s_simulating = false;
    }
}

} // namespace starlight::trajectory

// ---------------------------------------------------------------- side effect blockers

using starlight::trajectory::simulating;

class $modify(StarlightTrajectoryGuard, GJBaseGameLayer) {
    void playerTouchedRing(PlayerObject* player, RingObject* object) {
        if (simulating()) return;
        GJBaseGameLayer::playerTouchedRing(player, object);
    }
    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (simulating()) return;
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }
    void bumpPlayer(PlayerObject* player, EffectGameObject* object) {
        if (simulating()) return;
        GJBaseGameLayer::bumpPlayer(player, object);
    }
    void playerWillSwitchMode(PlayerObject* player, GameObject* object) {
        if (simulating()) return;
        GJBaseGameLayer::playerWillSwitchMode(player, object);
    }
    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        if (simulating()) return;
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
    }
    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        if (simulating()) return;
        GJBaseGameLayer::teleportPlayer(object, player);
    }
    void destroyObject(GameObject* object) {
        if (simulating()) return;
        GJBaseGameLayer::destroyObject(object);
    }
    void pickupItem(EffectGameObject* object) {
        if (simulating()) return;
        GJBaseGameLayer::pickupItem(object);
    }
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (simulating()) return;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }
};
