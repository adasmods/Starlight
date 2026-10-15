#pragma once

#include "Config.hpp"

#include <Geode/Geode.hpp>

#include <array>
#include <string>

namespace starlight {

namespace labels {

// Font list: 0 = "Starlight Theme", then every GD bitmap font.
int fontCount();
char const* const* fontNames();
char const* fontFile(int index);

char const* labelName(int id);
char const* sampleText(int id);

// Called every frame (even while paused) to refresh the labels of the current level.
void tick(float dt);

} // namespace labels

// Holds every label of a level. One instance per PlayLayer.
class LabelsNode : public cocos2d::CCNode {
public:
    static LabelsNode* create();
    static LabelsNode* current();
    ~LabelsNode() override;

    void refresh(float dt, PlayLayer* pl);

    // label rectangle in screen (world) space, for the label editor
    bool getWorldBox(int id, cocos2d::CCRect& out) const;

protected:
    bool init() override;

private:
    struct Entry {
        cocos2d::CCNode* root = nullptr;
        // exactly one of these text nodes exists at a time
        cocos2d::CCLabelBMFont* bm = nullptr; // GD bitmap fonts
        cocos2d::CCLabelTTF* ttf = nullptr;   // Starlight Theme (smooth system font)
        cocos2d::CCLabelTTF* halo = nullptr;  // Starlight Theme glow copy
        cocos2d::CCSprite* glow = nullptr;
        cocos2d::CCSprite* sparkle = nullptr;
        cocos2d::CCSprite* dot = nullptr;     // cheat indicator circle
        cocos2d::CCSprite* dotGlow = nullptr;
        int font = -1;
        std::string text;
        float phase = 0.f;
        bool hasBox = false;
        cocos2d::CCRect box;
    };

    void ensureText(Entry& e, int font, std::string const& text);

    std::array<Entry, kLabelCount> m_entries;
    cocos2d::CCTexture2D* m_glowTex = nullptr;
    cocos2d::CCTexture2D* m_discTex = nullptr;
    float m_time = 0.f;
    float m_fpsTimer = 0.f;
    std::string m_fpsText = "0 FPS";
};

} // namespace starlight
