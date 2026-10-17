// GDH-style extras: effect toggles, camera, popups, start position switcher.

#include "Extras.hpp"

#include "Config.hpp"
#include "State.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/AchievementBar.hpp>
#include <Geode/modify/CCCircleWave.hpp>
#include <Geode/modify/GameToolbox.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <vector>

using namespace geode::prelude;
using namespace starlight;

// ---------------------------------------------------------------- effects

// every circle effect (portals, orbs, pads, coins, death circles)
class $modify(StarlightCircleWave, CCCircleWave) {
    void draw() {
        if (cfg().noCircleEffects) return;
        CCCircleWave::draw();
    }
};

class $modify(StarlightAchievementBar, AchievementBar) {
    void show() {
        if (cfg().hideAchievements) return;
        AchievementBar::show();
    }
};

class $modify(StarlightToolbox, GameToolbox) {
    static gd::string intToShortString(int value) {
        if (cfg().noShortNumbers) return GameToolbox::intToString(value);
        return GameToolbox::intToShortString(value);
    }
};

class $modify(StarlightExtrasLayer, GJBaseGameLayer) {
    void playExitDualEffect(PlayerObject* player) {
        if (cfg().noExitDualEffect) return;
        GJBaseGameLayer::playExitDualEffect(player);
    }

    void updateZoom(float zoom, float duration, int easing, float rate, int uniqueID, int controlID) {
        if (cfg().noCameraZoom) return;
        GJBaseGameLayer::updateZoom(zoom, duration, easing, rate, uniqueID, controlID);
    }

    void updateCameraOffsetX(float offsetX, float duration, int easingType, float easingRate, int uniqueID, int controlID) {
        if (cfg().noCameraMove) return;
        GJBaseGameLayer::updateCameraOffsetX(offsetX, duration, easingType, easingRate, uniqueID, controlID);
    }

    void updateCameraOffsetY(float offsetY, float duration, int easingType, float easingRate, int uniqueID, int controlID) {
        if (cfg().noCameraMove) return;
        GJBaseGameLayer::updateCameraOffsetY(offsetY, duration, easingType, easingRate, uniqueID, controlID);
    }
};

// ---------------------------------------------------------------- start position switcher

namespace {

std::vector<StartPosObject*> s_startPositions;
int s_startIndex = -1; // -1 = level start
bool s_sorted = false;

} // namespace

class $modify(StarlightStartPosLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        s_startPositions.clear();
        s_startIndex = -1;
        s_sorted = false;
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void addObject(GameObject* object) {
        PlayLayer::addObject(object);
        if (object && object->m_objectID == 31) {
            if (auto sp = typeinfo_cast<StartPosObject*>(object)) {
                s_startPositions.push_back(sp);
                s_sorted = false;
            }
        }
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (cfg().noNewBest) return;
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }

    void updateAttempts() {
        if (cfg().freezeAttempts) return;
        PlayLayer::updateAttempts();
        if (cfg().showTotalAttempts && m_attemptLabel && m_level) {
            m_attemptLabel->setString(fmt::format("Attempt {}", m_level->m_attempts.value()).c_str());
        }
    }
};

namespace starlight::extras {

int startPosCount() { return static_cast<int>(s_startPositions.size()); }
int startPosIndex() { return s_startIndex; }

bool cycleStartPos(int direction) {
    auto pl = PlayLayer::get();
    if (!pl || s_startPositions.empty()) return false;
    if (!s_sorted) {
        std::sort(s_startPositions.begin(), s_startPositions.end(),
                  [](StartPosObject* a, StartPosObject* b) { return a->getPositionX() < b->getPositionX(); });
        s_sorted = true;
        // start from the start position the level is currently using
        auto current = std::find(s_startPositions.begin(), s_startPositions.end(), pl->m_startPosObject);
        s_startIndex = current == s_startPositions.end() ? -1 : static_cast<int>(current - s_startPositions.begin());
    }

    int count = static_cast<int>(s_startPositions.size());
    s_startIndex += direction;
    if (s_startIndex < -1) s_startIndex = count - 1;
    if (s_startIndex >= count) s_startIndex = -1;

    StartPosObject* target = s_startIndex < 0 ? nullptr : s_startPositions.at(s_startIndex);
    Ref<PlayLayer> self = pl;
    defer([self, target] {
        self->m_currentCheckpoint = nullptr;
        self->setStartPosObject(target);
        if (self->m_isPracticeMode) self->resetLevelFromStart();
        else self->resetLevel();
        self->startMusic();
    });
    return true;
}

} // namespace starlight::extras
