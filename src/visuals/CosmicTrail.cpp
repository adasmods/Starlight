#include "CosmicTrail.hpp"

#include "Config.hpp"
#include "Textures.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace starlight {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kSparkleCount = 48;
constexpr size_t kMaxPoints = 220;

ccColor4F lerp4(float const* a, float const* b, float t, float alpha) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, alpha};
}

// f: 0 = at the player, 1 = end of the trail
ccColor4F colorAt(float f, float alpha) {
    auto& c = cfg();
    f = std::clamp(f, 0.f, 1.f);
    return f < 0.5f ? lerp4(c.trailColorA, c.trailColorB, f * 2.f, alpha) : lerp4(c.trailColorB, c.trailColorC, (f - 0.5f) * 2.f, alpha);
}

ccColor4F scaled(ccColor4F col, float k) {
    return {std::min(col.r * k, 1.f), std::min(col.g * k, 1.f), std::min(col.b * k, 1.f), col.a};
}

} // namespace

CosmicTrailNode* CosmicTrailNode::create() {
    auto ret = new CosmicTrailNode();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CosmicTrailNode::init() {
    if (!CCNode::init()) return false;
    m_glowTex = tex::glow(64);
    m_glowTex->retain();

    m_glowDraw = CCDrawNode::create();
    m_glowDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    this->addChild(m_glowDraw, 0);

    m_coreDraw = CCDrawNode::create();
    m_coreDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    this->addChild(m_coreDraw, 1);

    for (int i = 0; i < kSparkleCount; i++) {
        auto spr = CCSprite::createWithTexture(m_glowTex);
        spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        spr->setVisible(false);
        this->addChild(spr, 2);
        m_sparkles.push_back({spr});
    }
    return true;
}

void CosmicTrailNode::step(float dt, CCPoint playerPos, bool emit) {
    auto& c = cfg();
    m_time += dt;
    float maxAge = 0.32f * std::clamp(c.trailLength, 0.2f, 4.f);

    // respawn / teleport -> start fresh
    if (ccpDistance(playerPos, m_last) > 250.f) m_points.clear();
    m_last = playerPos;

    for (auto& p : m_points) p.age += dt;
    while (!m_points.empty() && m_points.back().age > maxAge) m_points.pop_back();

    if (emit) {
        if (m_points.empty() || ccpDistance(m_points.front().pos, playerPos) > 1.5f) {
            m_points.push_front({playerPos, 0.f});
        } else {
            m_points.front().pos = playerPos;
        }
        while (m_points.size() > kMaxPoints) m_points.pop_back();
    }

    // sparkles
    std::uniform_real_distribution<float> u(0.f, 1.f);
    if (emit && c.trailSparkles) {
        m_spawnAcc += dt * 26.f;
        while (m_spawnAcc >= 1.f) {
            m_spawnAcc -= 1.f;
            auto it = std::find_if(m_sparkles.begin(), m_sparkles.end(), [](Sparkle const& s) { return s.life >= s.maxLife; });
            if (it == m_sparkles.end()) break;
            float f = u(m_rng);
            ccColor4F col = colorAt(f, 1.f);
            it->spr->setColor({static_cast<GLubyte>(col.r * 255.f), static_cast<GLubyte>(col.g * 255.f), static_cast<GLubyte>(col.b * 255.f)});
            it->spr->setPosition(playerPos + ccp((u(m_rng) - 0.5f) * 12.f * c.trailWidth, (u(m_rng) - 0.5f) * 12.f * c.trailWidth));
            it->vel = ccp((u(m_rng) - 0.5f) * 40.f, (u(m_rng) - 0.5f) * 40.f + 6.f);
            it->life = 0.f;
            it->maxLife = 0.5f + u(m_rng) * 0.7f;
            it->size = 3.f + u(m_rng) * 4.f;
            it->phase = u(m_rng) * 6.28f;
            it->spr->setVisible(true);
        }
    } else {
        m_spawnAcc = 0.f;
    }

    for (auto& s : m_sparkles) {
        if (s.life >= s.maxLife) {
            s.spr->setVisible(false);
            continue;
        }
        s.life += dt;
        s.spr->setPosition(s.spr->getPosition() + s.vel * dt);
        s.vel = s.vel * std::max(0.f, 1.f - dt * 1.5f);
        float k = std::clamp(s.life / s.maxLife, 0.f, 1.f);
        float twinkle = 0.6f + 0.4f * std::sin(m_time * 14.f + s.phase);
        s.spr->setOpacity(static_cast<GLubyte>(std::sin(k * kPi) * twinkle * 230.f));
        s.spr->setScale(s.size * (1.f - k * 0.6f) / s.spr->getContentSize().width);
    }

    // ribbon
    m_glowDraw->clear();
    m_coreDraw->clear();
    size_t n = m_points.size();
    if (n < 2) return;

    std::vector<CCPoint> normals(n);
    for (size_t i = 0; i < n; i++) {
        CCPoint d = i == 0 ? m_points[0].pos - m_points[1].pos
                  : i == n - 1 ? m_points[n - 2].pos - m_points[n - 1].pos
                               : m_points[i - 1].pos - m_points[i + 1].pos;
        float len = ccpLength(d);
        normals[i] = len > 0.001f ? ccp(-d.y / len, d.x / len) : ccp(0.f, 1.f);
    }

    float baseW = 7.f * c.trailWidth;
    ccColor4F none{0.f, 0.f, 0.f, 0.f};
    auto quad = [&](CCDrawNode* node, size_t i, float w0, float w1, ccColor4F col) {
        CCPoint const& p0 = m_points[i].pos;
        CCPoint const& p1 = m_points[i + 1].pos;
        CCPoint v[4] = {p0 + normals[i] * w0, p1 + normals[i + 1] * w1, p1 - normals[i + 1] * w1, p0 - normals[i] * w0};
        node->drawPolygon(v, 4, col, 0.f, none);
    };

    for (size_t i = 0; i + 1 < n; i++) {
        float f0 = std::clamp(m_points[i].age / maxAge, 0.f, 1.f);
        float f1 = std::clamp(m_points[i + 1].age / maxAge, 0.f, 1.f);
        float fm = (f0 + f1) * 0.5f;
        float w0 = baseW * std::pow(1.f - f0, 0.8f);
        float w1 = baseW * std::pow(1.f - f1, 0.8f);
        float alpha = std::pow(1.f - fm, 1.3f);

        ccColor4F col = colorAt(fm, alpha);
        if (c.trailAnimate) {
            // a soft pulse of light travelling down the trail
            float pulse = std::max(0.f, std::sin(fm * 10.f - m_time * 6.f));
            col = scaled(col, 1.f + 0.4f * pulse * pulse);
        }

        quad(m_glowDraw, i, w0 * 2.4f, w1 * 2.4f, {col.r, col.g, col.b, alpha * 0.22f});
        quad(m_coreDraw, i, w0, w1, {col.r, col.g, col.b, alpha * 0.85f});
        quad(m_coreDraw, i, w0 * 0.3f, w1 * 0.3f, {1.f, 1.f, 1.f, alpha * 0.55f});
    }
}

} // namespace starlight
