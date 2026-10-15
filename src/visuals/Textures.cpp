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
