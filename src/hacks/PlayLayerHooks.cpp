// Everything that runs inside a level: noclip, safe mode, respawn delay,
// player tweaks, hitboxes, skybox and orbs. Labels live in visuals/Labels.cpp.

#include "Config.hpp"
#include "State.hpp"
#include "bot/Bot.hpp"
#include "hacks/Trajectory.hpp"
#include "visuals/CosmicTrail.hpp"
#include "visuals/Labels.hpp"
#include "visuals/OrbTrail.hpp"
#include "visuals/Skybox.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>


using namespace geode::prelude;
using namespace starlight;

namespace {

ccColor3B toC3(float const* c) {
    return {static_cast<GLubyte>(std::clamp(c[0], 0.f, 1.f) * 255.f), static_cast<GLubyte>(std::clamp(c[1], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(c[2], 0.f, 1.f) * 255.f)};
}

// Hides a node while `hide` is on and restores its previous visibility afterwards,
// so we don't fight the game when the option is off.
struct VisOverride {
    bool active = false;
    bool prev = true;

    void apply(CCNode* node, bool hide) {
        if (!node) return;
        if (hide) {
            if (!active) {
                prev = node->isVisible();
                active = true;
            }
            node->setVisible(false);
        } else if (active) {
            node->setVisible(prev);
            active = false;
        }
    }
};

} // namespace

class $modify(StarlightPlayLayer, PlayLayer) {
    struct Fields {
        SkyboxNode* sky = nullptr;
        OrbTrailNode* orbs = nullptr;
        CosmicTrailNode* cosmic = nullptr;
        CCLayerColor* flash = nullptr;
        float flashA = 0.f;
        bool hitboxesWereOn = false;
        VisOverride background;
        VisOverride ground[2];
        VisOverride player[2];
        VisOverride trail[2];
        VisOverride wave[2];
        VisOverride robotFire[2];
        bool solidWaveOn = false;
        bool practiceApplied = false;
        bool instantDone = false;
        bool autoKilled = false;
        bool forceKill = false;
        bool musicMuted = false;
        bool clickerDown = false;
        float clickerTimer = 0.f;
    };

    bool sl_shouldBlockProgress() {
        auto& c = cfg();
        return c.safeMode || (c.autoSafeMode && (state().cheatedAttempt || cheatsActive()));
    }

    // ------------------------------------------------------------ lifecycle

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto& c = cfg();
        state().resetAttempt();
        state().sessionTime = 0.0;
        state().runStart = 0.f;
        state().bestStart = state().bestEnd = 0.f;
        if (c.autoLDM && level) level->m_lowDetailModeToggled = true;
        trajectory::reset();
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        state().cheatedAttempt = cheatsActive();

        if (c.forcePlatformer) {
            m_isPlatformer = true;
            if (m_levelSettings) m_levelSettings->m_platformerMode = true;
            for (auto p : {m_player1, m_player2}) {
                if (p) p->togglePlatformerMode(true);
            }
        }
        if (c.forceGhostTrail) {
            for (auto p : {m_player1, m_player2}) {
                if (p) p->toggleGhostEffect(static_cast<GhostType>(1));
            }
        }

        auto flash = CCLayerColor::create({255, 0, 0, 0});
        this->addChild(flash, 9998);
        m_fields->flash = flash;

        this->addChild(LabelsNode::create(), 9999);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        state().resetAttempt();
        state().cheatedAttempt = cheatsActive();
        state().runStart = this->getCurrentPercent();
        m_fields->autoKilled = false;
        bot::onReset(this);
        if (m_fields->musicMuted) {
            FMODAudioEngine::sharedEngine()->resumeAllMusic();
            m_fields->musicMuted = false;
        }
        if (m_fields->orbs) m_fields->orbs->snap();
        if (m_fields->cosmic) m_fields->cosmic->clearTrail();
    }

    // ------------------------------------------------------------ noclip / safe mode / respawn

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // trajectory ghosts never really die
        if (trajectory::simulating()) {
            trajectory::onSimulatedDeath();
            return;
        }
        auto& c = cfg();
        if (c.noclip && object != m_anticheatSpike && !m_fields->forceKill) {
            state().noclipHitThisFrame = true;
            state().cheatedAttempt = true;
            return;
        }

        bool block = sl_shouldBlockProgress();
        bool prevTest = m_isTestMode;
        if (block) m_isTestMode = true;
        bool wasDead = player && player->m_isDead;
        PlayLayer::destroyPlayer(player, object);
        m_isTestMode = prevTest;

        if (player && player->m_isDead && !wasDead) {
            this->sl_recordRun(this->getCurrentPercent());
            if (c.muteOnDeath && !m_isPracticeMode) {
                FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
                m_fields->musicMuted = true;
            }
        }

