#include "Menu.hpp"

#include "Config.hpp"
#include "State.hpp"
#include "visuals/Labels.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace starlight::menu {
namespace {

// ============================================================================
//  State
// ============================================================================

bool s_open = false;
float s_progress = 0.f; // 0 = closed, 1 = fully open (linear, eased per element)
float s_saveTimer = 0.f;
int* s_capturing = nullptr; // keybind waiting for a key
char s_search[64] = "";
bool s_inPopup = false;
bool s_layoutReady = false;
bool s_editLabels = false;
char s_nickBuf[64] = "";
bool s_nickBufInit = false;

ImFont* s_fontRegular = nullptr;
ImFont* s_fontBold = nullptr;

float s_appliedScale = -1.f;
float s_appliedAccent[3] = {-1.f, -1.f, -1.f};

std::unordered_map<ImGuiID, float> s_anims;

constexpr float kPi = 3.14159265f;

// ============================================================================
//  Math / color helpers
// ============================================================================

float S() { return cfg().uiScale; }
float dt() { return std::min(ImGui::GetIO().DeltaTime, 0.1f); }
float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float approach(float v, float target, float step) {
    return v < target ? std::min(v + step, target) : std::max(v - step, target);
}
float easeOutCubic(float t) {
    t = 1.f - clamp01(t);
    return 1.f - t * t * t;
}
float easeOutBack(float t) {
    t = clamp01(t);
    constexpr float c1 = 1.70158f, c3 = c1 + 1.f;
    float u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}
float smooth(float t) {
    t = clamp01(t);
    return t * t * (3.f - 2.f * t);
}

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}
ImVec4 withA(ImVec4 v, float a) {
    v.w = a;
    return v;
}
ImU32 u32(ImVec4 v) {
    v.w = clamp01(v.w);
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImVec4 accent(float a = 1.f) {
    auto& c = cfg().accent;
    return {c[0], c[1], c[2], a};
}
ImVec4 accent2(float a = 1.f) { return withA(mix(accent(), {0.25f, 0.45f, 1.f, 1.f}, 0.55f), a); }

const ImVec4 kWhite{1.f, 1.f, 1.f, 1.f};
const ImVec4 kText{0.95f, 0.93f, 1.f, 1.f};
const ImVec4 kTextDim{0.60f, 0.56f, 0.72f, 1.f};
const ImVec4 kPanel{0.045f, 0.032f, 0.085f, 0.94f};
const ImVec4 kFrame{0.11f, 0.085f, 0.18f, 1.f};
const ImVec4 kSwitchOff{0.19f, 0.16f, 0.28f, 1.f};

float& anim(ImGuiID id, float init) {
    auto [it, inserted] = s_anims.try_emplace(id, init);
    return it->second;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matches(char const* label) {
    if (s_inPopup || !s_search[0]) return true;
    return lower(label).find(lower(s_search)) != std::string::npos;
}

ImFont* boldFont() { return s_fontBold ? s_fontBold : ImGui::GetFont(); }
ImFont* regularFont() { return s_fontRegular ? s_fontRegular : ImGui::GetFont(); }

ImVec2 textSize(ImFont* font, float size, char const* text, char const* end = nullptr) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.f, text, end);
}

// 4-pointed star, built from triangles so it works without concave fill
void sparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    float k = r * 0.22f;
    dl->AddTriangleFilled({c.x, c.y - r}, {c.x + k, c.y}, {c.x - k, c.y}, col);
    dl->AddTriangleFilled({c.x, c.y + r}, {c.x - k, c.y}, {c.x + k, c.y}, col);
    dl->AddTriangleFilled({c.x - r, c.y}, {c.x, c.y - k}, {c.x, c.y + k}, col);
    dl->AddTriangleFilled({c.x + r, c.y}, {c.x, c.y + k}, {c.x, c.y - k}, col);
}

std::string keyName(int k) {
    if (k == 0) return "None";
    if ((k >= 0x41 && k <= 0x5A) || (k >= 0x30 && k <= 0x39)) return std::string(1, static_cast<char>(k));
    if (k >= 0x70 && k <= 0x87) return fmt::format("F{}", k - 0x6F);
    if (k >= 0x60 && k <= 0x69) return fmt::format("Num {}", k - 0x60);
    switch (k) {
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0x14: return "Caps Lock";
        case 0x20: return "Space";
        case 0x21: return "Page Up";
        case 0x22: return "Page Down";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0xA0: return "LShift";
        case 0xA1: return "RShift";
        case 0xA2: return "LCtrl";
        case 0xA3: return "RCtrl";
        case 0xC0: return "`";
        case 0xDC: return "\\";
        default: return fmt::format("Key {}", k);
    }
}

// ============================================================================
//  Theme
// ============================================================================

