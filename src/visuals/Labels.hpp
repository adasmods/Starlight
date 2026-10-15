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
        cocos2d::CCLabelBMFont* label = nullptr;
        cocos2d::CCSprite* glow = nullptr;
        cocos2d::CCSprite* sparkle = nullptr;
        int font = -1;
        std::string text;
        float phase = 0.f;
        bool hasBox = false;
        cocos2d::CCRect box;
    };

    std::array<Entry, kLabelCount> m_entries;
    cocos2d::CCTexture2D* m_glowTex = nullptr;
    float m_time = 0.f;
};

} // namespace starlight
