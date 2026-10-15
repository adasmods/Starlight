// Everything that runs inside a level: noclip, safe mode, respawn delay,
// player tweaks, hitboxes, skybox, orbs and labels.

#include "Config.hpp"
#include "State.hpp"
#include "visuals/OrbTrail.hpp"
#include "visuals/Skybox.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

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

constexpr ccColor3B kWhite{255, 255, 255};
constexpr ccColor3B kRed{255, 70, 100};
constexpr ccColor3B kGreen{90, 255, 150};

} // namespace

class $modify(StarlightPlayLayer, PlayLayer) {
    struct Fields {
        SkyboxNode* sky = nullptr;
        OrbTrailNode* orbs = nullptr;
        CCNode* labels = nullptr;
        std::vector<CCLabelBMFont*> labelPool;
        CCLayerColor* flash = nullptr;
        float flashA = 0.f;
        bool cheated = false;
        bool hitboxesWereOn = false;
        VisOverride background;
        VisOverride ground[2];
        VisOverride player[2];
        VisOverride trail[2];
        VisOverride wave[2];
    };

    bool sl_shouldBlockProgress() {
        auto& c = cfg();
        return c.safeMode || (c.autoSafeMode && (m_fields->cheated || cheatsActive()));
    }

    // ------------------------------------------------------------ lifecycle

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        state().resetAttempt();
        state().sessionTime = 0.0;
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        m_fields->cheated = cheatsActive();

        auto flash = CCLayerColor::create({255, 0, 0, 0});
        this->addChild(flash, 9998);
        m_fields->flash = flash;

        auto labels = CCNode::create();
        this->addChild(labels, 9999);
        m_fields->labels = labels;
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        state().resetAttempt();
        m_fields->cheated = cheatsActive();
        if (m_fields->orbs) m_fields->orbs->snap();
    }

    // ------------------------------------------------------------ noclip / safe mode / respawn

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& c = cfg();
        if (c.noclip && object != m_anticheatSpike) {
            state().noclipHitThisFrame = true;
            m_fields->cheated = true;
            return;
        }

        bool block = sl_shouldBlockProgress();
        bool prevTest = m_isTestMode;
        if (block) m_isTestMode = true;
        PlayLayer::destroyPlayer(player, object);
        m_isTestMode = prevTest;

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
    }

    // ------------------------------------------------------------ per frame

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& st = state();
        if (cheatsActive()) m_fields->cheated = true;

        st.noclipFrames++;
        if (st.noclipHitThisFrame) {
            st.noclipDeadFrames++;
            if (!st.noclipWasHit) st.noclipDeaths++;
            if (cfg().noclipFlash) m_fields->flashA = 1.f;
        }
        st.noclipWasHit = st.noclipHitThisFrame;
        st.noclipHitThisFrame = false;
        st.sessionTime += dt;

        this->sl_updateFlash(dt);
        this->sl_updatePlayerTweaks();
        this->sl_updateHitboxes();
        this->sl_updateVisuals(dt);
        this->sl_updateLabels();
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
        }
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
    }

    void sl_updateLabels() {
        auto node = m_fields->labels;
        if (!node) return;
        auto& c = cfg();
        auto& st = state();

        std::vector<std::pair<std::string, ccColor3B>> lines;
        if (c.labels) {
            if (c.lblCheat) lines.emplace_back(".", (m_fields->cheated || cheatsActive()) ? kRed : kGreen);
            if (c.lblFps) lines.emplace_back(fmt::format("{:.0f} FPS", st.fps), kWhite);
            if (c.lblCps) {
                double now = nowSeconds();
                auto& clicks = st.clicks;
                while (!clicks.empty() && now - clicks.front() > 1.0) clicks.pop_front();
                lines.emplace_back(fmt::format("{} CPS", clicks.size()), kWhite);
            }
            if (c.lblNoclipAcc) lines.emplace_back(fmt::format("{:.2f}%", st.noclipAccuracy()), st.noclipWasHit ? kRed : kWhite);
            if (c.lblNoclipDeaths) lines.emplace_back(fmt::format("{} Deaths", st.noclipDeaths), kWhite);
            if (c.lblAttempt) lines.emplace_back(fmt::format("Attempt {}", m_attempts), kWhite);
            if (c.lblPercent) lines.emplace_back(fmt::format("{:.2f}%", this->getCurrentPercent()), kWhite);
            if (c.lblTime) {
                int s = static_cast<int>(st.sessionTime);
                lines.emplace_back(fmt::format("{}:{:02}", s / 60, s % 60), kWhite);
            }
            if (c.lblClock) {
                std::time_t t = std::time(nullptr);
                std::tm tm{};
                localtime_s(&tm, &t);
                lines.emplace_back(fmt::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec), kWhite);
            }
        }

        auto& pool = m_fields->labelPool;
        while (pool.size() < lines.size()) {
            auto label = CCLabelBMFont::create("", "bigFont.fnt");
            node->addChild(label);
            pool.push_back(label);
        }

        auto win = CCDirector::get()->getWinSize();
        bool right = c.labelCorner == 1 || c.labelCorner == 3;
        bool bottom = c.labelCorner >= 2;
        float x = right ? win.width - 5.f : 5.f;
        float y = bottom ? 3.f : win.height - 3.f;

        for (size_t i = 0; i < pool.size(); i++) {
            auto label = pool[i];
            if (i >= lines.size()) {
                label->setVisible(false);
                continue;
            }
            auto const& [text, color] = lines[i];
            label->setVisible(true);
            if (std::string_view(label->getString()) != text) label->setString(text.c_str());
            label->setScale(c.labelScale * (text == "." ? 1.6f : 1.f));
            label->setColor(color);
            label->setOpacity(static_cast<GLubyte>(std::clamp(c.labelOpacity, 0.f, 1.f) * 255.f));
            label->setAnchorPoint({right ? 1.f : 0.f, bottom ? 0.f : 1.f});
            label->setPosition({x, y});
            float h = label->getContentSize().height * label->getScale() * 0.8f;
            y += bottom ? h : -h;
        }
    }
};
