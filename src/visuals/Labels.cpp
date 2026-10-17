#include "Labels.hpp"

#include "State.hpp"
#include "Textures.hpp"
#include "hacks/Extras.hpp"
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

// Smooth system font used by the Starlight Theme (rendered with CCLabelTTF).
constexpr char const* kThemeFont = "Candara";
constexpr float kThemeFontSize = 40.f; // render size; scaled down for crisp text

struct FontList {
    std::vector<std::string> names;
    std::vector<std::string> files;
    std::vector<char const*> namePtrs;

    FontList() {
        add("Starlight Theme", "");
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
        case 0: return 0.36f; // Starlight Theme (TTF rendered at kThemeFontSize)
        case 1: return 0.40f; // big font
        case 2: return 0.60f; // chat font
        case 3: return 0.45f; // gold font
        default: return 0.45f;
    }
}

constexpr ccColor3B kWhite{255, 255, 255};
constexpr ccColor3B kRed{255, 70, 100};
constexpr ccColor3B kGreen{90, 255, 150};
constexpr ccColor3B kCream{255, 243, 226};
constexpr ccColor3B kCreamPink{255, 228, 240};

bool isWhite(ccColor3B c) { return c.r == 255 && c.g == 255 && c.b == 255; }

ccColor3B lerpC(ccColor3B a, ccColor3B b, float t) {
    return {static_cast<GLubyte>(a.r + (b.r - a.r) * t), static_cast<GLubyte>(a.g + (b.g - a.g) * t),
            static_cast<GLubyte>(a.b + (b.b - a.b) * t)};
}

GLubyte toByte(float v) { return static_cast<GLubyte>(std::clamp(v, 0.f, 255.f)); }

void setDiameter(CCSprite* spr, float w, float h) {
    auto cs = spr->getContentSize();
    spr->setScaleX(w / cs.width);
    spr->setScaleY(h / cs.height);
}

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
                                             "Attempt",         "Percent", "Session Time", "Clock",           "Run From",      "Best Run",
                                             "Custom Label 1",  "Custom Label 2", "Custom Label 3"};
    return names[std::clamp(id, 0, kLabelCount - 1)];
}

char const* sampleText(int id) {
    static char const* samples[kLabelCount] = {"o", "240 FPS", "8 CPS", "100.00%", "0 Deaths", "Attempt 1", "42.00%", "1:23", "21:37:00", "Run From 0%", "Best Run 0-42%", "", "", ""};
    if (id >= LabelCustom1 && id <= LabelCustom3) return cfg().customLabels[id - LabelCustom1].c_str();
    return samples[std::clamp(id, 0, kLabelCount - 1)];
}

std::string expand(std::string text, std::vector<std::pair<std::string, std::string>> const& vars) {
    for (auto const& [key, value] : vars) {
        std::string token = "{" + key + "}";
        for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) {
            text.replace(at, token.size(), value);
        }
    }
    return text;
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
    if (m_discTex) m_discTex->release();
}

bool LabelsNode::init() {
    if (!CCNode::init()) return false;
    m_glowTex = tex::glow(64);
    m_glowTex->retain();
    m_discTex = tex::disc(64, false);
    m_discTex->retain();

    auto additiveSprite = [&](CCTexture2D* tex, CCNode* parent, int z) {
        auto spr = CCSprite::createWithTexture(tex);
        spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        parent->addChild(spr, z);
        return spr;
    };

    for (int i = 0; i < kLabelCount; i++) {
        auto& e = m_entries[i];
        e.root = CCNode::create();
        this->addChild(e.root);
        e.glow = additiveSprite(m_glowTex, e.root, 0);
        e.sparkle = additiveSprite(m_glowTex, e.root, 4);
        e.sparkle->setColor({255, 236, 250});
        e.phase = i * 0.9f;
    }

    // cheat indicator: soft glow + round dot
    auto& cheat = m_entries[LabelCheat];
    cheat.dotGlow = additiveSprite(m_glowTex, cheat.root, 1);
    cheat.dot = CCSprite::createWithTexture(m_discTex);
    cheat.root->addChild(cheat.dot, 2);

    s_current = this;
    return true;
}

