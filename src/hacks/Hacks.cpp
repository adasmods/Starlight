// Hooks that don't belong to PlayLayer: speedhack, fps, audio, bypasses, misc.

#include "Config.hpp"
#include "bot/Bot.hpp"
#include "discord/DiscordRPC.hpp"
#include "State.hpp"
#include "ui/Menu.hpp"
#include "visuals/Labels.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCLabelBMFont.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/CCTransitionFade.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

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
    if (c.pitchShift) want *= std::clamp(c.pitch, 0.1f, 4.f);
    if (want == appliedPitch) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_globalChannel) {
        engine->m_globalChannel->setPitch(want);
        appliedPitch = want;
    }
}

// keeps the cursor inside the game window while playing
static void applyCursorLock() {
    static bool locked = false;
    bool want = false;
    if (cfg().lockCursor && !menu::isOpen()) {
        if (auto pl = PlayLayer::get(); pl && !pl->m_isPaused && !pl->m_hasCompletedLevel) {
            HWND fg = GetForegroundWindow();
            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            if (fg && pid == GetCurrentProcessId()) {
                RECT r;
                GetClientRect(fg, &r);
                POINT tl{r.left, r.top}, br{r.right, r.bottom};
                ClientToScreen(fg, &tl);
                ClientToScreen(fg, &br);
                RECT clip{tl.x, tl.y, br.x, br.y};
                ClipCursor(&clip);
                locked = true;
                want = true;
            }
        }
    }
    if (!want && locked) {
        ClipCursor(nullptr);
        locked = false;
    }
}

class $modify(StarlightScheduler, CCScheduler) {
    void update(float dt) {
        runDeferred();
        applyFpsBypass();
        applyAudioPitch();
        applyCursorLock();

        // smoothed real fps for the label
        auto& st = state();
        if (dt > 0.f) st.fps = st.fps <= 0.f ? 1.f / dt : st.fps + (1.f / dt - st.fps) * 0.08f;

        auto& c = cfg();
        float mult = c.speedhack ? std::clamp(c.speed, 0.01f, 100.f) : 1.f;
        CCScheduler::update(dt * mult);

        // labels refresh even while the level is paused (label editor)
        labels::tick(dt);
        discord::tick(dt);
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
        auto& c = cfg();
        if (c.noMirror) return;
        GJBaseGameLayer::toggleFlipped(flip, instant || c.instantMirror);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        bool inLevel = PlayLayer::get() && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this;
        if (inLevel && !bot::injecting()) {
            // during playback the replay owns the controls
            if (bot::mode() == bot::Mode::Playback && cfg().botIgnoreInputs) return;
            bot::recordInput(this, button, down, isPlayer1);
        }

        if (down) {
            auto& clicks = state().clicks;
            clicks.push_back(nowSeconds());
            while (clicks.size() > 200) clicks.pop_front();

            // jump hack: pretend we're on the ground so you can jump mid-air
            if (cfg().jumpHack && button == 1) {
                for (auto p : {m_player1, m_player2}) {
                    if (p) p->m_isOnGround = true;
                }
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void shakeCamera(float duration, float strength, float interval) {
        auto& c = cfg();
        if (c.noCameraShake) return;
        if (c.noEndShake) {
            if (auto pl = PlayLayer::get(); pl && pl->m_hasCompletedLevel) return;
        }
        GJBaseGameLayer::shakeCamera(duration, strength, interval);
    }

    void lightningFlash(
        CCPoint from, CCPoint to, ccColor3B color, float lineWidth, float duration, int displacement, bool flash, float opacity
    ) {
        if (cfg().noPortalLightning) return;
        GJBaseGameLayer::lightningFlash(from, to, color, lineWidth, duration, displacement, flash, opacity);
    }
};

class $modify(StarlightPlayerObject, PlayerObject) {
    void playDeathEffect() {
        if (cfg().noDeathEffect) return;
        PlayerObject::playDeathEffect();
    }

    void playSpawnEffect() {
        if (cfg().noRespawnFlash) return;
        PlayerObject::playSpawnEffect();
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (cfg().noSpiderDash) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void toggleGhostEffect(GhostType type) {
        auto& c = cfg();
        if (c.noGhostTrail) type = static_cast<GhostType>(0);
        else if (c.forceGhostTrail) type = static_cast<GhostType>(1);
        PlayerObject::toggleGhostEffect(type);
    }
};

// ---------------------------------------------------------------- editor

class $modify(StarlightEditorUI, EditorUI) {
    void constrainGameLayerPosition(float x, float y) {
        if (cfg().freeScroll) return;
        EditorUI::constrainGameLayerPosition(x, y);
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

// set while re-running a confirmed pause action, so the hook doesn't ask again
static bool s_confirmed = false;

class $modify(StarlightPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (cfg().hidePauseMenu) this->setVisible(false);
    }

    // shows a confirm popup and re-runs `action` on "yes"
    template <class F>
    void sl_confirm(char const* title, char const* text, char const* yes, F&& action) {
        Ref<PauseLayer> self = this;
        createQuickPopup(title, text, "Cancel", yes, [self, action](FLAlertLayer*, bool ok) {
            if (!ok) return;
            s_confirmed = true;
            action(self.data());
            s_confirmed = false;
        });
    }

    void onRestart(CCObject* sender) {
        if (!cfg().confirmRestart || s_confirmed) return PauseLayer::onRestart(sender);
        sl_confirm("Restart", "Restart the level?", "Restart", [sender](PauseLayer* p) { p->onRestart(sender); });
    }

    void onRestartFull(CCObject* sender) {
        if (!cfg().confirmRestart || s_confirmed) return PauseLayer::onRestartFull(sender);
        sl_confirm("Restart", "Restart the level from the start?", "Restart", [sender](PauseLayer* p) { p->onRestartFull(sender); });
    }

    void onPracticeMode(CCObject* sender) {
        if (!cfg().confirmPractice || s_confirmed) return PauseLayer::onPracticeMode(sender);
        sl_confirm("Practice", "Enter practice mode?", "Practice", [sender](PauseLayer* p) { p->onPracticeMode(sender); });
    }

    void onNormalMode(CCObject* sender) {
        if (!cfg().confirmPractice || s_confirmed) return PauseLayer::onNormalMode(sender);
        sl_confirm("Practice", "Leave practice mode?", "Leave", [sender](PauseLayer* p) { p->onNormalMode(sender); });
    }
};
