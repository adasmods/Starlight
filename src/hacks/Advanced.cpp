// Bigger hacks: replay bot playback, frame stepper, TPS (physics) bypass,
// no shaders, no object glow, shop unlocks, show taps, pause countdown.

#include "Config.hpp"
#include "State.hpp"
#include "bot/Bot.hpp"
#include "visuals/Textures.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCTouchDispatcher.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/ShaderLayer.hpp>

#include <chrono>
#include <cmath>
#include <functional>

using namespace geode::prelude;
using namespace starlight;

namespace {

bool isPlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

bool tpsActive() {
    auto& c = cfg();
    return c.tpsBypass && c.tps > 0.f && c.tps != 240.f;
}

float tickRate() { return tpsActive() ? std::clamp(cfg().tps, 1.f, 100000.f) : 240.f; }

} // namespace

// ---------------------------------------------------------------- physics: bot, stepper, TPS

class $modify(StarlightPhysicsLayer, GJBaseGameLayer) {
    struct Fields {
        double tpsAccum = 0.0;
    };

    // runs once per physics tick: perfect place for replay inputs
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (isPlayLayer(this)) bot::playbackTick(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    // custom timestep for the TPS bypass
    double getModifiedDelta(float dt) {
        if (!isPlayLayer(this) || !tpsActive()) return GJBaseGameLayer::getModifiedDelta(dt);
        double spt = 1.0 / tickRate();
        if (m_resumeTimer > 0) {
            --m_resumeTimer;
            dt = 0.f;
        }
        double total = dt + m_extraDelta;
        double step = std::min(m_gameState.m_timeWarp, 1.f) * spt;
        double steps = std::round(total / step);
        double newDelta = steps * step;
        m_extraDelta = total - newDelta;
        return newDelta;
    }

    void update(float dt) {
        if (!isPlayLayer(this)) return GJBaseGameLayer::update(dt);
        auto& c = cfg();
        auto& st = state();

        // frame stepper: the level is frozen, each key press advances one tick
        if (c.frameStepper) {
            if (st.stepRequests <= 0) return;
            st.stepRequests--;
            GJBaseGameLayer::update(static_cast<float>(1.0 / tickRate()));
            return;
        }
        st.stepRequests = 0;

        if (!tpsActive()) return GJBaseGameLayer::update(dt);

        // TPS bypass: run as many fixed steps as the frame needs
        auto& acc = m_fields->tpsAccum;
        acc += dt;
        double stepDelta = 1.0 / tickRate();
        if (acc < stepDelta) return;
        auto steps = static_cast<size_t>(acc / stepDelta);
        acc -= steps * stepDelta;

        auto start = std::chrono::steady_clock::now();
        auto budget = std::chrono::duration<double>(std::max(dt, 1.f / 60.f));
        st.tpsSubstep = true;
        while (steps > 1) {
            GJBaseGameLayer::update(static_cast<float>(stepDelta));
            --steps;
            if (std::chrono::steady_clock::now() - start > budget) break; // never freeze the game
        }
        st.tpsSubstep = false;
        GJBaseGameLayer::update(static_cast<float>(stepDelta * steps));
    }
};

// ---------------------------------------------------------------- visuals

class $modify(StarlightShaderLayer, ShaderLayer) {
    void visit() {
        if (cfg().noShaders) {
            CCNode::visit();
            return;
        }
        ShaderLayer::visit();
    }
};

class $modify(StarlightGameObject, GameObject) {
    void addGlow(gd::string frame) {
        if (cfg().noObjectGlow) return;
        GameObject::addGlow(frame);
    }
};

class $modify(StarlightShop, GameStatsManager) {
    bool isStoreItemUnlocked(int index) {
        if (cfg().unlockShop) return true;
        return GameStatsManager::isStoreItemUnlocked(index);
    }
};

// ---------------------------------------------------------------- show taps

namespace {

void spawnRipple(CCPoint pos) {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto ring = CCSprite::createWithTexture(tex::ring(64));
    ring->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    ring->setColor({200, 150, 255});
    ring->setPosition(pos);
    ring->setScale(10.f / ring->getContentSize().width);
    ring->runAction(CCSequence::create(
        CCSpawn::create(CCEaseOut::create(CCScaleTo::create(0.45f, 46.f / ring->getContentSize().width), 2.f), CCFadeOut::create(0.45f), nullptr),
        CCRemoveSelf::create(), nullptr
    ));
    scene->addChild(ring, 100000);

    auto dot = CCSprite::createWithTexture(tex::glow(64));
    dot->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    dot->setColor({255, 220, 245});
    dot->setPosition(pos);
    dot->setScale(22.f / dot->getContentSize().width);
    dot->runAction(CCSequence::create(CCFadeOut::create(0.3f), CCRemoveSelf::create(), nullptr));
    scene->addChild(dot, 100000);
}

} // namespace

class $modify(StarlightTouches, CCTouchDispatcher) {
    void touches(CCSet* touches, CCEvent* event, unsigned int type) {
        if (cfg().showTaps && type == 0 /* began */ && touches && PlayLayer::get()) {
            for (auto it = touches->begin(); it != touches->end(); ++it) {
                if (auto touch = static_cast<CCTouch*>(*it)) spawnRipple(touch->getLocation());
            }
        }
        CCTouchDispatcher::touches(touches, event, type);
    }
};

// ---------------------------------------------------------------- pause countdown

namespace {

// 3 - 2 - 1 in the Starlight style, then runs the callback
class CountdownNode : public CCNode {
public:
    static CountdownNode* create(std::function<void()> done) {
        auto ret = new CountdownNode();
        ret->m_done = std::move(done);
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        auto win = CCDirector::get()->getWinSize();

        m_glow = CCSprite::createWithTexture(tex::glow(64));
        m_glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        m_glow->setColor({150, 90, 255});
        m_glow->setScale(150.f / m_glow->getContentSize().width);
        m_glow->setPosition({win.width * 0.5f, win.height * 0.5f});
        this->addChild(m_glow);

        m_label = CCLabelTTF::create("3", "Candara", 90.f);
        m_label->setColor({255, 243, 226});
        m_label->setPosition({win.width * 0.5f, win.height * 0.5f});
        this->addChild(m_label, 1);

        this->showNumber(3);
        this->scheduleUpdate();
        return true;
    }