bool LabelsNode::getWorldBox(int id, CCRect& out) const {
    if (id < 0 || id >= kLabelCount || !m_entries[id].hasBox) return false;
    out = m_entries[id].box;
    return true;
}

// creates/recreates the text node for the wanted font and sets the string
void LabelsNode::ensureText(Entry& e, int font, std::string const& text) {
    bool theme = font == 0;
    if (font != e.font) {
        if (e.bm) e.bm->removeFromParent();
        if (e.ttf) e.ttf->removeFromParent();
        if (e.halo) e.halo->removeFromParent();
        e.bm = nullptr;
        e.ttf = nullptr;
        e.halo = nullptr;

        if (theme) {
            e.halo = CCLabelTTF::create(text.c_str(), kThemeFont, kThemeFontSize);
            e.halo->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            e.root->addChild(e.halo, 1);
            e.ttf = CCLabelTTF::create(text.c_str(), kThemeFont, kThemeFontSize);
            e.root->addChild(e.ttf, 2);
        } else {
            e.bm = CCLabelBMFont::create(text.c_str(), labels::fontFile(font));
            if (!e.bm) e.bm = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            e.root->addChild(e.bm, 2);
        }
        e.font = font;
        e.text = text;
        return;
    }
    if (e.text == text) return;
    e.text = text;
    if (e.ttf) e.ttf->setString(text.c_str());
    if (e.halo) e.halo->setString(text.c_str());
    if (e.bm) e.bm->setString(text.c_str());
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

    colors[LabelCheat] = (st.cheatedAttempt || cheatsActive()) ? kRed : kGreen;
    m_fpsTimer -= dt;
    if (m_fpsTimer <= 0.f) {
        m_fpsTimer = 0.25f; // readable, and avoids re-rendering text every frame
        m_fpsText = fmt::format("{:.0f} FPS", st.fps);
    }
    texts[LabelFps] = m_fpsText;
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
    texts[LabelRunFrom] = fmt::format("Run From {:.0f}%", st.runStart);
    texts[LabelBestRun] = fmt::format("Best Run {:.0f}-{:.0f}%", st.bestStart, st.bestEnd);

    // custom labels: free text with {variables}
    {
        int spCount = extras::startPosCount(), spIndex = extras::startPosIndex();
        std::string level = pl->m_level ? std::string(pl->m_level->m_levelName) : "";
        int sessionSecs = static_cast<int>(st.sessionTime);
        std::vector<std::pair<std::string, std::string>> vars = {
            {"fps", fmt::format("{:.0f}", st.fps)},
            {"cps", std::to_string(st.clicks.size())},
            {"attempt", std::to_string(pl->m_attempts)},
            {"total", pl->m_level ? std::to_string(pl->m_level->m_attempts.value()) : "0"},
            {"percent", fmt::format("{:.2f}", pl->getCurrentPercent())},
            {"deaths", std::to_string(st.noclipDeaths)},
            {"accuracy", fmt::format("{:.2f}", st.noclipAccuracy())},
            {"time", fmt::format("{}:{:02}", sessionSecs / 60, sessionSecs % 60)},
            {"clock", texts[LabelClock]},
            {"level", level},
            {"best", fmt::format("{:.0f}-{:.0f}", st.bestStart, st.bestEnd)},
            {"runfrom", fmt::format("{:.0f}", st.runStart)},
            {"startpos", spCount == 0 ? std::string("-") : spIndex < 0 ? fmt::format("0/{}", spCount) : fmt::format("{}/{}", spIndex + 1, spCount)},
            {"speed", fmt::format("{:.2f}", cfg().speedhack ? cfg().speed : 1.f)},
        };
        for (int i = 0; i < 3; i++) texts[LabelCustom1 + i] = labels::expand(c.customLabels[i], vars);
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
        bool theme = font == 0;
        float ax = L.align == 0 ? 0.f : L.align == 1 ? 0.5f : 1.f;

        float bob = 0.f, breathe = 1.f;
        if (theme) {
            // calm floating + slow breathing glow
            bob = std::sin(m_time * 1.1f + e.phase) * 1.6f;
            breathe = 0.85f + 0.15f * std::sin(m_time * 0.9f + e.phase * 1.3f);
        }
        e.root->setPosition({L.x * win.width, L.y * win.height + bob});

        // ---- cheat indicator: a glowing circle, no text
        if (i == LabelCheat) {
            float d = 7.f * c.labelScale * L.scale;
            ccColor3B col = colors[i];
            float cx = (0.5f - ax) * d;
            e.dot->setPosition({cx, 0.f});
            setDiameter(e.dot, d, d);
            e.dot->setColor(col);
            e.dot->setOpacity(toByte(opacity * 255.f));
            e.dotGlow->setPosition({cx, 0.f});
            setDiameter(e.dotGlow, d * 3.2f, d * 3.2f);
            e.dotGlow->setColor(col);
            e.dotGlow->setOpacity(toByte(opacity * 120.f * breathe));
            e.glow->setVisible(false);
            e.sparkle->setVisible(false);

            CCPoint a = e.root->convertToWorldSpace({cx - d * 0.5f, -d * 0.5f});
            CCPoint b = e.root->convertToWorldSpace({cx + d * 0.5f, d * 0.5f});
            e.box = CCRect(a.x, a.y, b.x - a.x, b.y - a.y);
            e.hasBox = true;
            continue;
        }

        ensureText(e, font, texts[i]);
        CCNode* textNode = theme ? static_cast<CCNode*>(e.ttf) : static_cast<CCNode*>(e.bm);
        float scale = c.labelScale * L.scale * fontBaseScale(font);
        textNode->setAnchorPoint({ax, 0.5f});
        textNode->setScale(scale);

        float w = textNode->getContentSize().width * scale;
        float h = textNode->getContentSize().height * scale;
        float cx = (0.5f - ax) * w;

        e.glow->setVisible(theme);
        e.sparkle->setVisible(theme);
        if (theme) {
            // creamy text with a gentle shimmer (keeps warning colors like red)
            ccColor3B col = isWhite(colors[i]) ? lerpC(kCream, kCreamPink, 0.5f + 0.5f * std::sin(m_time * 0.6f + e.phase)) : colors[i];
            e.ttf->setColor(col);
            e.ttf->setOpacity(toByte(opacity * 255.f));

            // illuminated: slightly larger additive copy + soft purple glow behind
            e.halo->setAnchorPoint({ax, 0.5f});
            e.halo->setScale(scale * 1.045f);
            e.halo->setPosition({(ax - 0.5f) * w * 0.045f, 0.f});
            e.halo->setColor({200, 140, 255});
            e.halo->setOpacity(toByte(opacity * 110.f * breathe));

            e.glow->setPosition({cx, 0.f});
            setDiameter(e.glow, w * 1.3f + 20.f, h * 1.6f + 10.f);
            e.glow->setColor({150, 90, 255});
            e.glow->setOpacity(toByte(opacity * 85.f * breathe));

            float tw = std::pow(std::max(0.f, std::sin(m_time * 1.6f + e.phase * 2.f)), 6.f);
            e.sparkle->setPosition({cx + w * 0.5f + 4.f, h * 0.3f});
            setDiameter(e.sparkle, 5.f + 3.f * tw, 5.f + 3.f * tw);
            e.sparkle->setOpacity(toByte(opacity * 255.f * tw));
        } else {
            e.bm->setColor(colors[i]);
            e.bm->setOpacity(toByte(opacity * 255.f));
        }

        // screen-space box for the label editor
        CCRect bb = textNode->boundingBox();
        CCPoint a = e.root->convertToWorldSpace(bb.origin);
        CCPoint b = e.root->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});
        e.box = CCRect(a.x, a.y, b.x - a.x, b.y - a.y);
        e.hasBox = true;
    }
}

} // namespace starlight
