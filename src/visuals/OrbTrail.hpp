#pragma once

#include <Geode/Geode.hpp>

#include <deque>
#include <vector>

namespace starlight {

// Glowing orbs that chase the player with springy motion and leave little trails.
// Lives in the object layer (world space), driven by PlayLayer::postUpdate.
class OrbTrailNode : public cocos2d::CCNode {
public:
    static OrbTrailNode* create();
    ~OrbTrailNode() override {
        if (m_glowTex) m_glowTex->release();
    }

    // playerPos is in this node's parent space
    void step(float dt, cocos2d::CCPoint playerPos, bool showOrbs, bool showAura);
    void snap() { m_snap = true; }

protected:
    bool init() override;

private:
    struct Orb {
        cocos2d::CCSprite* core = nullptr;
        cocos2d::CCSprite* halo = nullptr;
        std::vector<cocos2d::CCSprite*> trail;
        std::deque<cocos2d::CCPoint> history;
        cocos2d::CCPoint pos;
    };

    void rebuild(int count, int trailLen);

    std::vector<Orb> m_orbs;
    cocos2d::CCSprite* m_aura = nullptr;
    cocos2d::CCSprite* m_auraCore = nullptr;
    cocos2d::CCTexture2D* m_glowTex = nullptr;
    int m_count = -1;
    int m_trailLen = -1;
    float m_time = 0.f;
    float m_dir = 1.f;
    cocos2d::CCPoint m_lastPlayer;
    bool m_snap = true;
};

} // namespace starlight
