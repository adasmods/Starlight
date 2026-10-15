#include "Config.hpp"

#include <Geode/Geode.hpp>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

using namespace geode::prelude;

namespace starlight {

Config& cfg() {
    static Config c;
    return c;
}

template <class F>
static void visit(Config& c, F&& f) {
    f("fps-bypass", c.fpsBypass);
    f("fps", c.fps);
    f("speedhack", c.speedhack);
    f("speed", c.speed);
    f("speedhack-audio", c.speedhackAudio);
    f("safe-mode", c.safeMode);
    f("auto-safe-mode", c.autoSafeMode);
    f("custom-transition", c.customTransition);
    f("transition-time", c.transitionTime);

    f("noclip", c.noclip);
    f("noclip-flash", c.noclipFlash);
    f("noclip-flash-color", c.noclipFlashColor);
    f("show-hitboxes", c.showHitboxes);
    f("no-mirror", c.noMirror);
    f("no-particles", c.noParticles);
    f("hide-pause-menu", c.hidePauseMenu);

    f("no-death-effect", c.noDeathEffect);
    f("hide-player", c.hidePlayer);
    f("no-trail", c.noTrail);
    f("no-wave-trail", c.noWaveTrail);
    f("custom-respawn", c.customRespawn);
    f("respawn-delay", c.respawnDelay);

    f("unlock-icons", c.unlockIcons);
    f("practice-music", c.practiceMusic);
    f("text-length-bypass", c.textLengthBypass);
    f("char-filter-bypass", c.charFilterBypass);

    f("orbs", c.orbs);
    f("orb-count", c.orbCount);
    f("orb-size", c.orbSize);
    f("orb-speed", c.orbSpeed);
    f("orb-trail", c.orbTrail);
    f("orb-rainbow", c.orbRainbow);
    f("orb-color-1", c.orbColor1);
    f("orb-color-2", c.orbColor2);
    f("player-aura", c.playerAura);
    f("skybox", c.skybox);
    f("star-density", c.starDensity);
    f("shooting-stars", c.shootingStars);
    f("sky-parallax", c.skyParallax);
    f("hide-ground", c.hideGround);

    f("labels", c.labels);
    f("label-corner", c.labelCorner);
    f("label-scale", c.labelScale);
    f("label-opacity", c.labelOpacity);
    f("lbl-cheat", c.lblCheat);
    f("lbl-fps", c.lblFps);
    f("lbl-cps", c.lblCps);
    f("lbl-noclip-acc", c.lblNoclipAcc);
    f("lbl-noclip-deaths", c.lblNoclipDeaths);
    f("lbl-attempt", c.lblAttempt);
    f("lbl-percent", c.lblPercent);
    f("lbl-time", c.lblTime);
    f("lbl-clock", c.lblClock);

    f("accent", c.accent);
    f("ui-scale", c.uiScale);
    f("animations", c.animations);
    f("starfield", c.starfield);
    f("bg-dim", c.bgDim);

    f("menu-key", c.menuKey);
    f("noclip-key", c.noclipKey);
}

template <class T>
static void loadOne(std::string const& key, T& v) {
    auto mod = Mod::get();
    if (!mod->hasSavedValue(key)) return;
    if constexpr (std::is_same_v<T, bool>) v = mod->getSavedValue<bool>(key, v);
    else if constexpr (std::is_same_v<T, int>) v = mod->getSavedValue<int>(key, v);
    else v = static_cast<float>(mod->getSavedValue<double>(key, v));
}

template <class T>
static void saveOne(std::string const& key, T const& v) {
    auto mod = Mod::get();
    if constexpr (std::is_same_v<T, bool>) mod->setSavedValue<bool>(key, v);
    else if constexpr (std::is_same_v<T, int>) mod->setSavedValue<int>(key, v);
    else mod->setSavedValue<double>(key, static_cast<double>(v));
}

void loadConfig() {
    visit(cfg(), [](char const* key, auto& v) {
        using T = std::remove_cvref_t<decltype(v)>;
        if constexpr (std::is_array_v<T>) {
            for (size_t i = 0; i < std::extent_v<T>; i++) loadOne(fmt::format("{}-{}", key, i), v[i]);
        } else {
            loadOne(key, v);
        }
    });
}

void saveConfig() {
    visit(cfg(), [](char const* key, auto& v) {
        using T = std::remove_cvref_t<decltype(v)>;
        if constexpr (std::is_array_v<T>) {
            for (size_t i = 0; i < std::extent_v<T>; i++) saveOne(fmt::format("{}-{}", key, i), v[i]);
        } else {
            saveOne(key, v);
        }
    });
}

bool cheatsActive() {
    auto& c = cfg();
    return c.noclip || (c.speedhack && c.speed != 1.f) || c.showHitboxes;
}

static std::mutex s_deferMutex;
static std::vector<std::function<void()>> s_deferred;

void defer(std::function<void()> fn) {
    std::lock_guard lock(s_deferMutex);
    s_deferred.push_back(std::move(fn));
}

void runDeferred() {
    std::vector<std::function<void()>> fns;
    {
        std::lock_guard lock(s_deferMutex);
        fns.swap(s_deferred);
    }
    for (auto& fn : fns) fn();
}

} // namespace starlight