void applyTheme() {
    auto& c = cfg();
    if (s_appliedScale == c.uiScale && std::equal(c.accent, c.accent + 3, s_appliedAccent)) return;
    s_appliedScale = c.uiScale;
    std::copy(c.accent, c.accent + 3, s_appliedAccent);

    float keepFontBase = ImGui::GetStyle().FontSizeBase;
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);

    style.WindowRounding = 9.f;
    style.ChildRounding = 6.f;
    style.FrameRounding = 5.f;
    style.PopupRounding = 8.f;
    style.GrabRounding = 4.f;
    style.ScrollbarRounding = 6.f;
    style.TabRounding = 5.f;
    style.WindowBorderSize = 1.f;
    style.PopupBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.WindowPadding = {10.f, 0.f};
    style.FramePadding = {8.f, 4.f};
    style.ItemSpacing = {6.f, 3.f};
    style.ItemInnerSpacing = {6.f, 4.f};
    style.WindowMinSize = {32.f, 32.f};
    style.GrabMinSize = 10.f;
    style.ScrollbarSize = 10.f;

    auto* col = style.Colors;
    col[ImGuiCol_Text] = kText;
    col[ImGuiCol_TextDisabled] = kTextDim;
    col[ImGuiCol_WindowBg] = kPanel;
    col[ImGuiCol_PopupBg] = {0.06f, 0.045f, 0.11f, 0.98f};
    col[ImGuiCol_Border] = accent(0.35f);
    col[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    col[ImGuiCol_FrameBg] = kFrame;
    col[ImGuiCol_FrameBgHovered] = mix(kFrame, accent(), 0.25f);
    col[ImGuiCol_FrameBgActive] = mix(kFrame, accent(), 0.4f);
    col[ImGuiCol_SliderGrab] = accent();
    col[ImGuiCol_SliderGrabActive] = mix(accent(), kWhite, 0.3f);
    col[ImGuiCol_CheckMark] = accent();
    col[ImGuiCol_Button] = mix(kFrame, accent(), 0.2f);
    col[ImGuiCol_ButtonHovered] = mix(kFrame, accent(), 0.45f);
    col[ImGuiCol_ButtonActive] = mix(kFrame, accent(), 0.65f);
    col[ImGuiCol_Header] = mix(kFrame, accent(), 0.35f);
    col[ImGuiCol_HeaderHovered] = mix(kFrame, accent(), 0.5f);
    col[ImGuiCol_HeaderActive] = mix(kFrame, accent(), 0.7f);
    col[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    col[ImGuiCol_ScrollbarGrab] = accent(0.4f);
    col[ImGuiCol_ScrollbarGrabHovered] = accent(0.6f);
    col[ImGuiCol_ScrollbarGrabActive] = accent(0.8f);
    col[ImGuiCol_Separator] = accent(0.25f);
    col[ImGuiCol_TextSelectedBg] = accent(0.35f);
    col[ImGuiCol_NavCursor] = accent();

    style.ScaleAllSizes(c.uiScale);
    style.FontSizeBase = keepFontBase;
    style.FontScaleMain = c.uiScale;
    ImGui::GetStyle() = style;
}

// ============================================================================
//  Panels (draggable, collapsible windows with animated open)
// ============================================================================

struct PanelDef {
    char const* id;
    char const* title;
    int rows; // rough height estimate for default layout
};

constexpr PanelDef kPanels[] = {
    {"global", "Global", 5},   {"level", "Level", 6},         {"player", "Player", 6},
    {"bypass", "Bypass", 4},   {"visuals", "Visuals", 14},    {"labels", "Labels", 14},
    {"keybinds", "Keybinds", 8}, {"interface", "Interface", 9}, {"shortcuts", "Shortcuts", 5},
};
constexpr int kPanelCount = sizeof(kPanels) / sizeof(kPanels[0]);

struct PanelState {
    ImVec2 pos{};
    bool placed = false;      // pos pushed to imgui at least once
    bool wasAnimating = false;
    bool collapsed = false;
    bool dragged = false;
};
std::unordered_map<std::string, PanelState> s_panels;

float panelWidth() { return 232.f * S(); }

void computeLayout(bool force) {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float s = S();
    float pw = panelWidth(), gap = 12.f * s;
    int cols = std::max(1, static_cast<int>((ds.x - 24.f * s + gap) / (pw + gap)));
    cols = std::min(cols, kPanelCount);
    float total = cols * pw + (cols - 1) * gap;
    float startX = std::max(8.f * s, (ds.x - total) * 0.5f);
    std::vector<float> bottoms(cols, 86.f * s);

    for (auto const& def : kPanels) {
        auto& st = s_panels[def.id];
        int col = static_cast<int>(std::min_element(bottoms.begin(), bottoms.end()) - bottoms.begin());
        ImVec2 pos{startX + col * (pw + gap), bottoms[col]};
        bottoms[col] += (34.f + def.rows * 28.f + 12.f) * s + gap;

        auto keyX = fmt::format("panel-{}-x", def.id);
        auto keyY = fmt::format("panel-{}-y", def.id);
        if (!force && Mod::get()->hasSavedValue(keyX)) {
            pos.x = static_cast<float>(Mod::get()->getSavedValue<double>(keyX, pos.x));
            pos.y = static_cast<float>(Mod::get()->getSavedValue<double>(keyY, pos.y));
            st.collapsed = Mod::get()->getSavedValue<bool>(fmt::format("panel-{}-collapsed", def.id), false);
        }
        if (force) st.collapsed = false;
        st.pos = pos;
        st.placed = false;
    }
    s_layoutReady = true;
}

void saveAll() {
    saveConfig();
    for (auto const& [id, st] : s_panels) {
        Mod::get()->setSavedValue<double>(fmt::format("panel-{}-x", id), st.pos.x);
        Mod::get()->setSavedValue<double>(fmt::format("panel-{}-y", id), st.pos.y);
        Mod::get()->setSavedValue<bool>(fmt::format("panel-{}-collapsed", id), st.collapsed);
    }
}

class Panel {
public:
    Panel(char const* id, char const* title, int index) : m_st(&s_panels[id]) {
        float s = S();
        auto& io = ImGui::GetIO();

        // staggered per-panel animation
        constexpr float stagger = 0.07f;
        float local = clamp01(s_progress * (1.f + stagger * (kPanelCount - 1)) - stagger * index);
        m_alpha = easeOutCubic(local);
        float slide = (1.f - easeOutBack(local)) * 46.f * s;
        bool animating = local < 1.f;

        if (animating || !m_st->placed || m_st->wasAnimating) {
            ImGui::SetNextWindowPos(m_st->pos + ImVec2(0.f, animating ? slide : 0.f), ImGuiCond_Always);
            m_st->placed = true;
        }
        m_st->wasAnimating = animating;
        m_animating = animating;

        ImGui::SetNextWindowSizeConstraints({panelWidth(), 0.f}, {panelWidth(), FLT_MAX});
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(m_alpha, 0.001f));
        auto name = fmt::format("{}##starlight-{}", title, id);
        ImGui::Begin(
            name.c_str(), nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse
        );

        ImVec2 wp = ImGui::GetWindowPos();
        float ww = ImGui::GetWindowWidth();
        float hh = 30.f * s;
        float r = ImGui::GetStyle().WindowRounding;
        float t = static_cast<float>(ImGui::GetTime());
        auto* dl = ImGui::GetWindowDrawList();

        // header: rounded base + gradient + moving shimmer
        dl->AddRectFilled(wp, wp + ImVec2(ww, hh), u32(mix(accent(), {0, 0, 0, 1}, 0.25f)), r, ImDrawFlags_RoundCornersTop);
        dl->PushClipRect(wp + ImVec2(r * 0.5f, 0.f), wp + ImVec2(ww - r * 0.5f, hh), true);
        dl->AddRectFilledMultiColor(
            wp, wp + ImVec2(ww, hh), u32(accent(0.95f)), u32(accent2(0.95f)), u32(accent2(0.7f)), u32(accent(0.7f))
        );
        float cycle = std::fmod(t * 0.35f + index * 0.21f, 1.6f) / 1.6f;
        float sx = wp.x - 60.f * s + cycle * (ww + 120.f * s);
        ImU32 clear = u32(withA(kWhite, 0.f)), shine = u32(withA(kWhite, 0.16f));
        dl->AddRectFilledMultiColor({sx - 50.f * s, wp.y}, {sx, wp.y + hh}, clear, shine, shine, clear);
        dl->AddRectFilledMultiColor({sx, wp.y}, {sx + 50.f * s, wp.y + hh}, shine, clear, clear, shine);
        dl->PopClipRect();
        dl->AddLine({wp.x, wp.y + hh}, {wp.x + ww, wp.y + hh}, u32(withA(kWhite, 0.22f)));

        // collapse arrow (rotates)
        float& rot = anim(ImGui::GetID("##arrow"), m_st->collapsed ? 1.f : 0.f);
        rot = approach(rot, m_st->collapsed ? 1.f : 0.f, dt() * 8.f);
        {
            ImVec2 c{wp.x + 15.f * s, wp.y + hh * 0.5f};
            float a = -kPi * 0.5f * smooth(rot);
            auto rp = [&](float x, float y) {
                return ImVec2{c.x + (x * std::cos(a) - y * std::sin(a)) * s, c.y + (x * std::sin(a) + y * std::cos(a)) * s};
            };
            dl->AddTriangleFilled(rp(-5.f, -2.5f), rp(5.f, -2.5f), rp(0.f, 3.5f), u32(withA(kWhite, 0.9f)));
        }

        // title + sparkle
        ImFont* font = boldFont();
        float fs = ImGui::GetFontSize();
        ImVec2 ts = textSize(font, fs, title);
        ImVec2 tp{wp.x + (ww - ts.x) * 0.5f, wp.y + (hh - ts.y) * 0.5f};
        dl->AddText(font, fs, tp + ImVec2(0.f, 1.f * s), u32({0, 0, 0, 0.35f}), title);
        dl->AddText(font, fs, tp, u32(kWhite), title);
        float tw = 0.5f + 0.5f * std::sin(t * 2.2f + index);
        sparkle(dl, {tp.x - 11.f * s, wp.y + hh * 0.5f}, (4.f + 1.5f * tw) * s, u32(withA(kWhite, 0.6f + 0.4f * tw)));

        // header interaction: drag to move, click to collapse
        ImGui::SetCursorScreenPos(wp);
        ImGui::InvisibleButton("##header", {ww, hh});
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f)) {
            m_st->dragged = true;
            ImGui::SetWindowPos(ImGui::GetWindowPos() + io.MouseDelta);
        }
        if (ImGui::IsItemDeactivated()) {
            if (!m_st->dragged) m_st->collapsed = !m_st->collapsed;
            m_st->dragged = false;
        }

        ImGui::SetCursorScreenPos(wp + ImVec2(ImGui::GetStyle().WindowPadding.x, hh + 6.f * s));
        if (m_st->collapsed) ImGui::Dummy({0.f, 0.f});
    }

    ~Panel() {
        float s = S();
        if (!m_st->collapsed) ImGui::Dummy({0.f, 4.f * s});
        if (!m_animating) m_st->pos = ImGui::GetWindowPos();

        // soft outer glow drawn behind every window
        ImVec2 wp = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
        float r = ImGui::GetStyle().WindowRounding;
        auto* bg = ImGui::GetBackgroundDrawList();
        for (int k = 1; k <= 6; k++) {
            float e = k * 2.f * s;
            bg->AddRect(
                wp - ImVec2(e, e), wp + sz + ImVec2(e, e), u32(accent(0.09f * (1.f - k / 7.f) * m_alpha)), r + e, 0, 2.2f * s
            );
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    explicit operator bool() const { return !m_st->collapsed; }

    Panel(Panel const&) = delete;
    Panel& operator=(Panel const&) = delete;

private:
    PanelState* m_st;
    float m_alpha = 1.f;
    bool m_animating = false;
};

// ============================================================================
//  Widgets
// ============================================================================

struct PadScope {
    PadScope(float x = 10.f, float y = 8.f) { ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(x, y) * S()); }
    ~PadScope() { ImGui::PopStyleVar(); }
};

