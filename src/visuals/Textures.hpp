#pragma once

#include <Geode/Geode.hpp>

#include <vector>

// Procedurally generated textures, so the mod needs no image resources.
// Each call returns a fresh autoreleased texture (keep it alive by using it in a sprite).
namespace starlight::tex {

// soft radial glow, white with alpha falloff
cocos2d::CCTexture2D* glow(int size = 64);

// solid disc with soft edge; moon=true adds limb darkening + craters
cocos2d::CCTexture2D* disc(int size, bool moon);

// realistic full moon: maria, craters with rims, ray craters, limb darkening
cocos2d::CCTexture2D* moon(int size = 256);

// vertical gradient, stops[0] = top, stops.back() = bottom
cocos2d::CCTexture2D* gradient(std::vector<cocos2d::ccColor3B> const& stops, int height = 256);

} // namespace starlight::tex
