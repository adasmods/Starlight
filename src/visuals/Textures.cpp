#include "Textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace cocos2d;

namespace starlight::tex {

static CCTexture2D* make(std::vector<uint8_t> const& px, int w, int h) {
    auto t = new CCTexture2D();
    t->initWithData(px.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(static_cast<float>(w), static_cast<float>(h)));
    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    t->setTexParameters(&params);
    t->autorelease();
    return t;
}

static float smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

CCTexture2D* glow(int size) {
    std::vector<uint8_t> px(size * size * 4);
    float half = size * 0.5f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
            float d = std::min(std::sqrt(dx * dx + dy * dy), 1.f);
            float a = std::pow(1.f - d, 2.2f);
            uint8_t* p = &px[(y * size + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = static_cast<uint8_t>(a * 255.f);
        }
    }
    return make(px, size, size);
}

CCTexture2D* ring(int size) {
    std::vector<uint8_t> px(size * size * 4);
    float half = size * 0.5f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
            float d = std::sqrt(dx * dx + dy * dy);
            float a = std::exp(-std::pow((d - 0.82f) / 0.09f, 2.f));
            uint8_t* p = &px[(y * size + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = static_cast<uint8_t>(std::clamp(a, 0.f, 1.f) * 255.f);
        }
    }
    return make(px, size, size);
}

CCTexture2D* disc(int size, bool moon) {
    struct Crater {
        float x, y, r;
    };
    Crater craters[] = {{-0.3f, 0.25f, 0.22f}, {0.25f, -0.15f, 0.16f}, {0.1f, 0.45f, 0.1f},
                        {-0.15f, -0.45f, 0.13f}, {0.45f, 0.3f, 0.09f}, {-0.5f, -0.1f, 0.08f}};
    std::vector<uint8_t> px(size * size * 4);
    float half = size * 0.5f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
            float d = std::sqrt(dx * dx + dy * dy);
            float a = 1.f - smoothstep(0.94f, 1.f, d);
            float b = 1.f;
            if (moon) {
                b = 1.f - 0.28f * d * d;
                for (auto const& c : craters) {
                    float cd = std::sqrt((dx - c.x) * (dx - c.x) + (dy - c.y) * (dy - c.y));
                    if (cd < c.r) b -= 0.13f * (1.f - smoothstep(c.r * 0.6f, c.r, cd));
                }
            }
            uint8_t v = static_cast<uint8_t>(std::clamp(b, 0.f, 1.f) * 255.f);
            uint8_t* p = &px[(y * size + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = static_cast<uint8_t>(a * 255.f);
        }
    }
    return make(px, size, size);
}

// ---- value noise for the moon surface
static float hash2(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h ^ (h >> 16)) / 4294967295.f;
}