void tooltip(char const* text) {
    PadScope pad(9.f, 7.f);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(240.f * S());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void smallLabel(char const* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool toggle(char const* label, bool* v, char const* tip = nullptr, char const* popup = nullptr) {
    if (!matches(label)) return false;
    float s = S();
    ImGui::PushID(label);

    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = 25.f * s;
    float optW = popup ? 22.f * s : 0.f;
    bool changed = false;

    if (ImGui::InvisibleButton("##row", {w - optW, h})) {
        *v = !*v;
        changed = true;
    }
    bool hov = ImGui::IsItemHovered();
    ImGuiID id = ImGui::GetItemID();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) tooltip(tip);

    bool openPopup = false, optHov = false;
    if (popup) {
        ImGui::SameLine(0.f, 0.f);
        if (ImGui::InvisibleButton("##opt", {optW, h})) openPopup = true;
        optHov = ImGui::IsItemHovered();
    }

    float& on = anim(id, *v ? 1.f : 0.f);
    on = approach(on, *v ? 1.f : 0.f, dt() * 7.f);
    float& ho = anim(id ^ 0x5eedu, 0.f);
    ho = approach(ho, (hov || optHov) ? 1.f : 0.f, dt() * 9.f);

    auto* dl = ImGui::GetWindowDrawList();
    if (ho > 0.f) dl->AddRectFilled(p, p + ImVec2(w, h), u32(accent(0.10f * ho)), 4.f * s);

    // left accent bar (grows when enabled)
    if (on > 0.f) {
        float bh = (h - 8.f * s) * easeOutCubic(on);
        float cy = p.y + h * 0.5f;
        dl->AddRectFilled({p.x, cy - bh * 0.5f}, {p.x + 7.f * s, cy + bh * 0.5f}, u32(accent(0.15f * on)), 3.f * s);
        dl->AddRectFilled({p.x, cy - bh * 0.5f}, {p.x + 3.f * s, cy + bh * 0.5f}, u32(accent(on)), 2.f * s);
    }

    float fs = ImGui::GetFontSize();
    ImVec4 tc = mix(kTextDim, kText, std::max(on, ho * 0.6f));
    dl->AddText({p.x + (10.f + 2.f * on) * s, p.y + (h - fs) * 0.5f}, u32(tc), label);

    // pill switch
    float sw = 30.f * s, sh = 16.f * s;
    ImVec2 sp{p.x + w - optW - sw - 5.f * s, p.y + (h - sh) * 0.5f};
    dl->AddRectFilled(sp, sp + ImVec2(sw, sh), u32(mix(kSwitchOff, accent(), on)), sh * 0.5f);
    if (on > 0.01f) {
        float e = 1.5f * s;
        dl->AddRect(sp - ImVec2(e, e), sp + ImVec2(sw + e, sh + e), u32(accent(0.35f * on)), sh * 0.5f + e, 0, e);
    }
    float kx = sp.x + sh * 0.5f + (sw - sh) * smooth(on);
    dl->AddCircleFilled({kx, sp.y + sh * 0.5f}, sh * 0.5f - 2.5f * s, u32(mix({0.72f, 0.68f, 0.84f, 1.f}, kWhite, on)), 20);

    // options arrow
    if (popup) {
        ImVec2 c{p.x + w - optW * 0.5f, p.y + h * 0.5f};
        ImU32 ac = u32(optHov ? accent() : kTextDim);
        dl->AddTriangleFilled({c.x - 2.5f * s, c.y - 4.5f * s}, {c.x + 3.5f * s, c.y}, {c.x - 2.5f * s, c.y + 4.5f * s}, ac);
    }

    ImGui::PopID();
    if (openPopup) ImGui::OpenPopup(popup);
    return changed;
}

template <class F>
void options(char const* popup, F&& body) {
    bool open;
    {
        PadScope pad(12.f, 10.f);
        open = ImGui::BeginPopup(popup);
    }
    if (!open) return;
    s_inPopup = true;
    ImGui::Dummy({200.f * S(), 0.f});
    body();
    s_inPopup = false;
    ImGui::EndPopup();
}

bool sliderF(char const* label, float* v, float mn, float mx, char const* fmtStr, ImGuiSliderFlags flags = 0) {
    if (!matches(label)) return false;
    ImGui::PushID(label);
    smallLabel(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool r = ImGui::SliderFloat("##v", v, mn, mx, fmtStr, flags);
    ImGui::PopID();
    return r;
}

bool sliderI(char const* label, int* v, int mn, int mx) {
    if (!matches(label)) return false;
    ImGui::PushID(label);
    smallLabel(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool r = ImGui::SliderInt("##v", v, mn, mx);
    ImGui::PopID();
    return r;
}

bool combo(char const* label, int* v, char const* const* items, int count) {
    if (!matches(label)) return false;
    ImGui::PushID(label);
    smallLabel(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool r;
    {
        PadScope pad(8.f, 6.f);
        r = ImGui::Combo("##v", v, items, count);
    }
    ImGui::PopID();
    return r;
}

bool colorRow(char const* label, float* col) {
    if (!matches(label)) return false;
    ImGui::PushID(label);
    bool r;
    {
        PadScope pad(8.f, 8.f);
        r = ImGui::ColorEdit3("##c", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return r;
}

bool button(char const* label, float width = 0.f) {
    if (!matches(label)) return false;
    float s = S();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = width > 0.f ? width : ImGui::GetContentRegionAvail().x, h = 26.f * s;
    bool pressed = ImGui::InvisibleButton(label, {w, h});
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    float& t = anim(ImGui::GetItemID(), 0.f);
    t = approach(t, hov ? 1.f : 0.f, dt() * 8.f);

    auto* dl = ImGui::GetWindowDrawList();
    ImVec4 base = mix(kFrame, accent(), 0.16f + 0.24f * t + (act ? 0.25f : 0.f));
    dl->AddRectFilled(p, p + ImVec2(w, h), u32(base), 5.f * s);
    if (t > 0.f) {
        float e = 2.f * s * t;
        dl->AddRect(p - ImVec2(e, e), p + ImVec2(w + e, h + e), u32(accent(0.18f * t)), 5.f * s + e, 0, 2.f * s);
    }
    dl->AddRect(p, p + ImVec2(w, h), u32(accent(0.25f + 0.5f * t)), 5.f * s);
    float fs = ImGui::GetFontSize();
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText({p.x + (w - ts.x) * 0.5f, p.y + (h - fs) * 0.5f}, u32(mix(kText, kWhite, t)), label);
    return pressed;
}

void keybind(char const* label, int* key) {
    if (!matches(label)) return;
    float s = S();
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = 26.f * s, bw = 92.f * s;
    bool waiting = s_capturing == key;

    ImGui::Dummy({w - bw, h});
    ImGui::SameLine(0.f, 0.f);
    if (ImGui::InvisibleButton("##kb", {bw, h})) s_capturing = waiting ? nullptr : key;
    bool hov = ImGui::IsItemHovered();
    if (hov) tooltip("Click, then press a key. Esc = none.");

    auto* dl = ImGui::GetWindowDrawList();
    float fs = ImGui::GetFontSize();
    dl->AddText({p.x + 10.f * s, p.y + (h - fs) * 0.5f}, u32(kText), label);

    ImVec2 bp{p.x + w - bw, p.y + 2.f * s}, be{p.x + w, p.y + h - 2.f * s};
    float pulse = waiting ? 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.f) : 0.f;
    dl->AddRectFilled(bp, be, u32(mix(kFrame, accent(), (hov ? 0.3f : 0.12f) + pulse * 0.4f)), 5.f * s);
    dl->AddRect(bp, be, u32(accent(0.4f + pulse * 0.6f)), 5.f * s);
    std::string text = waiting ? "..." : keyName(*key);
    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    dl->AddText({bp.x + (bw - ts.x) * 0.5f, p.y + (h - fs) * 0.5f}, u32(kText), text.c_str());
    ImGui::PopID();
}

// ============================================================================
//  Background: dim, nebula, twinkling stars, shooting stars, logo
// ============================================================================

struct BgStar {
    float x, y, r, phase, speed, depth;
    ImVec4 col;
};

std::vector<BgStar> const& bgStars() {
    static std::vector<BgStar> stars = [] {
        std::vector<BgStar> v;
        std::mt19937 rng(1337);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        for (int i = 0; i < 240; i++) {
            float big = u(rng);
            BgStar st;
            st.x = u(rng);
            st.y = u(rng);
            st.r = big > 0.93f ? 1.8f + u(rng) * 1.2f : 0.6f + u(rng) * 1.0f;
            st.phase = u(rng) * 6.28f;
            st.speed = 0.8f + u(rng) * 2.5f;
            st.depth = 0.3f + u(rng);
            float tint = u(rng);
            st.col = tint < 0.6f ? ImVec4{1.f, 1.f, 1.f, 1.f}
                   : tint < 0.8f ? ImVec4{0.8f, 0.7f, 1.f, 1.f}
                                 : ImVec4{0.7f, 0.85f, 1.f, 1.f};
            v.push_back(st);
        }
        return v;
    }();
    return stars;
}

void drawBackdrop(float g) {
    auto* dl = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float t = static_cast<float>(ImGui::GetTime());
    float s = S();

    dl->AddRectFilled({0.f, 0.f}, ds, u32({0.015f, 0.008f, 0.04f, cfg().bgDim * g}));
    ImU32 top = u32(accent(0.20f * g)), topClear = u32(accent(0.f));
    dl->AddRectFilledMultiColor({0.f, 0.f}, {ds.x, ds.y * 0.35f}, top, top, topClear, topClear);
    ImU32 bot = u32({0.15f, 0.2f, 0.65f, 0.16f * g}), botClear = u32({0.15f, 0.2f, 0.65f, 0.f});
    dl->AddRectFilledMultiColor({0.f, ds.y * 0.65f}, ds, botClear, botClear, bot, bot);

    if (!cfg().starfield) return;

    // nebula clouds (stacked translucent circles)
    struct Blob {
        float x, y, r, sp;
        ImVec4 c;
    };
    Blob blobs[] = {
        {0.20f, 0.32f, 0.30f, 0.11f, accent()},
        {0.80f, 0.64f, 0.34f, 0.08f, {0.25f, 0.4f, 1.f, 1.f}},
        {0.55f, 0.14f, 0.22f, 0.13f, {0.9f, 0.3f, 0.7f, 1.f}},
    };
    float maxDim = std::max(ds.x, ds.y);
    for (auto const& b : blobs) {
        ImVec2 c{(b.x + 0.03f * std::sin(t * b.sp * 2.f)) * ds.x, (b.y + 0.03f * std::cos(t * b.sp * 3.f)) * ds.y};
        float R = b.r * maxDim;
        for (int k = 0; k < 14; k++) {
            float f = 1.f - k / 14.f;
            dl->AddCircleFilled(c, R * f, u32(withA(b.c, 0.018f * g)), 48);
        }
    }

    // twinkling, slowly drifting stars
    for (auto const& st : bgStars()) {
        float x = std::fmod(st.x + t * 0.004f * st.depth, 1.f) * ds.x;
        ImVec2 p{x, st.y * ds.y};
        float tw = 0.5f + 0.5f * std::sin(t * st.speed + st.phase);
        float a = (0.3f + 0.7f * tw) * g;
        float r = st.r * s;
        if (st.r > 1.6f) dl->AddCircleFilled(p, r * 3.2f, u32(withA(st.col, 0.10f * a)), 16);
        dl->AddCircleFilled(p, r, u32(withA(st.col, a)), 8);
        if (st.r > 2.2f && tw > 0.75f) sparkle(dl, p, r * 4.f, u32(withA(st.col, (tw - 0.75f) * 4.f * 0.7f * a)));
    }

    // shooting star
    static float next = 1.5f, life = -1.f;
    static ImVec2 start;
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    if (life < 0.f) {
        next -= dt();
        if (next <= 0.f) {
            life = 0.f;
            start = {(0.35f + u(rng) * 0.75f) * ds.x, u(rng) * 0.35f * ds.y};
            next = 2.5f + u(rng) * 5.f;
        }
    } else {
        life += dt();
        constexpr float L = 1.1f;
        if (life > L) {
            life = -1.f;
        } else {
            float pr = life / L;
            ImVec2 dir{-0.86f, 0.5f};
            ImVec2 head = start + dir * (pr * ds.x * 0.45f);
            float fade = std::sin(pr * kPi) * g;
            float len = 150.f * s;
            for (int i = 0; i < 16; i++) {
                float f = i / 16.f, f2 = (i + 1) / 16.f;
                dl->AddLine(head - dir * (f * len), head - dir * (f2 * len), u32({1.f, 0.92f, 1.f, (1.f - f) * fade * 0.9f}),
                            (2.2f * (1.f - f) + 0.4f) * s);
            }
            dl->AddCircleFilled(head, 7.f * s, u32(accent(0.25f * fade)), 16);
            dl->AddCircleFilled(head, 2.4f * s, u32(withA(kWhite, fade)), 12);
        }
    }
}

void drawLogo(float g) {
    auto* dl = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float s = S();
    float t = static_cast<float>(ImGui::GetTime());
    ImFont* font = boldFont();
    float size = 36.f * s, spacing = 8.f * s;
    char const* text = "STARLIGHT";
    int n = static_cast<int>(std::strlen(text));

    float total = -spacing;
    for (int i = 0; i < n; i++) total += textSize(font, size, text + i, text + i + 1).x + spacing;
    float x = (ds.x - total) * 0.5f;
    float y = 16.f * s - (1.f - g) * 24.f * s;
    float startX = x;

    for (int i = 0; i < n; i++) {
        char const* ch = text + i;
        float cw = textSize(font, size, ch, ch + 1).x;
        float k = static_cast<float>(i) / n;
        ImVec4 c = mix(accent(), {0.55f, 0.75f, 1.f, 1.f}, 0.5f + 0.5f * std::sin(t * 1.5f - k * 4.f));
        float bob = std::sin(t * 2.f + i * 0.6f) * 1.5f * s;
        for (int d = 0; d < 8; d++) {
            float ang = d * kPi / 4.f;
            ImVec2 off{std::cos(ang) * 2.5f * s, std::sin(ang) * 2.5f * s};
            dl->AddText(font, size, ImVec2{x, y + bob} + off, u32(accent(0.13f * g)), ch, ch + 1);
        }
        dl->AddText(font, size, {x, y + bob}, u32(withA(mix(c, kWhite, 0.35f), g)), ch, ch + 1);
        x += cw + spacing;
    }

    float tw = 0.5f + 0.5f * std::sin(t * 3.f);
    sparkle(dl, {startX - 24.f * s, y + size * 0.5f}, (8.f + 3.f * tw) * s, u32(withA(kWhite, 0.85f * g)));
    sparkle(dl, {x + 16.f * s, y + size * 0.5f}, (8.f + 3.f * (1.f - tw)) * s, u32(withA(kWhite, 0.85f * g)));

    auto sub = fmt::format("cosmic mod menu   -   press {} to close", keyName(cfg().menuKey));
    float subSize = 14.f * s;
    ImVec2 ss = textSize(regularFont(), subSize, sub.c_str());
    dl->AddText(regularFont(), subSize, {(ds.x - ss.x) * 0.5f, y + size + 4.f * s}, u32(withA(kTextDim, 0.9f * g)), sub.c_str());
}

// ============================================================================
//  Panel contents
// ============================================================================

void panelGlobal(int idx) {
    auto& c = cfg();
    if (Panel p{"global", "Global", idx}) {
        toggle("FPS Bypass", &c.fpsBypass, "Unlocks the frame rate cap. Turn VSync off in GD graphics settings.", "##fps");
        options("##fps", [&] { sliderF("Target FPS", &c.fps, 30.f, 540.f, "%.0f FPS"); });

        toggle("Speedhack", &c.speedhack, "Changes the game speed.", "##speed");
        options("##speed", [&] {
            sliderF("Speed", &c.speed, 0.1f, 4.f, "%.2fx", ImGuiSliderFlags_Logarithmic);
            toggle("Sync Audio", &c.speedhackAudio, "Changes music pitch to match the speed.");
            if (button("Reset to 1.00x")) c.speed = 1.f;
        });

        toggle("Safe Mode", &c.safeMode, "Never saves progress or completions.");
        toggle("Auto Safe Mode", &c.autoSafeMode, "Blocks progress automatically when a cheat was used this attempt.");
        toggle("Transition Time", &c.customTransition, "Changes the scene fade duration.", "##trans");
        options("##trans", [&] { sliderF("Duration multiplier", &c.transitionTime, 0.f, 1.f, "%.2fx"); });
    }
}

void panelLevel(int idx) {
    auto& c = cfg();
    if (Panel p{"level", "Level", idx}) {
        toggle("Noclip", &c.noclip, "You can't die.", "##noclip");
        options("##noclip", [&] {
            toggle("Death Flash", &c.noclipFlash, "Flashes the screen when you would have died.");
            colorRow("Flash Color", c.noclipFlashColor);
        });
        toggle("Show Hitboxes", &c.showHitboxes, "Draws object and player hitboxes.");
        toggle("No Mirror", &c.noMirror, "Disables the mirror portal effect.");
        toggle("No Particles", &c.noParticles, "Hides all particle systems.");
        toggle("Hide Pause Menu", &c.hidePauseMenu, "Makes the pause menu invisible.");
    }
}

void panelPlayer(int idx) {
    auto& c = cfg();
    if (Panel p{"player", "Player", idx}) {
        toggle("No Death Effect", &c.noDeathEffect, "Removes the explosion when dying.");
        toggle("Hide Player", &c.hidePlayer, "Makes the player icon invisible.");
        toggle("No Trail", &c.noTrail, "Hides the regular player trail.");
        toggle("No Wave Trail", &c.noWaveTrail, "Hides the wave trail.");
        toggle("Respawn Delay", &c.customRespawn, "Custom time before respawning.", "##respawn");
        options("##respawn", [&] { sliderF("Delay", &c.respawnDelay, 0.f, 2.f, "%.2f s"); });
        toggle("Nick Changer", &c.nickChanger, "Shows a custom name instead of yours. Visual only, nothing is sent to the servers.", "##nick");
        options("##nick", [&] {
            if (!s_nickBufInit) {
                std::strncpy(s_nickBuf, c.nick.c_str(), sizeof(s_nickBuf) - 1);
                s_nickBufInit = true;
            }
            smallLabel("Nick");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##nickinput", s_nickBuf, sizeof(s_nickBuf))) c.nick = s_nickBuf;
            smallLabel("Reopen a page to see the change.");
        });
    }
}

void panelBypass(int idx) {
    auto& c = cfg();
    if (Panel p{"bypass", "Bypass", idx}) {
        toggle("Unlock Icons", &c.unlockIcons, "Unlocks every icon and color (client-side only).");
        toggle("Practice Music Sync", &c.practiceMusic, "Unlocks the practice music sync option.");
        toggle("Text Length Bypass", &c.textLengthBypass, "Removes text input length limits.");
        toggle("Character Filter Bypass", &c.charFilterBypass, "Allows any character in text inputs.");
    }
}

void panelVisuals(int idx) {
    auto& c = cfg();
    if (Panel p{"visuals", "Visuals", idx}) {
        toggle("Star Orbs", &c.orbs, "Glowing orbs that fly behind the player.", "##orbs");
        options("##orbs", [&] {
            sliderI("Orb Count", &c.orbCount, 1, 8);
            sliderF("Orb Size", &c.orbSize, 0.4f, 2.5f, "%.2fx");
            sliderF("Orbit Speed", &c.orbSpeed, 0.2f, 3.f, "%.2fx");
            sliderI("Trail Length", &c.orbTrail, 0, 16);
            colorRow("Color A", c.orbColor1);
            colorRow("Color B", c.orbColor2);
            toggle("Rainbow", &c.orbRainbow, "Cycles orb colors.");
        });
        toggle("Player Aura", &c.playerAura, "Soft glow around the player.");
        toggle("Cosmic Trail", &c.cosmicTrail, "Glowing gradient trail with sparkles behind the player.", "##ctrail");
        options("##ctrail", [&] {
            sliderF("Width", &c.trailWidth, 0.3f, 3.f, "%.2fx");
            sliderF("Length", &c.trailLength, 0.2f, 4.f, "%.2fx");
            colorRow("Head Color", c.trailColorA);
            colorRow("Middle Color", c.trailColorB);
            colorRow("Tail Color", c.trailColorC);
            toggle("Sparkles", &c.trailSparkles, "Little stars drifting off the trail.");
            toggle("Light Pulse", &c.trailAnimate, "A soft pulse of light travels down the trail.");
            if (button("Hide Default Trail")) c.noTrail = true;
        });

        if (matches("Skybox")) ImGui::Dummy({0.f, 2.f * S()});
        static char const* skies[] = {"Off", "Starry Night", "Evening Sunset", "Deep Nebula", "Blood Moon"};
        combo("Skybox", &c.skybox, skies, 5);
        sliderF("Star Density", &c.starDensity, 0.2f, 3.f, "%.2fx");
        toggle("Shooting Stars", &c.shootingStars, "Occasional shooting stars on the skybox.");
        toggle("Sky Parallax", &c.skyParallax, "Stars move slightly with the camera.");
        toggle("Hide Ground", &c.hideGround, "Hides the ground so the skybox fills the screen.");
    }
}

// per-label settings (used by the Labels panel and by the label editor)
void labelOptionsBody(int i) {
    auto& L = cfg().lbl[i];
    static char const* aligns[] = {"Left", "Center", "Right"};
    combo("Alignment", &L.align, aligns, 3);

    static std::vector<char const*> fontItems = [] {
        std::vector<char const*> v{"Use global font"};
        for (int f = 0; f < labels::fontCount(); f++) v.push_back(labels::fontNames()[f]);
        return v;
    }();
    int sel = L.font + 1;
    if (combo("Font", &sel, fontItems.data(), static_cast<int>(fontItems.size()))) L.font = sel - 1;
    sliderF("Size", &L.scale, 0.3f, 3.f, "%.2fx");
    if (button("Reset Position")) {
        Config def;
        L.x = def.lbl[i].x;
        L.y = def.lbl[i].y;
        L.align = def.lbl[i].align;
    }
}

void panelLabels(int idx) {
    auto& c = cfg();
    static char const* popupIds[kLabelCount] = {"##lbl0", "##lbl1", "##lbl2", "##lbl3", "##lbl4",
                                                "##lbl5", "##lbl6", "##lbl7", "##lbl8"};
    static char const* tips[kLabelCount] = {
        "Red dot = cheats used this attempt, green = clean.", nullptr, "Clicks per second.", nullptr, nullptr, nullptr, nullptr,
        "Time spent in the level this session.", nullptr};

    if (Panel p{"labels", "Labels", idx}) {
        toggle("Show Labels", &c.labels, "Master switch for all labels.", "##labels");
        options("##labels", [&] {
            sliderF("Scale", &c.labelScale, 0.3f, 3.f, "%.2fx");
            sliderF("Opacity", &c.labelOpacity, 0.1f, 1.f, "%.2f");
        });
        combo("Font", &c.labelFont, labels::fontNames(), labels::fontCount());

        for (int i = 0; i < kLabelCount; i++) {
            toggle(labels::labelName(i), &c.lbl[i].enabled, tips[i], popupIds[i]);
            options(popupIds[i], [&] { labelOptionsBody(i); });
        }

        if (button("Edit Positions")) s_editLabels = true;
        if (button("Reset Positions")) c.resetLabelLayout();
    }
}

void panelKeybinds(int idx) {
    auto& c = cfg();
    if (Panel p{"keybinds", "Keybinds", idx}) {
        keybind("Menu Key", &c.menuKey);
        keybind("Noclip Key", &c.noclipKey);
        keybind("Jump Key 1", &c.jumpKeys[0]);
        keybind("Jump Key 2", &c.jumpKeys[1]);
        keybind("Jump Key 3", &c.jumpKeys[2]);
        keybind("P2 Jump Key", &c.p2JumpKey);
        keybind("Platformer Left", &c.leftKey);
        keybind("Platformer Right", &c.rightKey);
    }
}

// Full-screen overlay: drag labels to move them, right-click for options.
void drawLabelEditor(float g) {
    auto& io = ImGui::GetIO();
    auto& c = cfg();
    float s = S();
    float t = static_cast<float>(ImGui::GetTime());
    auto win = CCDirector::get()->getWinSize();
    ImVec2 ds = io.DisplaySize;

    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin(
        "##starlight-label-editor", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleVar();
    auto* dl = ImGui::GetWindowDrawList();

    // center guides
    ImVec2 center = ImGuiCocos::cocosToFrame(ccp(win.width * 0.5f, win.height * 0.5f));
    dl->AddLine({center.x, 0.f}, {center.x, ds.y}, u32(accent(0.18f * g)));
    dl->AddLine({0.f, center.y}, {ds.x, center.y}, u32(accent(0.18f * g)));

    auto node = LabelsNode::current();
    static ImVec2 dragStart;

    for (int i = 0; i < kLabelCount; i++) {
        auto& L = c.lbl[i];
        if (!L.enabled) continue;
        ImGui::PushID(i);

        ImVec2 anchor = ImGuiCocos::cocosToFrame(ccp(L.x * win.width, L.y * win.height));
        ImVec2 a, b;
        CCRect r;
        if (node && node->getWorldBox(i, r)) {
            a = ImGuiCocos::cocosToFrame(ccp(r.getMinX(), r.getMaxY()));
            b = ImGuiCocos::cocosToFrame(ccp(r.getMaxX(), r.getMinY()));
        } else {
            // no level open: draw a preview of the label
            char const* sample = labels::sampleText(i);
            float fs = 18.f * s * c.labelScale * L.scale;
            ImVec2 ts = textSize(boldFont(), fs, sample);
            float ax = L.align == 0 ? 0.f : L.align == 1 ? 0.5f : 1.f;
            a = {anchor.x - ts.x * ax, anchor.y - ts.y * 0.5f};
            b = a + ts;
            dl->AddText(boldFont(), fs, a, u32(withA(mix(kText, accent(), 0.3f), g)), sample);
        }
        a -= ImVec2(5.f * s, 3.f * s);
        b += ImVec2(5.f * s, 3.f * s);
        if (b.x - a.x < 18.f * s) {
            float cx = (a.x + b.x) * 0.5f;
            a.x = cx - 9.f * s;
            b.x = cx + 9.f * s;
        }
        if (b.y - a.y < 14.f * s) {
            float cy = (a.y + b.y) * 0.5f;
            a.y = cy - 7.f * s;
            b.y = cy + 7.f * s;
        }

        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##box", b - a, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (ImGui::IsItemActivated()) dragStart = anchor;
        if (act && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
            ImVec2 raw = dragStart + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
            CCPoint cp = ImGuiCocos::frameToCocos(raw);
            // snap to edges and center
            auto snap = [](float v, float size) {
                float pts = v * size;
                for (float target : {size * 0.012f, size * 0.5f, size * 0.988f}) {
                    if (std::abs(pts - target) < 5.f) return target / size;
                }
                return v;
            };
            L.x = std::clamp(snap(cp.x / win.width, win.width), 0.f, 1.f);
            L.y = std::clamp(snap(cp.y / win.height, win.height), 0.f, 1.f);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##lblopts");
        if (hov && !act) tooltip("Drag to move - right-click for options");

        float pulse = 0.5f + 0.5f * std::sin(t * 3.f + i);
        bool hot = hov || act;
        dl->AddRectFilled(a, b, u32(accent((hot ? 0.22f : 0.10f) * g)), 4.f * s);
        dl->AddRect(a, b, u32(accent((hot ? 1.f : 0.45f + 0.25f * pulse) * g)), 4.f * s, 0, 1.5f * s);

        char const* name = labels::labelName(i);
        float ns = 12.f * s;
        ImVec2 nts = textSize(regularFont(), ns, name);
        ImVec2 tag{a.x, a.y - nts.y - 4.f * s};
        if (tag.y < 0.f) tag.y = b.y + 4.f * s;
        dl->AddRectFilled(tag - ImVec2(4.f * s, 1.f * s), tag + nts + ImVec2(4.f * s, 1.f * s), u32(withA(kPanel, 0.9f * g)), 3.f * s);
        dl->AddText(regularFont(), ns, tag, u32(withA(kText, g)), name);

        options("##lblopts", [&] { labelOptionsBody(i); });
        ImGui::PopID();
    }

    // banner with instructions
    float bw = 430.f * s, bh = node ? 74.f * s : 92.f * s;
    ImVec2 bp{(ds.x - bw) * 0.5f, 14.f * s};
    dl->AddRectFilled(bp, bp + ImVec2(bw, bh), u32(withA(kPanel, 0.95f * g)), 9.f * s);
    dl->AddRect(bp, bp + ImVec2(bw, bh), u32(accent(0.6f * g)), 9.f * s, 0, 1.2f * s);
    ImFont* bold = boldFont();
    float fs = ImGui::GetFontSize();
    char const* title = "Label Editor";
    ImVec2 tsz = textSize(bold, fs, title);
    dl->AddText(bold, fs, {bp.x + (bw - tsz.x) * 0.5f, bp.y + 8.f * s}, u32(withA(kWhite, g)), title);
    sparkle(dl, {bp.x + (bw - tsz.x) * 0.5f - 12.f * s, bp.y + 8.f * s + tsz.y * 0.5f}, 5.f * s, u32(accent(g)));
    char const* help = "Drag labels to move them. Right-click a label for its options.";
    ImVec2 hsz = ImGui::CalcTextSize(help);
    dl->AddText({bp.x + (bw - hsz.x) * 0.5f, bp.y + 30.f * s}, u32(withA(kTextDim, g)), help);
    if (!node) {
        char const* hint = "Open a level to see and move the real labels.";
        ImVec2 isz = ImGui::CalcTextSize(hint);
        dl->AddText({bp.x + (bw - isz.x) * 0.5f, bp.y + 48.f * s}, u32(withA(accent2(), g)), hint);
    }

    float btnW = 120.f * s;
    ImGui::SetCursorScreenPos({bp.x + bw * 0.5f - btnW - 5.f * s, bp.y + bh + 8.f * s});
    if (button("Done", btnW)) s_editLabels = false;
    ImGui::SetCursorScreenPos({bp.x + bw * 0.5f + 5.f * s, bp.y + bh + 8.f * s});
    if (button("Reset All", btnW)) c.resetLabelLayout();

    ImGui::End();
}

void panelInterface(int idx) {
    auto& c = cfg();
    if (Panel p{"interface", "Interface", idx}) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        {
            PadScope pad(8.f, 6.f);
            ImGui::InputTextWithHint("##search", "Search options...", s_search, sizeof(s_search));
        }
        colorRow("Accent Color", c.accent);
        sliderF("UI Scale", &c.uiScale, 0.75f, 1.5f, "%.2fx");
        toggle("Animations", &c.animations, "Animated opening and closing.");
        toggle("Starfield Backdrop", &c.starfield, "Stars and nebula behind the menu.");
        sliderF("Background Dim", &c.bgDim, 0.f, 0.9f, "%.2f");
        if (button("Reset Layout")) computeLayout(true);
        if (button("Save Settings")) saveAll();
    }
}

void panelShortcuts(int idx) {
    if (Panel p{"shortcuts", "Shortcuts", idx}) {
        if (button("Restart Level")) defer([] {
            if (auto pl = PlayLayer::get()) pl->resetLevel();
        });
        if (button("Toggle Practice Mode")) defer([] {
            if (auto pl = PlayLayer::get()) pl->togglePracticeMode(!pl->m_isPracticeMode);
        });
        if (button("Place Checkpoint")) defer([] {
            if (auto pl = PlayLayer::get(); pl && pl->m_isPracticeMode) pl->markCheckpoint();
        });
        if (button("Remove Checkpoint")) defer([] {
            if (auto pl = PlayLayer::get(); pl && pl->m_isPracticeMode) pl->removeCheckpoint(false);
        });
        if (button("Game Options")) defer([] { OptionsLayer::create()->showLayer(false); });
    }
}

} // namespace

// ============================================================================
//  Public API
// ============================================================================

void setup() {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    auto tryFont = [&](char const* path, float size) -> ImFont* {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return nullptr;
        return io.Fonts->AddFontFromFileTTF(path, size);
    };
    s_fontRegular = tryFont("C:\\Windows\\Fonts\\segoeui.ttf", 17.f);
    if (!s_fontRegular) s_fontRegular = io.Fonts->AddFontDefault();
    s_fontBold = tryFont("C:\\Windows\\Fonts\\seguisb.ttf", 17.f);
    if (!s_fontBold) s_fontBold = tryFont("C:\\Windows\\Fonts\\segoeuib.ttf", 17.f);
    if (!s_fontBold) s_fontBold = s_fontRegular;
    io.FontDefault = s_fontRegular;

    s_appliedScale = -1.f; // force theme re-apply
}

void draw() {
    float step = cfg().animations ? dt() / 0.5f : 1.f;
    s_progress = approach(s_progress, s_open ? 1.f : 0.f, step);
    if (!s_open && s_progress <= 0.f) {
        ImGuiCocos::get().setVisible(false);
        return;
    }

    applyTheme();
    if (!s_layoutReady) computeLayout(false);

    s_saveTimer += dt();
    if (s_saveTimer > 3.f) {
        s_saveTimer = 0.f;
        saveAll();
    }

    float g = easeOutCubic(s_progress);

    if (s_editLabels) {
        // light dim only, so the level stays visible while placing labels
        ImGui::GetBackgroundDrawList()->AddRectFilled({0.f, 0.f}, ImGui::GetIO().DisplaySize, u32({0.015f, 0.008f, 0.04f, 0.18f * g}));
        drawLabelEditor(g);
        return;
    }

    drawBackdrop(g);
    drawLogo(g);

    int i = 0;
    panelGlobal(i++);
    panelLevel(i++);
    panelPlayer(i++);
    panelBypass(i++);
    panelVisuals(i++);
    panelLabels(i++);
    panelKeybinds(i++);
    panelInterface(i++);
    panelShortcuts(i++);
}

void toggle() {
    s_open = !s_open;
    if (s_open) {
        ImGuiCocos::get().setVisible(true);
        PlatformToolbox::showCursor();
    } else {
        s_capturing = nullptr;
        s_editLabels = false;
        saveAll();
    }
}

bool isOpen() { return s_open; }

bool isEditingLabels() { return s_open && s_editLabels; }

bool captureKey(int key) {
    if (!s_capturing) return false;
    *s_capturing = key == 0x1B ? 0 : key; // Esc clears
    s_capturing = nullptr;
    return true;
}

} // namespace starlight::menu
