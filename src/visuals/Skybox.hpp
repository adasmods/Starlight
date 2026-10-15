#pragma once

#include <Geode/Geode.hpp>

#include <random>
#include <vector>

namespace starlight {

// Procedural sky that replaces the level background.
// Presets: 1 starry night, 2 evening sunset, 3 deep nebula, 4 blood moon.
class SkyboxNode : public cocos2d::CCNode {
public:
    static SkyboxNode* create();
    ~SkyboxNode() override {
        if (m_glowTex) m_glowTex->release();
    }

    void setPreset(int preset, float density);
    void setScroll(float x) { m_scroll = x; }
    void update(float dt) override;

protected:
    bool init() override;

private:
    struct Star {
        cocos2d::CCSprite* spr;
        float bx, by, depth, phase, speed, baseOpacity;
    };
    struct Cloud {
        cocos2d::CCSprite* spr;
        float bx, by, drift, phase, baseOpacity;
    };

    void rebuild();
    void spawnShootingStar();

    int m_preset = -1;
    float m_density = -1.f;
    float m_time = 0.f;
    float m_scroll = 0.f;
    float m_nextShooting = 2.f;
    float m_glowBase = 150.f;
    cocos2d::CCSize m_size;
    std::mt19937 m_rng{7};

    cocos2d::CCTexture2D* m_glowTex = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_shootingLayer = nullptr;
    cocos2d::CCSprite* m_celestial = nullptr;
    cocos2d::CCSprite* m_celestialGlow = nullptr;
    std::vector<Star> m_stars;
    std::vector<Cloud> m_clouds;
};

} // namespace starlight