static float valueNoise(float x, float y) {
    int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    float tx = x - xi, ty = y - yi;
    tx = tx * tx * (3.f - 2.f * tx);
    ty = ty * ty * (3.f - 2.f * ty);
    float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

static float fbm(float x, float y, int octaves) {
    float sum = 0.f, amp = 0.5f, freq = 1.f;
    for (int i = 0; i < octaves; i++) {
        sum += valueNoise(x * freq, y * freq) * amp;
        freq *= 2.03f;
        amp *= 0.5f;
    }
    return sum;
}

CCTexture2D* moon(int size) {
    // dark "seas", roughly where the real ones are (x right, y down, unit disc).
    // They are summed as soft gaussians so they merge into irregular shapes.
    struct Mare {
        float x, y, rx, ry, weight;
    };
    Mare const maria[] = {
        {-0.30f, -0.30f, 0.34f, 0.28f, 1.00f}, // Imbrium
        {0.10f, -0.30f, 0.19f, 0.17f, 0.95f},  // Serenitatis
        {0.22f, -0.02f, 0.25f, 0.20f, 1.00f},  // Tranquillitatis
        {0.64f, -0.18f, 0.13f, 0.11f, 0.95f},  // Crisium
        {-0.60f, 0.05f, 0.30f, 0.50f, 0.90f},  // Procellarum
        {-0.40f, 0.32f, 0.22f, 0.16f, 0.70f},  // Humorum / Cognitum
        {0.44f, 0.24f, 0.16f, 0.14f, 0.80f},   // Fecunditatis
        {-0.15f, 0.30f, 0.18f, 0.14f, 0.75f},  // Nubium
        {0.24f, 0.38f, 0.12f, 0.10f, 0.65f},   // Nectaris
        {-0.05f, -0.62f, 0.34f, 0.09f, 0.60f}, // Frigoris
        {0.00f, 0.05f, 0.14f, 0.12f, 0.50f},   // Sinus Medii
    };

    struct Crater {
        float x, y, r, depth;
    };
    std::vector<Crater> craters;
    for (int i = 0; i < 140; i++) {
        float a = hash2(i, 7) * 6.2831f, rr = std::sqrt(hash2(i, 13)) * 0.96f;
        float r = 0.006f + std::pow(hash2(i, 29), 4.f) * 0.05f;
        craters.push_back({std::cos(a) * rr, std::sin(a) * rr, r, 0.5f + hash2(i, 31) * 0.5f});
    }

    std::vector<uint8_t> px(size * size * 4);
    float half = size * 0.5f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t* p = &px[(y * size + x) * 4];
            if (d > 1.f) {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }

            // highlands: bright with fine mottling
            float b = 0.84f + (fbm(dx * 7.f + 11.f, dy * 7.f + 3.f, 6) - 0.5f) * 0.12f +
                      (fbm(dx * 28.f + 5.f, dy * 28.f + 1.f, 3) - 0.5f) * 0.05f;

            // maria: merged gaussian field + warp -> irregular connected seas
            float wx = dx + (fbm(dx * 2.5f + 40.f, dy * 2.5f + 9.f, 4) - 0.5f) * 0.25f;
            float wy = dy + (fbm(dx * 2.5f + 13.f, dy * 2.5f + 77.f, 4) - 0.5f) * 0.25f;
            float field = 0.f;
            for (auto const& m : maria) {
                float mx = (wx - m.x) / m.rx, my = (wy - m.y) / m.ry;
                field += m.weight * std::exp(-(mx * mx + my * my) * 1.6f);
            }
            field += (fbm(dx * 5.f + 3.f, dy * 5.f + 8.f, 5) - 0.5f) * 0.45f;
            float mare = std::pow(smoothstep(0.12f, 0.85f, field), 0.85f) * 0.95f;
            float mareTex = (fbm(dx * 10.f + 70.f, dy * 10.f + 21.f, 4) - 0.5f) * 0.10f +
                            (fbm(dx * 3.f + 1.f, dy * 3.f + 2.f, 3) - 0.5f) * 0.08f;
            b = b * (1.f - mare) + (0.56f + mareTex) * mare;

            // small, low-contrast craters
            for (auto const& c : craters) {
                float cx = dx - c.x, cy = dy - c.y;
                float cd = std::sqrt(cx * cx + cy * cy) / c.r;
                if (cd > 1.4f) continue;
                float vis = c.depth * (1.f - mare * 0.6f);
                if (cd < 0.9f) b -= 0.035f * vis * (1.f - cd);
                float rim = std::exp(-std::pow((cd - 1.f) * 4.f, 2.f));
                float lit = -(cx * 0.7f + cy * 0.7f) / (c.r * cd + 1e-4f);
                b += rim * (0.02f + 0.03f * lit) * vis;
            }

            // soft ray systems (Tycho, Copernicus, Kepler)
            auto rays = [&](float px0, float py0, float strength, float reach) {
                float rx = dx - px0, ry = dy - py0;
                float rd = std::sqrt(rx * rx + ry * ry);
                float ang = std::atan2(ry, rx);
                float streak = smoothstep(0.55f, 0.85f, fbm(ang * 3.2f + 50.f, rd * 2.f + 0.5f, 3));
                float fall = std::pow(std::max(0.f, 1.f - rd / reach), 1.5f);
                b += strength * streak * fall;
                b += strength * 1.2f * std::exp(-(rd * rd) / (0.022f * 0.022f));
            };
            rays(-0.12f, 0.70f, 0.10f, 0.85f);
            rays(-0.30f, -0.05f, 0.06f, 0.35f);
            rays(-0.62f, -0.02f, 0.05f, 0.25f);

            // limb darkening + soft anti-aliased edge
            b *= 0.72f + 0.28f * std::sqrt(std::max(0.f, 1.f - d * d));
            b = std::clamp(b, 0.f, 1.f);
            float a = 1.f - smoothstep(0.985f, 1.f, d);

            p[0] = static_cast<uint8_t>(std::clamp(b * 1.00f, 0.f, 1.f) * 255.f);
            p[1] = static_cast<uint8_t>(std::clamp(b * 0.98f, 0.f, 1.f) * 255.f);
            p[2] = static_cast<uint8_t>(std::clamp(b * 0.94f, 0.f, 1.f) * 255.f);
            p[3] = static_cast<uint8_t>(a * 255.f);
        }
    }
    return make(px, size, size);
}
CCTexture2D* gradient(std::vector<ccColor3B> const& stops, int height) {
    constexpr int w = 2;
    std::vector<uint8_t> px(w * height * 4);
    int segs = static_cast<int>(stops.size()) - 1;
    for (int y = 0; y < height; y++) {
        float f = static_cast<float>(y) / (height - 1) * segs;
        int i = std::min(static_cast<int>(f), segs - 1);
        float t = f - i;
        auto const& a = stops[i];
        auto const& b = stops[i + 1];
        uint8_t r = static_cast<uint8_t>(a.r + (b.r - a.r) * t);
        uint8_t g = static_cast<uint8_t>(a.g + (b.g - a.g) * t);
        uint8_t bl = static_cast<uint8_t>(a.b + (b.b - a.b) * t);
        for (int x = 0; x < w; x++) {
            uint8_t* p = &px[(y * w + x) * 4];
            p[0] = r;
            p[1] = g;
            p[2] = bl;
            p[3] = 255;
        }
    }
    return make(px, w, height);
}

} // namespace starlight::tex