    void update(float dt) override {
        m_time += dt;
        int number = 3 - static_cast<int>(m_time / 0.6f);
        if (number <= 0) {
            auto done = std::move(m_done);
            this->removeFromParent();
            if (done) done();
            return;
        }
        if (number != m_number) this->showNumber(number);
    }

private:
    void showNumber(int n) {
        m_number = n;
        m_label->setString(std::to_string(n).c_str());
        m_label->stopAllActions();
        m_label->setScale(0.9f);
        m_label->setOpacity(255);
        m_label->runAction(CCSpawn::create(
            CCEaseBackOut::create(CCScaleTo::create(0.3f, 0.55f)),
            CCSequence::create(CCDelayTime::create(0.35f), CCFadeTo::create(0.25f, 90), nullptr), nullptr
        ));
        m_glow->stopAllActions();
        m_glow->setOpacity(200);
        m_glow->runAction(CCFadeTo::create(0.6f, 60));
    }

    std::function<void()> m_done;
    CCLabelTTF* m_label = nullptr;
    CCSprite* m_glow = nullptr;
    float m_time = 0.f;
    int m_number = 0;
};

bool s_countdownDone = false;

} // namespace

class $modify(StarlightPauseCountdown, PauseLayer) {
    struct Fields {
        bool counting = false;
    };

    void onResume(CCObject* sender) {
        if (!cfg().pauseCountdown || s_countdownDone || !PlayLayer::get()) return PauseLayer::onResume(sender);
        if (m_fields->counting) return;
        m_fields->counting = true;
        this->setVisible(false);

        Ref<PauseLayer> self = this;
        auto countdown = CountdownNode::create([self, sender] {
            s_countdownDone = true;
            self->onResume(sender);
            s_countdownDone = false;
        });
        if (auto scene = CCDirector::get()->getRunningScene()) scene->addChild(countdown, 100000);
    }
};
