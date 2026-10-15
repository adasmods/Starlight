#pragma once

#include <Geode/Geode.hpp>

#include <deque>
#include <random>
#include <vector>

namespace starlight {

// Smooth gradient ribbon behind the player (pink -> violet -> blue by default).
// Drawn as soft-edged triangle strips (glow, body, bright core) with per-vertex
// colors, plus little drifting sparkles. Lives in the object layer (world space).
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

    void draw() override;

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
        float life = 1.f, maxLife = 1.f, size = 4.f, phase = 0.f;
    };
    struct Strip {
        std::vector<cocos2d::ccVertex2F> verts;
        std::vector<cocos2d::ccTex2F> uvs;
        std::vector<cocos2d::ccColor4B> colors;
        void clear() {
            verts.clear();
            uvs.clear();
            colors.clear();
        }
        void push(cocos2d::CCPoint p, float v, cocos2d::ccColor4F c);
    };

    void buildStrips();

    cocos2d::CCTexture2D* m_glowTex = nullptr;
    std::deque<Point> m_points; // newest first
    std::vector<Sparkle> m_sparkles;
    Strip m_glow, m_body, m_core;
    float m_time = 0.f;
    float m_spawnAcc = 0.f;
    float m_maxAge = 0.45f;
    cocos2d::CCPoint m_last;
    std::mt19937 m_rng{99};
};

} // namespace starlight
