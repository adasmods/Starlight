#pragma once

#include <Geode/Geode.hpp>

#include <deque>
#include <random>
#include <vector>

namespace starlight {

// Smooth gradient ribbon behind the player (pink -> violet -> blue by default)
// with a soft glow pass, a bright core and little drifting sparkles.
// Lives in the object layer (world space), driven by PlayLayer::postUpdate.
class CosmicTrailNode : public cocos2d::CCNode {
public:
    static CosmicTrailNode* create();
    ~CosmicTrailNode() override {
        if (m_glowTex) m_glowTex->release();
    }

    // playerPos is in this node's parent space
    void step(float dt, cocos2d::CCPoint playerPos, bool emit);
    void clearTrail() {
        m_points.clear();
    }

protected:
    bool init() override;

private:
    struct Point {
        cocos2d::CCPoint pos;
        float age;
    };
    struct Sparkle {
        cocos2d::CCSprite* spr;
        cocos2d::CCPoint vel;
        float life = 0.f, maxLife = 1.f, size = 4.f, phase = 0.f;
    };

    void drawRibbon();

    cocos2d::CCDrawNode* m_glowDraw = nullptr;
    cocos2d::CCDrawNode* m_coreDraw = nullptr;
    cocos2d::CCTexture2D* m_glowTex = nullptr;
    std::deque<Point> m_points; // newest first
    std::vector<Sparkle> m_sparkles;
    float m_time = 0.f;
    float m_spawnAcc = 0.f;
    cocos2d::CCPoint m_last;
    std::mt19937 m_rng{99};
};

} // namespace starlight
