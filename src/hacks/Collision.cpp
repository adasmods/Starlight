// Collision tweaks: hitbox multiplier, all passable, legacy level physics flags.

#include "Config.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using namespace starlight;

class $modify(StarlightHitbox, GameObject) {
    CCRect const& getObjectRect() {
        auto const& rect = GameObject::getObjectRect();
        auto& c = cfg();
        if (!c.hitboxMultiplier) return rect;
        auto pl = PlayLayer::get();
        if (!pl) return rect;

        float mult;
        if (this == static_cast<GameObject*>(pl->m_player1) || this == static_cast<GameObject*>(pl->m_player2)) {
            mult = c.hitboxPlayer;
        } else {
            switch (m_objectType) {
                case GameObjectType::Hazard:
                case GameObjectType::AnimatedHazard: mult = c.hitboxHazard; break;
                case GameObjectType::Solid:
                case GameObjectType::Slope:
                case GameObjectType::Breakable: mult = c.hitboxSolid; break;
                default: return rect;
            }
        }
        if (mult == 1.f) return rect;

        // callers read the rect right away; a small ring buffer keeps references valid
        static thread_local CCRect ring[32];
        static thread_local unsigned index = 0;
        auto& out = ring[index++ & 31];
        float w = rect.size.width * mult, h = rect.size.height * mult;
        out = CCRect(rect.getMidX() - w * 0.5f, rect.getMidY() - h * 0.5f, w, h);
        return out;
    }
};

class $modify(StarlightCollisionLayer, PlayLayer) {
    void addObject(GameObject* object) {
        PlayLayer::addObject(object);
        if (!cfg().allPassable || !object) return;
        switch (object->m_objectType) {
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Breakable: object->m_isPassable = true; break;
            default: break;
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& c = cfg();
        if (m_levelSettings) {
            // turning a "fix" flag off brings back the old (legacy) behaviour
            if (c.legacyGravityBug) m_levelSettings->m_fixGravityBug = false;
            if (c.legacyNegativeScale) m_levelSettings->m_fixNegativeScale = false;
            if (c.legacyRadiusCollision) m_levelSettings->m_fixRadiusCollision = false;
        }
        return true;
    }
};