        if (c.customRespawn) {
            if (auto action = this->getActionByTag(0x10)) {
                this->stopAction(action);
                auto seq = CCSequence::create(
                    CCDelayTime::create(std::max(c.respawnDelay, 0.f)),
                    CCCallFunc::create(this, callfunc_selector(PlayLayer::delayedResetLevel)), nullptr
                );
                seq->setTag(0x10);
                this->runAction(seq);
            }
        }
    }

    void levelComplete() {
        bool block = sl_shouldBlockProgress();
        bool prevTest = m_isTestMode;
        if (block) m_isTestMode = true;
        PlayLayer::levelComplete();
        m_isTestMode = prevTest;
        this->sl_recordRun(100.f);
        // keep the finished recording safe from the next restart
        if (bot::mode() == bot::Mode::Record) bot::setMode(bot::Mode::Disabled);
    }

    void sl_recordRun(float end) {
        auto& st = state();
        if (end - st.runStart > st.bestEnd - st.bestStart) {
            st.bestStart = st.runStart;
            st.bestEnd = end;
        }
    }

    // ------------------------------------------------------------ small level tweaks

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        if (cfg().accuratePercent && m_percentageLabel && !m_isPlatformer) {
            m_percentageLabel->setString(fmt::format("{:.2f}%", this->getCurrentPercent()).c_str());
        }
    }

    void updateAttempts() {
        if (cfg().freezeAttempts) return;
        PlayLayer::updateAttempts();
    }

    void showCompleteEffect() {
        if (cfg().hideCompleteVfx) return;
        PlayLayer::showCompleteEffect();
    }

    // ------------------------------------------------------------ per frame

    void postUpdate(float dt) {
        // TPS bypass extra steps: skip visuals/labels work for speed
        if (state().tpsSubstep) return;
        PlayLayer::postUpdate(dt);

        auto& st = state();
        if (cheatsActive()) state().cheatedAttempt = true;

        st.noclipFrames++;
        if (st.noclipHitThisFrame) {
            st.noclipDeadFrames++;
            if (!st.noclipWasHit) st.noclipDeaths++;
            if (cfg().noclipFlash) m_fields->flashA = 1.f;
        }
        st.noclipWasHit = st.noclipHitThisFrame;
        st.noclipHitThisFrame = false;
        st.sessionTime += dt;

        this->sl_updateAutomation(dt);
        this->sl_updateFlash(dt);
        this->sl_updatePlayerTweaks();
        this->sl_updateHitboxes();
        this->sl_updateVisuals(dt);
        trajectory::update(this);
    }

    // auto practice, instant complete, auto kill, autoclicker
    void sl_updateAutomation(float dt) {
        auto& c = cfg();

        if (c.autoPractice && !m_fields->practiceApplied) {
            m_fields->practiceApplied = true;
            if (!m_isPracticeMode) {
                Ref<PlayLayer> self = this;
                defer([self] { self->togglePracticeMode(true); });
            }
        }

        if (c.instantComplete && !m_fields->instantDone && !m_hasCompletedLevel && m_gameState.m_levelTime > 0.2) {
            m_fields->instantDone = true;
            this->levelComplete();
            return;
        }

        if (c.autoKill && !m_fields->autoKilled && m_player1 && !m_player1->m_isDead &&
            this->getCurrentPercent() >= c.autoKillPercent) {
            m_fields->autoKilled = true;
            m_fields->forceKill = true;
            this->destroyPlayer(m_player1, nullptr);
            m_fields->forceKill = false;
        }

        if (c.autoClicker && !m_hasCompletedLevel) {
            float period = 1.f / std::clamp(c.autoClickerCps, 1.f, 60.f);
            m_fields->clickerTimer += dt;
            if (!m_fields->clickerDown && m_fields->clickerTimer >= period) {
                this->handleButton(true, 1, true);
                m_fields->clickerDown = true;
                m_fields->clickerTimer = 0.f;
            } else if (m_fields->clickerDown && m_fields->clickerTimer >= period * 0.5f) {
                this->handleButton(false, 1, true);
                m_fields->clickerDown = false;
            }
        } else if (m_fields->clickerDown) {
            this->handleButton(false, 1, true);
            m_fields->clickerDown = false;
        }
    }

    void sl_updateFlash(float dt) {
        auto flash = m_fields->flash;
        if (!flash) return;
        auto& f = m_fields->flashA;
        f = std::max(0.f, f - dt * 4.f);
        flash->setColor(toC3(cfg().noclipFlashColor));
        flash->setOpacity(static_cast<GLubyte>(f * 90.f));
    }

    void sl_updatePlayerTweaks() {
        auto& c = cfg();
        PlayerObject* players[2] = {m_player1, m_player2};
        for (int i = 0; i < 2; i++) {
            auto p = players[i];
            if (!p) continue;
            m_fields->player[i].apply(p, c.hidePlayer);
            m_fields->trail[i].apply(p->m_regularTrail, c.noTrail);
            m_fields->wave[i].apply(p->m_waveTrail, c.noWaveTrail);
            m_fields->robotFire[i].apply(p->m_robotFire, c.noRobotFire);

            if (p->m_waveTrail && (c.solidWaveTrail || m_fields->solidWaveOn)) {
                p->m_waveTrail->m_isSolid = c.solidWaveTrail;
                p->m_waveTrail->setBlendFunc(
                    c.solidWaveTrail ? ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA} : ccBlendFunc{GL_SRC_ALPHA, GL_ONE}
                );
            }
        }
        m_fields->solidWaveOn = c.solidWaveTrail;
    }

    void sl_updateHitboxes() {
        if (!m_debugDrawNode) return;
        if (cfg().showHitboxes) {
            m_fields->hitboxesWereOn = true;
            bool prev = m_isDebugDrawEnabled;
            m_isDebugDrawEnabled = true;
            this->updateDebugDraw();
            m_isDebugDrawEnabled = prev;
            m_debugDrawNode->setVisible(true);
        } else if (m_fields->hitboxesWereOn) {
            m_fields->hitboxesWereOn = false;
            m_debugDrawNode->clear();
            m_debugDrawNode->setVisible(m_isDebugDrawEnabled);
        }
    }

    void sl_updateVisuals(float dt) {
        auto& c = cfg();
        auto win = CCDirector::get()->getWinSize();

        // ---- skybox (replaces the level background)
        if (c.skybox != 0 && m_background && m_background->getParent()) {
            if (!m_fields->sky) {
                auto sky = SkyboxNode::create();
                m_background->getParent()->addChild(sky, m_background->getZOrder());
                m_fields->sky = sky;
            }
            auto sky = m_fields->sky;
            sky->setPreset(c.skybox, c.starDensity);
            sky->setScroll(c.skyParallax && m_player1 ? m_player1->getPositionX() : 0.f);

            // keep it covering the screen whatever the parent does
            auto parent = sky->getParent();
            CCPoint bl = parent->convertToNodeSpace({0.f, 0.f});
            CCPoint tr = parent->convertToNodeSpace({win.width, win.height});
            sky->setPosition(bl);
            sky->setScaleX((tr.x - bl.x) / win.width);
            sky->setScaleY((tr.y - bl.y) / win.height);

            m_fields->background.apply(m_background, true);
        } else {
            if (m_fields->sky) {
                m_fields->sky->removeFromParent();
                m_fields->sky = nullptr;
            }
            m_fields->background.apply(m_background, false);
        }

        // ---- ground
        m_fields->ground[0].apply(m_groundLayer, c.hideGround);
        m_fields->ground[1].apply(m_groundLayer2, c.hideGround);

        // ---- orbs + aura
        if ((c.orbs || c.playerAura) && m_player1 && m_objectLayer && m_player1->getParent()) {
            if (!m_fields->orbs) {
                auto orbs = OrbTrailNode::create();
                int z = m_player1->getParent() == m_objectLayer ? m_player1->getZOrder() - 1 : 1000;
                m_objectLayer->addChild(orbs, z);
                m_fields->orbs = orbs;
            }
            CCPoint world = m_player1->getParent()->convertToWorldSpace(m_player1->getPosition());
            CCPoint local = m_objectLayer->convertToNodeSpace(world);
            m_fields->orbs->step(dt, local, c.orbs, c.playerAura && m_player1->isVisible());
        } else if (m_fields->orbs) {
            m_fields->orbs->removeFromParent();
            m_fields->orbs = nullptr;
        }

        // ---- cosmic gradient trail
        if (c.cosmicTrail && m_player1 && m_objectLayer && m_player1->getParent()) {
            if (!m_fields->cosmic) {
                auto trail = CosmicTrailNode::create();
                int z = m_player1->getParent() == m_objectLayer ? m_player1->getZOrder() - 2 : 999;
                m_objectLayer->addChild(trail, z);
                m_fields->cosmic = trail;
            }
            CCPoint world = m_player1->getParent()->convertToWorldSpace(m_player1->getPosition());
            CCPoint local = m_objectLayer->convertToNodeSpace(world);
            m_fields->cosmic->step(dt, local, true);
        } else if (m_fields->cosmic) {
            m_fields->cosmic->removeFromParent();
            m_fields->cosmic = nullptr;
        }
    }

};
