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
constexpr size_t kMaxPoints = 400;

ccColor4F lerp4(float const* a, float const* b, float t, float alpha) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, alpha};
}

// f: 0 = at the player, 1 = end of the trail
ccColor4F colorAt(float f, float alpha) {
    auto& c = cfg();
    f = std::clamp(f, 0.f, 1.f);
    return f < 0.5f ? lerp4(c.trailColorA, c.trailColorB, f * 2.f, alpha) : lerp4(c.trailColorB, c.trailColorC, (f - 0.5f) * 2.f, alpha);
}

} // namespace

void CosmicTrailNode::Strip::push(CCPoint p, float v, ccColor4F c) {
    verts.push_back({p.x, p.y});
    uvs.push_back({0.5f, v}); // vertical slice through the radial glow = soft edge profile
    colors.push_back({static_cast<GLubyte>(std::clamp(c.r, 0.f, 1.f) * 255.f), static_cast<GLubyte>(std::clamp(c.g, 0.f, 1.f) * 255.f),
                      static_cast<GLubyte>(std::clamp(c.b, 0.f, 1.f) * 255.f), static_cast<GLubyte>(std::clamp(c.a, 0.f, 1.f) * 255.f)});
}

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
    this->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));

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
    m_maxAge = 0.45f * std::clamp(c.trailLength, 0.2f, 4.f);

    // respawn / teleport -> start fresh
    if (ccpDistance(playerPos, m_last) > 250.f) m_points.clear();
    m_last = playerPos;

    for (auto& p : m_points) p.age += dt;
    while (!m_points.empty() && m_points.back().age > m_maxAge) m_points.pop_back();

    if (emit) {
        if (m_points.empty() || ccpDistance(m_points.front().pos, playerPos) > 1.f) {
            m_points.push_front({playerPos, 0.f});
        } else {
            m_points.front().pos = playerPos;
            m_points.front().age = 0.f;
        }
        while (m_points.size() > kMaxPoints) m_points.pop_back();
    }

    // sparkles
    std::uniform_real_distribution<float> u(0.f, 1.f);
    if (emit && c.trailSparkles && m_points.size() > 1) {
        m_spawnAcc += dt * 26.f;
        while (m_spawnAcc >= 1.f) {
            m_spawnAcc -= 1.f;
            auto it = std::find_if(m_sparkles.begin(), m_sparkles.end(), [](Sparkle const& s) { return s.life >= s.maxLife; });
            if (it == m_sparkles.end()) break;
            // spawn somewhere along the first part of the trail
            size_t idx = static_cast<size_t>(u(m_rng) * std::min<size_t>(m_points.size() - 1, 40));
            float f = m_points[idx].age / m_maxAge;
            ccColor4F col = colorAt(f, 1.f);
            it->spr->setColor({static_cast<GLubyte>(col.r * 255.f), static_cast<GLubyte>(col.g * 255.f), static_cast<GLubyte>(col.b * 255.f)});
            it->spr->setPosition(m_points[idx].pos + ccp((u(m_rng) - 0.5f) * 10.f * c.trailWidth, (u(m_rng) - 0.5f) * 10.f * c.trailWidth));
            it->vel = ccp((u(m_rng) - 0.5f) * 30.f, (u(m_rng) - 0.5f) * 30.f + 8.f);
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

    buildStrips();
}

void CosmicTrailNode::buildStrips() {
    m_glow.clear();
    m_body.clear();
    m_core.clear();
    size_t n = m_points.size();
    if (n < 2) return;
    auto& c = cfg();

    // smooth the path (two passes of a 3-tap average, endpoints fixed)
    std::vector<CCPoint> pts(n);
    for (size_t i = 0; i < n; i++) pts[i] = m_points[i].pos;
    for (int pass = 0; pass < 2; pass++) {
        std::vector<CCPoint> tmp = pts;
        for (size_t i = 1; i + 1 < n; i++) tmp[i] = (pts[i - 1] + pts[i] * 2.f + pts[i + 1]) * 0.25f;
        pts.swap(tmp);
    }

    // consistent normals (never flip between neighbours -> no twisting)
    std::vector<CCPoint> normals(n);
    for (size_t i = 0; i < n; i++) {
        CCPoint d = i == 0 ? pts[0] - pts[1] : i == n - 1 ? pts[n - 2] - pts[n - 1] : pts[i - 1] - pts[i + 1];
        float len = ccpLength(d);
        CCPoint nrm = len > 0.0001f ? ccp(-d.y / len, d.x / len) : (i > 0 ? normals[i - 1] : ccp(0.f, 1.f));
        if (i > 0 && ccpDot(nrm, normals[i - 1]) < 0.f) nrm = nrm * -1.f;
        normals[i] = nrm;
    }

    float baseW = 7.f * c.trailWidth;
    for (size_t i = 0; i < n; i++) {
        float f = std::clamp(m_points[i].age / m_maxAge, 0.f, 1.f);
        float taper = std::pow(1.f - f, 0.6f);
        float alpha = std::pow(1.f - f, 1.2f);
        // round-ish head so it doesn't start with a hard edge
        float head = std::min(1.f, 0.35f + static_cast<float>(i) * 0.22f);
        float w = baseW * taper * head;

        ccColor4F col = colorAt(f, alpha);
        if (c.trailAnimate) {
            // a soft pulse of light travelling down the trail
            float pulse = std::max(0.f, std::sin(f * 9.f - m_time * 5.f));
            float k = 1.f + 0.35f * pulse * pulse;
            col.r = std::min(col.r * k, 1.f);
            col.g = std::min(col.g * k, 1.f);
            col.b = std::min(col.b * k, 1.f);
        }

        CCPoint p = pts[i], nrm = normals[i];
        float gw = w * 3.2f, bw = w * 1.6f, cw = w * 0.55f;
        m_glow.push(p + nrm * gw, 0.f, {col.r, col.g, col.b, alpha * 0.35f});
        m_glow.push(p - nrm * gw, 1.f, {col.r, col.g, col.b, alpha * 0.35f});
        m_body.push(p + nrm * bw, 0.f, {col.r, col.g, col.b, alpha * 0.95f});
        m_body.push(p - nrm * bw, 1.f, {col.r, col.g, col.b, alpha * 0.95f});
        ccColor4F hot{0.5f + col.r * 0.5f, 0.5f + col.g * 0.5f, 0.5f + col.b * 0.5f, alpha * 0.8f};
        m_core.push(p + nrm * cw, 0.f, hot);
        m_core.push(p - nrm * cw, 1.f, hot);
    }
}

void CosmicTrailNode::draw() {
    if (m_body.verts.size() < 4 || !m_glowTex) return;

    auto prog = this->getShaderProgram();
    if (!prog) return;
    prog->use();
    prog->setUniformsForBuiltins();

    ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);
    ccGLBlendFunc(GL_SRC_ALPHA, GL_ONE);
    ccGLBindTexture2D(m_glowTex->getName());

    for (Strip* s : {&m_glow, &m_body, &m_core}) {
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, s->verts.data());
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, s->uvs.data());
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, s->colors.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(s->verts.size()));
    }
}

} // namespace starlight
