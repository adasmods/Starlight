#include "Skybox.hpp"

#include "Config.hpp"
#include "Textures.hpp"

#include <cmath>

using namespace geode::prelude;

namespace starlight {

namespace {

struct SkyPreset {
    std::vector<ccColor3B> stops; // top -> bottom
    int stars;
    float starRegion;   // fraction of the screen (from the top) that has stars
    int celestial;      // 0 none, 1 moon, 2 sun
    ccColor3B celColor;
    ccColor3B celGlow;
    CCPoint celPos;     // normalized
    float celSize;      // points
    std::vector<ccColor3B> clouds;
    GLubyte cloudOpacity;
    ccColor3B horizon;
    GLubyte horizonOpacity;
};

SkyPreset const& preset(int id) {
    static SkyPreset const presets[] = {
        // 1: starry night
        {{{3, 2, 12}, {8, 8, 34}, {20, 16, 60}, {44, 26, 88}}, 170, 1.f, 1,
         {236, 236, 255}, {130, 150, 255}, {0.82f, 0.78f}, 46.f,
         {{90, 45, 170}, {35, 65, 170}}, 30, {120, 70, 200}, 70},
        // 2: evening sunset
        {{{18, 10, 46}, {58, 26, 98}, {150, 52, 112}, {238, 112, 90}, {255, 184, 112}}, 70, 0.45f, 2,
         {255, 222, 160}, {255, 130, 60}, {0.72f, 0.22f}, 58.f,
         {{255, 120, 130}, {190, 80, 170}}, 34, {255, 150, 80}, 120},
        // 3: deep nebula
        {{{2, 1, 8}, {14, 5, 32}, {32, 10, 56}, {10, 4, 28}}, 230, 1.f, 0,
         {0, 0, 0}, {0, 0, 0}, {0.f, 0.f}, 0.f,
         {{170, 60, 255}, {255, 70, 170}, {60, 110, 255}, {120, 40, 220}}, 62, {90, 40, 180}, 50},
        // 4: blood moon
        {{{6, 1, 4}, {30, 4, 14}, {70, 10, 24}, {110, 20, 30}}, 120, 1.f, 1,
         {255, 120, 100}, {255, 40, 40}, {0.78f, 0.74f}, 52.f,
         {{160, 20, 40}, {110, 10, 50}}, 34, {210, 40, 40}, 75},
    };
    int i = std::clamp(id, 1, 4) - 1;
    return presets[i];
}

void setDiameter(CCSprite* spr, float w, float h = -1.f) {
    auto cs = spr->getContentSize();
    spr->setScaleX(w / cs.width);
    spr->setScaleY((h < 0.f ? w : h) / cs.height);
}

void additive(CCSprite* spr) { spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE}); }

} // namespace

