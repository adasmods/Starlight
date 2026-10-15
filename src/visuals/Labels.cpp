#include "Labels.hpp"

#include "State.hpp"
#include "Textures.hpp"
#include "ui/Menu.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

using namespace geode::prelude;

namespace starlight {

// ============================================================================
//  Fonts
// ============================================================================

namespace {

struct FontList {
    std::vector<std::string> names;
    std::vector<std::string> files;
    std::vector<char const*> namePtrs;

    FontList() {
        add("Starlight Theme", "chatFont.fnt");
        add("Big Font", "bigFont.fnt");
        add("Chat Font", "chatFont.fnt");
        add("Gold Font", "goldFont.fnt");
        for (int i = 1; i <= 59; i++) add(fmt::format("GJ Font {:02}", i), fmt::format("gjFont{:02}.fnt", i));
        for (auto const& n : names) namePtrs.push_back(n.c_str());
    }
    void add(std::string name, std::string file) {
        names.push_back(std::move(name));
        files.push_back(std::move(file));
    }
};

FontList const& fonts() {
    static FontList const list;
    return list;
}

// rough per-font size correction so every font looks similar at scale 1
float fontBaseScale(int font) {
    switch (font) {
        case 0: return 0.62f; // Starlight Theme (chat font)
        case 1: return 0.40f; // big font
        case 2: return 0.60f; // chat font
        case 3: return 0.45f; // gold font
        default: return 0.45f;
    }
}

constexpr ccColor3B kWhite{255, 255, 255};
constexpr ccColor3B kRed{255, 70, 100};
constexpr ccColor3B kGreen{90, 255, 150};

bool isWhite(ccColor3B c) { return c.r == 255 && c.g == 255 && c.b == 255; }

ccColor3B lerpC(ccColor3B a, ccColor3B b, float t) {
    return {static_cast<GLubyte>(a.r + (b.r - a.r) * t), static_cast<GLubyte>(a.g + (b.g - a.g) * t),
            static_cast<GLubyte>(a.b + (b.b - a.b) * t)};
}

GLubyte toByte(float v) { return static_cast<GLubyte>(std::clamp(v, 0.f, 255.f)); }

LabelsNode* s_current = nullptr;

} // namespace

namespace labels {

int fontCount() { return static_cast<int>(fonts().files.size()); }
char const* const* fontNames() { return fonts().namePtrs.data(); }
char const* fontFile(int index) {
    auto const& f = fonts().files;
    return f[std::clamp(index, 0, static_cast<int>(f.size()) - 1)].c_str();
}

char const* labelName(int id) {
    static char const* names[kLabelCount] = {"Cheat Indicator", "FPS",     "CPS",          "Noclip Accuracy", "Noclip Deaths",
                                             "Attempt",         "Percent", "Session Time", "Clock"};
    return names[std::clamp(id, 0, kLabelCount - 1)];
}

char const* sampleText(int id) {
    static char const* samples[kLabelCount] = {".", "240 FPS", "8 CPS", "100.00%", "0 Deaths", "Attempt 1", "42.00%", "1:23", "21:37:00"};
    return samples[std::clamp(id, 0, kLabelCount - 1)];
}

void tick(float dt) {
    auto node = LabelsNode::current();
    auto pl = PlayLayer::get();
    if (node && pl && node->getParent() == pl) node->refresh(dt, pl);
}

} // namespace labels

// ============================================================================
//  LabelsNode
// ============================================================================

LabelsNode* LabelsNode::create() {
    auto ret = new LabelsNode();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

LabelsNode* LabelsNode::current() { return s_current; }

LabelsNode::~LabelsNode() {
    if (s_current == this) s_current = nullptr;
    if (m_glowTex) m_glowTex->release();
}

bool LabelsNode::init() {
    if (!CCNode::init()) return false;
    m_glowTex = tex::glow(64);
    m_glowTex->retain();

    for (int i = 0; i < kLabelCount; i++) {
        auto& e = m_entries[i];
        e.root = CCNode::create();
        this->addChild(e.root);

        e.glow = CCSprite::createWithTexture(m_glowTex);
        e.glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        e.root->addChild(e.glow, 0);

        e.sparkle = CCSprite::createWithTexture(m_glowTex);
        e.sparkle->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        e.sparkle->setColor({235, 220, 255});
        e.root->addChild(e.sparkle, 2);

        e.phase = i * 0.9f;
    }
    s_current = this;
    return true;
}

bool LabelsNode::getWorldBox(int id, CCRect& out) const {
    if (id < 0 || id >= kLabelCount || !m_entries[id].hasBox) return false;
    out = m_entries[id].box;
    return true;
}

void LabelsNode::refresh(float dt, PlayLayer* pl) {
    m_time += dt;
    auto& c = cfg();
    auto& st = state();
    bool editing = menu::isEditingLabels();
    auto win = CCDirector::get()->getWinSize();

    // ---- texts
    std::array<std::string, kLabelCount> texts;
    std::array<ccColor3B, kLabelCount> colors;
    colors.fill(kWhite);

    texts[LabelCheat] = ".";
    colors[LabelCheat] = (st.cheatedAttempt || cheatsActive()) ? kRed : kGreen;
    texts[LabelFps] = fmt::format("{:.0f} FPS", st.fps);
    {
        double now = nowSeconds();
        auto& clicks = st.clicks;
        while (!clicks.empty() && now - clicks.front() > 1.0) clicks.pop_front();
        texts[LabelCps] = fmt::format("{} CPS", clicks.size());
    }
    texts[LabelNoclipAcc] = fmt::format("{:.2f}%", st.noclipAccuracy());
    if (st.noclipWasHit) colors[LabelNoclipAcc] = kRed;
    texts[LabelNoclipDeaths] = fmt::format("{} Deaths", st.noclipDeaths);
    texts[LabelAttempt] = fmt::format("Attempt {}", pl->m_attempts);
    texts[LabelPercent] = fmt::format("{:.2f}%", pl->getCurrentPercent());
    {
        int s = static_cast<int>(st.sessionTime);
        texts[LabelTime] = fmt::format("{}:{:02}", s / 60, s % 60);
    }
    {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        texts[LabelClock] = fmt::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    // ---- layout + style
    float opacity = std::clamp(c.labelOpacity, 0.f, 1.f);
    for (int i = 0; i < kLabelCount; i++) {
        auto& L = c.lbl[i];
        auto& e = m_entries[i];
        bool show = L.enabled && (c.labels || editing);
        e.root->setVisible(show);
        e.hasBox = false;
        if (!show) continue;

        int font = std::clamp(L.font >= 0 ? L.font : c.labelFont, 0, labels::fontCount() - 1);
        auto const& text = texts[i];
        if (font != e.font || !e.label) {
            if (e.label) e.label->removeFromParent();
            e.label = CCLabelBMFont::create(text.c_str(), labels::fontFile(font));
            if (!e.label) e.label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            e.root->addChild(e.label, 1);
            e.font = font;
            e.text = text;
        } else if (e.text != text) {
            e.label->setString(text.c_str());
            e.text = text;
        }

        bool theme = font == 0;
        float ax = L.align == 0 ? 0.f : L.align == 1 ? 0.5f : 1.f;
        float scale = c.labelScale * L.scale * fontBaseScale(font) * (i == LabelCheat ? 1.8f : 1.f);
        e.label->setAnchorPoint({ax, 0.5f});
        e.label->setScale(scale);

        float bob = 0.f, breathe = 1.f;
        ccColor3B col = colors[i];
        if (theme) {
            // calm floating + slow purple shimmer
            bob = std::sin(m_time * 1.1f + e.phase) * 1.6f;
            breathe = 0.82f + 0.18f * std::sin(m_time * 0.9f + e.phase * 1.3f);
            if (isWhite(col)) col = lerpC({214, 190, 255}, {168, 120, 255}, 0.5f + 0.5f * std::sin(m_time * 0.7f + e.phase));
        }

        e.root->setPosition({L.x * win.width, L.y * win.height + bob});
        e.label->setColor(col);
        e.label->setOpacity(toByte(opacity * 255.f * breathe));

        float w = e.label->getContentSize().width * scale;
        float h = e.label->getContentSize().height * scale;
        float cx = (0.5f - ax) * w;

        e.glow->setVisible(theme);
        e.sparkle->setVisible(theme);
        if (theme) {
            auto gs = e.glow->getContentSize();
            e.glow->setPosition({cx, 0.f});
            e.glow->setScaleX((w * 1.35f + 18.f) / gs.width);
            e.glow->setScaleY((h * 1.9f + 8.f) / gs.height);
            e.glow->setColor(i == LabelCheat ? col : ccColor3B{150, 90, 255});
            e.glow->setOpacity(toByte(opacity * 75.f * breathe));

            float tw = std::pow(std::max(0.f, std::sin(m_time * 1.6f + e.phase * 2.f)), 6.f);
            e.sparkle->setPosition({cx + w * 0.5f + 3.f, h * 0.35f});
            e.sparkle->setScale((5.f + 3.f * tw) / e.sparkle->getContentSize().width);
            e.sparkle->setOpacity(toByte(opacity * 255.f * tw));
        }

        // screen-space box for the label editor
        CCRect bb = e.label->boundingBox();
        CCPoint a = e.root->convertToWorldSpace(bb.origin);
        CCPoint b = e.root->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});
        e.box = CCRect(a.x, a.y, b.x - a.x, b.y - a.y);
        e.hasBox = true;
    }
}

} // namespace starlight
