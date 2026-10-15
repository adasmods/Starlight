#include "OrbTrail.hpp"

#include "Config.hpp"
#include "Textures.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace starlight {

namespace {

ccColor3B toC3(float const* c) {
    return {static_cast<GLubyte>(std::clamp(c[0], 0.f, 1.f) * 255.f), static_cast<GLubyte>(std::clamp(c[1], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(c[2], 0.f, 1.f) * 255.f)};
}

ccColor3B lerpC(ccColor3B a, ccColor3B b, float t) {
    return {static_cast<GLubyte>(a.r + (b.r - a.r) * t), static_cast<GLubyte>(a.g + (b.g - a.g) * t),
            static_cast<GLubyte>(a.b + (b.b - a.b) * t)};
}

ccColor3B hsv(float h, float s, float v) {
    float r = 0, g = 0, b = 0;
    float i = std::floor(h * 6.f);
    float f = h * 6.f - i;
    float p = v * (1.f - s), q = v * (1.f - f * s), t = v * (1.f - (1.f - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
    return {static_cast<GLubyte>(r * 255.f), static_cast<GLubyte>(g * 255.f), static_cast<GLubyte>(b * 255.f)};
}

void setDiameter(CCSprite* spr, float d) { spr->setScale(d / spr->getContentSize().width); }

} // namespace

OrbTrailNode* OrbTrailNode::create() {
    auto ret = new OrbTrailNode();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool OrbTrailNode::init() {
    if (!CCNode::init()) return false;
    m_glowTex = tex::glow(64);
    m_glowTex->retain();

    m_aura = CCSprite::createWithTexture(m_glowTex);
    m_aura->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    this->addChild(m_aura, -2);
    m_auraCore = CCSprite::createWithTexture(m_glowTex);
    m_auraCore->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    this->addChild(m_auraCore, -1);
    return true;
}

void OrbTrailNode::rebuild(int count, int trailLen) {
    for (auto& o : m_orbs) {
        for (auto t : o.trail) t->removeFromParent();
        if (o.halo) o.halo->removeFromParent();
        if (o.core) o.core->removeFromParent();
    }
    m_orbs.clear();
    m_orbs.resize(count);

    auto make = [&](int z) {
        auto spr = CCSprite::createWithTexture(m_glowTex);
        spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        this->addChild(spr, z);
        return spr;
    };
    for (auto& o : m_orbs) {
        for (int j = 0; j < trailLen; j++) o.trail.push_back(make(0));
        o.halo = make(1);
        o.core = make(2);
    }
    m_snap = true;
}

void OrbTrailNode::step(float dt, CCPoint playerPos, bool showOrbs, bool showAura) {
    auto& c = cfg();
    int count = std::clamp(c.orbCount, 1, 8);
    int trailLen = std::clamp(c.orbTrail, 0, 16);
    if (count != m_count || trailLen != m_trailLen) {
        m_count = count;
        m_trailLen = trailLen;
        rebuild(count, trailLen);
    }

    m_time += dt * c.orbSpeed;

    // detect respawns/teleports and the direction the player travels
    float dx = playerPos.x - m_lastPlayer.x;
    if (ccpDistance(playerPos, m_lastPlayer) > 250.f) m_snap = true;
    if (std::abs(dx) > 0.01f) m_dir += ((dx > 0.f ? 1.f : -1.f) - m_dir) * std::min(1.f, dt * 4.f);
    m_lastPlayer = playerPos;

    ccColor3B c1 = toC3(c.orbColor1), c2 = toC3(c.orbColor2);
    float size = c.orbSize;

    for (int i = 0; i < count; i++) {
        auto& o = m_orbs[i];
        float fi = static_cast<float>(i);
        float phase = m_time * (1.6f + fi * 0.23f) + fi * 6.2831f / count;
        CCPoint base = playerPos + ccp(-m_dir * (26.f + fi * 13.f), 4.f);
        CCPoint orbit = ccp(std::cos(phase) * 9.f, std::sin(phase * 1.3f) * 15.f);
        CCPoint target = base + orbit;

        if (m_snap) {
            o.pos = target;
            o.history.clear();
        } else {
            // later orbs are lazier -> nice stretched swarm
            float k = 1.f - std::exp(-dt * std::max(2.f, 8.f - fi * 0.7f));
            o.pos = o.pos + (target - o.pos) * k;
        }
        o.history.push_front(o.pos);
        while (o.history.size() > static_cast<size_t>(trailLen * 2 + 1)) o.history.pop_back();

        float t = count > 1 ? fi / (count - 1) : 0.f;
        ccColor3B col = c.orbRainbow ? hsv(std::fmod(m_time * 0.15f + fi / count, 1.f), 0.55f, 1.f) : lerpC(c1, c2, t);
        float pulse = 0.85f + 0.15f * std::sin(m_time * 4.f + fi);

        o.halo->setVisible(showOrbs);
        o.halo->setPosition(o.pos);
        o.halo->setColor(col);
        o.halo->setOpacity(static_cast<GLubyte>(150.f * pulse));
        setDiameter(o.halo, 24.f * size);

        o.core->setVisible(showOrbs);
        o.core->setPosition(o.pos);
        o.core->setColor(lerpC(col, {255, 255, 255}, 0.6f));
        o.core->setOpacity(255);
        setDiameter(o.core, 8.f * size * pulse);

        for (int j = 0; j < trailLen; j++) {
            auto spr = o.trail[j];
            size_t idx = static_cast<size_t>((j + 1) * 2);
            bool vis = showOrbs && idx < o.history.size();
            spr->setVisible(vis);
            if (!vis) continue;
            float f = 1.f - static_cast<float>(j + 1) / (trailLen + 1);
            spr->setPosition(o.history[idx]);
            spr->setColor(col);
            spr->setOpacity(static_cast<GLubyte>(160.f * f));
            setDiameter(spr, (10.f * f + 1.5f) * size);
        }
    }
    m_snap = false;

    ccColor3B auraCol = c.orbRainbow ? hsv(std::fmod(m_time * 0.15f, 1.f), 0.5f, 1.f) : c1;
    float breathe = 1.f + 0.06f * std::sin(m_time * 2.5f);
    m_aura->setVisible(showAura);
    m_auraCore->setVisible(showAura);
    m_aura->setPosition(playerPos);
    m_aura->setColor(auraCol);
    m_aura->setOpacity(90);
    setDiameter(m_aura, 70.f * breathe);
    m_auraCore->setPosition(playerPos);
    m_auraCore->setColor(lerpC(auraCol, {255, 255, 255}, 0.4f));
    m_auraCore->setOpacity(60);
    setDiameter(m_auraCore, 34.f * breathe);
}

} // namespace starlight