SkyboxNode* SkyboxNode::create() {
    auto ret = new SkyboxNode();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SkyboxNode::init() {
    if (!CCNode::init()) return false;
    m_size = CCDirector::get()->getWinSize();
    this->setContentSize(m_size);
    this->setAnchorPoint({0.f, 0.f});

    m_glowTex = tex::glow(64);
    m_glowTex->retain();

    m_content = CCNode::create();
    this->addChild(m_content, 0);
    m_shootingLayer = CCNode::create();
    this->addChild(m_shootingLayer, 1);

    this->scheduleUpdate();
    return true;
}

void SkyboxNode::setPreset(int presetId, float density) {
    if (presetId == m_preset && density == m_density) return;
    m_preset = presetId;
    m_density = density;
    this->rebuild();
}

void SkyboxNode::rebuild() {
    m_content->removeAllChildrenWithCleanup(true);
    m_stars.clear();
    m_clouds.clear();
    m_celestial = nullptr;
    m_celestialGlow = nullptr;

    auto const& p = preset(m_preset);
    float W = m_size.width, H = m_size.height;
    std::uniform_real_distribution<float> u(0.f, 1.f);
    m_rng.seed(1234 + m_preset);

    // gradient
    auto grad = CCSprite::createWithTexture(tex::gradient(p.stops));
    grad->setAnchorPoint({0.f, 0.f});
    setDiameter(grad, W, H);
    m_content->addChild(grad, 0);

    // horizon haze
    auto haze = CCSprite::createWithTexture(m_glowTex);
    additive(haze);
    haze->setColor(p.horizon);
    haze->setOpacity(p.horizonOpacity);
    setDiameter(haze, W * 1.7f, H * 0.75f);
    haze->setPosition({W * 0.5f, 0.f});
    m_content->addChild(haze, 1);

    // nebula clouds
    for (size_t i = 0; i < p.clouds.size() * 2; i++) {
        auto spr = CCSprite::createWithTexture(m_glowTex);
        additive(spr);
        spr->setColor(p.clouds[i % p.clouds.size()]);
        float d = 160.f + u(m_rng) * 200.f;
        setDiameter(spr, d, d * (0.45f + u(m_rng) * 0.4f));
        spr->setRotation(u(m_rng) * 360.f);
        Cloud c{spr, u(m_rng) * W, H * (0.25f + u(m_rng) * 0.75f), 0.1f + u(m_rng) * 0.25f, u(m_rng) * 6.28f,
                static_cast<float>(p.cloudOpacity) * (0.6f + u(m_rng) * 0.4f)};
        spr->setPosition({c.bx, c.by});
        spr->setOpacity(static_cast<GLubyte>(c.baseOpacity));
        m_content->addChild(spr, 2);
        m_clouds.push_back(c);
    }

    // stars
    int count = static_cast<int>(p.stars * m_density);
    for (int i = 0; i < count; i++) {
        auto spr = CCSprite::createWithTexture(m_glowTex);
        additive(spr);
        bool big = u(m_rng) > 0.92f;
        float d = big ? 5.f + u(m_rng) * 4.f : 1.6f + u(m_rng) * 2.2f;
        setDiameter(spr, d);
        float tint = u(m_rng);
        spr->setColor(tint < 0.6f ? ccColor3B{255, 255, 255} : tint < 0.8f ? ccColor3B{210, 190, 255} : ccColor3B{190, 215, 255});

        float yNorm = 1.f - u(m_rng) * p.starRegion; // 1 = top
        float fadeNearHorizon = std::clamp((yNorm - (1.f - p.starRegion)) / std::max(p.starRegion, 0.01f) * 1.4f, 0.f, 1.f);
        Star st{spr, u(m_rng), yNorm * H, 0.3f + u(m_rng) * 0.9f, u(m_rng) * 6.28f, 0.8f + u(m_rng) * 2.5f,
                (big ? 230.f : 110.f + u(m_rng) * 140.f) * fadeNearHorizon};
        m_content->addChild(spr, 3);
        m_stars.push_back(st);
    }

    // moon / sun
    if (p.celestial != 0) {
        CCPoint pos{p.celPos.x * W, p.celPos.y * H};
        m_celestialGlow = CCSprite::createWithTexture(m_glowTex);
        additive(m_celestialGlow);
        m_celestialGlow->setColor(p.celGlow);
        m_glowBase = p.celestial == 2 ? 220.f : 150.f;
        m_celestialGlow->setOpacity(static_cast<GLubyte>(m_glowBase));
        setDiameter(m_celestialGlow, p.celSize * (p.celestial == 2 ? 6.f : 4.5f));
        m_celestialGlow->setPosition(pos);
        m_content->addChild(m_celestialGlow, 4);

        if (p.celestial == 2) {
            auto inner = CCSprite::createWithTexture(m_glowTex);
            additive(inner);
            inner->setColor({255, 200, 120});
            inner->setOpacity(230);
            setDiameter(inner, p.celSize * 2.2f);
            inner->setPosition(pos);
            m_content->addChild(inner, 5);
        }

        m_celestial = CCSprite::createWithTexture(tex::disc(128, p.celestial == 1));
        m_celestial->setColor(p.celColor);
        setDiameter(m_celestial, p.celSize);
        m_celestial->setPosition(pos);
        m_content->addChild(m_celestial, 6);
    }
}

void SkyboxNode::spawnShootingStar() {
    std::uniform_real_distribution<float> u(0.f, 1.f);
    float W = m_size.width, H = m_size.height;
    CCPoint start{(0.3f + u(m_rng) * 0.8f) * W, (0.62f + u(m_rng) * 0.38f) * H};
    CCPoint dir{-0.87f, -0.5f};
    float rot = -CC_RADIANS_TO_DEGREES(std::atan2(dir.y, dir.x));
    float dist = 220.f + u(m_rng) * 120.f;
    float dur = 0.8f + u(m_rng) * 0.4f;

    auto makeAction = [&](float peak) {
        return CCSequence::create(
            CCSpawn::create(
                CCMoveBy::create(dur, dir * dist),
                CCSequence::create(
                    CCFadeTo::create(dur * 0.15f, static_cast<GLubyte>(peak)), CCDelayTime::create(dur * 0.5f),
                    CCFadeOut::create(dur * 0.35f), nullptr
                ),
                nullptr
            ),
            CCRemoveSelf::create(), nullptr
        );
    };

    auto tail = CCSprite::createWithTexture(m_glowTex);
    additive(tail);
    setDiameter(tail, 70.f, 2.6f);
    tail->setRotation(rot);
    tail->setColor({235, 220, 255});
    tail->setOpacity(0);
    tail->setAnchorPoint({0.15f, 0.5f});
    tail->setPosition(start);
    tail->runAction(makeAction(255.f));
    m_shootingLayer->addChild(tail);

    auto head = CCSprite::createWithTexture(m_glowTex);
    additive(head);
    setDiameter(head, 7.f);
    head->setOpacity(0);
    head->setPosition(start);
    head->runAction(makeAction(255.f));
    m_shootingLayer->addChild(head);
}

void SkyboxNode::update(float dt) {
    m_time += dt;
    float W = m_size.width;

    for (auto& st : m_stars) {
        float x = std::fmod(st.bx * W - m_scroll * st.depth * 0.02f, W);
        if (x < 0.f) x += W;
        st.spr->setPosition({x, st.by});
        float tw = 0.55f + 0.45f * std::sin(m_time * st.speed + st.phase);
        st.spr->setOpacity(static_cast<GLubyte>(std::clamp(st.baseOpacity * tw, 0.f, 255.f)));
    }

    for (auto& c : m_clouds) {
        c.spr->setPosition({c.bx + std::sin(m_time * c.drift + c.phase) * 14.f, c.by + std::cos(m_time * c.drift * 0.7f + c.phase) * 6.f});
        float o = c.baseOpacity * (0.85f + 0.15f * std::sin(m_time * 0.5f + c.phase));
        c.spr->setOpacity(static_cast<GLubyte>(std::clamp(o, 0.f, 255.f)));
    }

    if (m_celestialGlow) {
        float pulse = 0.9f + 0.1f * std::sin(m_time * 1.3f);
        m_celestialGlow->setOpacity(static_cast<GLubyte>(std::clamp(m_glowBase * pulse, 0.f, 255.f)));
    }

    if (cfg().shootingStars) {
        m_nextShooting -= dt;
        if (m_nextShooting <= 0.f) {
            std::uniform_real_distribution<float> u(0.f, 1.f);
            spawnShootingStar();
            m_nextShooting = 3.f + u(m_rng) * 6.f;
        }
    }
}

} // namespace starlight
