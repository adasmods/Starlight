// Hooks that don't belong to PlayLayer: speedhack, fps, audio, bypasses, misc.

#include "Config.hpp"
#include "State.hpp"
#include "visuals/Labels.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCLabelBMFont.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/CCTransitionFade.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <cmath>

using namespace geode::prelude;
using namespace starlight;

// ---------------------------------------------------------------- scheduler
// Speedhack, FPS bypass, audio pitch, fps counter, deferred actions.

static void applyFpsBypass() {
    static bool applied = false;
    static double original = 1.0 / 60.0;
    auto dir = CCDirector::get();
    auto& c = cfg();
    if (c.fpsBypass) {
        if (!applied) {
            original = dir->getAnimationInterval();
            applied = true;
        }
        double want = 1.0 / std::max(c.fps, 1.f);
        if (std::abs(dir->getAnimationInterval() - want) > 1e-7) dir->setAnimationInterval(want);
    } else if (applied) {
        dir->setAnimationInterval(original);
        applied = false;
    }
}

static void applyAudioPitch() {
    static float appliedPitch = 1.f;
    auto& c = cfg();
    float want = (c.speedhack && c.speedhackAudio) ? c.speed : 1.f;
    if (want == appliedPitch) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_globalChannel) {
        engine->m_globalChannel->setPitch(want);
        appliedPitch = want;
    }
}

class $modify(StarlightScheduler, CCScheduler) {
    void update(float dt) {
        runDeferred();
        applyFpsBypass();
        applyAudioPitch();

        // smoothed real fps for the label
        auto& st = state();
        if (dt > 0.f) st.fps = st.fps <= 0.f ? 1.f / dt : st.fps + (1.f / dt - st.fps) * 0.08f;

        auto& c = cfg();
        float mult = c.speedhack ? std::clamp(c.speed, 0.01f, 100.f) : 1.f;
        CCScheduler::update(dt * mult);

        // labels refresh even while the level is paused (label editor)
        labels::tick(dt);
    }
};

// ---------------------------------------------------------------- transitions

class $modify(StarlightTransitionFade, CCTransitionFade) {
    static CCTransitionFade* create(float duration, CCScene* scene) {
        auto& c = cfg();
        if (c.customTransition) duration *= std::max(c.transitionTime, 0.f);
        return CCTransitionFade::create(duration, scene);
    }
};

// ---------------------------------------------------------------- particles

class $modify(StarlightParticles, CCParticleSystemQuad) {
    void draw() {
        if (cfg().noParticles) return;
        CCParticleSystemQuad::draw();
    }
};

// ---------------------------------------------------------------- bypasses

class $modify(StarlightGameManager, GameManager) {
    bool isIconUnlocked(int id, IconType type) {
        if (cfg().unlockIcons) return true;
        return GameManager::isIconUnlocked(id, type);
    }
    bool isColorUnlocked(int id, UnlockType type) {
        if (cfg().unlockIcons) return true;
        return GameManager::isColorUnlocked(id, type);
    }
};

class $modify(StarlightStats, GameStatsManager) {
    bool isItemUnlocked(UnlockType type, int key) {
        // 0xC / 17 = practice music sync
        if (cfg().practiceMusic && static_cast<int>(type) == 0xC && key == 17) return true;
        return GameStatsManager::isItemUnlocked(type, key);
    }
};

class $modify(StarlightTextInput, CCTextInputNode) {
    void updateLabel(gd::string str) {
        auto& c = cfg();
        if (c.charFilterBypass) {
            m_allowedChars =
                " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
        }
        if (c.textLengthBypass) m_maxLabelLength = 99999;
        CCTextInputNode::updateLabel(str);
    }
};

// ---------------------------------------------------------------- gameplay misc

class $modify(StarlightBaseLayer, GJBaseGameLayer) {
    void toggleFlipped(bool flip, bool instant) {
        if (cfg().noMirror) return;
        GJBaseGameLayer::toggleFlipped(flip, instant);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (down) {
            auto& clicks = state().clicks;
            clicks.push_back(nowSeconds());
            while (clicks.size() > 200) clicks.pop_front();
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(StarlightPlayerObject, PlayerObject) {
    void playDeathEffect() {
        if (cfg().noDeathEffect) return;
        PlayerObject::playDeathEffect();
    }
};

// ---------------------------------------------------------------- nick changer
// Purely visual: any label showing your own username shows the custom nick instead.
// Nothing is sent to the servers.

static bool isOwnName(char const* str) {
    if (!str || !*str) return false;
    if (auto am = GJAccountManager::sharedState(); am && am->m_accountID > 0) {
        if (std::string(am->m_username) == str) return true;
    }
    std::string playerName = GameManager::get()->m_playerName;
    return !playerName.empty() && playerName != "Player" && playerName == str;
}

class $modify(StarlightNickLabel, CCLabelBMFont) {
    void setString(char const* str, bool needUpdateLabel) {
        auto& c = cfg();
        if (c.nickChanger && !c.nick.empty() && isOwnName(str)) {
            CCLabelBMFont::setString(c.nick.c_str(), needUpdateLabel);
            return;
        }
        CCLabelBMFont::setString(str, needUpdateLabel);
    }
};

class $modify(StarlightPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (cfg().hidePauseMenu) this->setVisible(false);
    }
};
