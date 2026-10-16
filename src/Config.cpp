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
    f("pitch-shift", c.pitchShift);
    f("pitch", c.pitch);
    f("lock-cursor", c.lockCursor);

    f("noclip", c.noclip);
    f("noclip-flash", c.noclipFlash);
    f("noclip-flash-color", c.noclipFlashColor);
    f("show-hitboxes", c.showHitboxes);
    f("no-mirror", c.noMirror);
    f("no-particles", c.noParticles);
    f("hide-pause-menu", c.hidePauseMenu);
    f("accurate-percent", c.accuratePercent);
    f("auto-ldm", c.autoLDM);
    f("auto-practice", c.autoPractice);
    f("freeze-attempts", c.freezeAttempts);
    f("instant-mirror", c.instantMirror);
    f("no-camera-shake", c.noCameraShake);
    f("no-end-shake", c.noEndShake);
    f("no-portal-lightning", c.noPortalLightning);
    f("hide-complete-vfx", c.hideCompleteVfx);
    f("confirm-restart", c.confirmRestart);
    f("confirm-practice", c.confirmPractice);
    f("force-platformer", c.forcePlatformer);
    f("instant-complete", c.instantComplete);
    f("free-scroll", c.freeScroll);

    f("no-death-effect", c.noDeathEffect);
    f("hide-player", c.hidePlayer);
    f("no-trail", c.noTrail);
    f("no-wave-trail", c.noWaveTrail);
    f("custom-respawn", c.customRespawn);
    f("respawn-delay", c.respawnDelay);
    f("jump-hack", c.jumpHack);
    f("auto-clicker", c.autoClicker);
    f("auto-clicker-cps", c.autoClickerCps);
    f("auto-kill", c.autoKill);
    f("auto-kill-percent", c.autoKillPercent);
    f("no-respawn-flash", c.noRespawnFlash);
    f("no-spider-dash", c.noSpiderDash);
    f("no-robot-fire", c.noRobotFire);
    f("no-ghost-trail", c.noGhostTrail);
    f("force-ghost-trail", c.forceGhostTrail);
    f("solid-wave-trail", c.solidWaveTrail);
    f("mute-on-death", c.muteOnDeath);
    f("nick-changer", c.nickChanger);
    f("nick", c.nick);
    f("cosmic-profile", c.cosmicProfile);
    f("uid", c.uid);

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
    f("cosmic-trail", c.cosmicTrail);
    f("trail-width", c.trailWidth);
    f("trail-length", c.trailLength);
    f("trail-color-a", c.trailColorA);
    f("trail-color-b", c.trailColorB);
    f("trail-color-c", c.trailColorC);
    f("trail-sparkles", c.trailSparkles);
    f("trail-animate", c.trailAnimate);
    f("skybox", c.skybox);
    f("star-density", c.starDensity);
    f("shooting-stars", c.shootingStars);
    f("sky-parallax", c.skyParallax);
    f("hide-ground", c.hideGround);

    f("labels", c.labels);
    f("label-font", c.labelFont);
    f("label-scale-v2", c.labelScale);
    f("label-opacity", c.labelOpacity);
    for (int i = 0; i < kLabelCount; i++) {
        auto& l = c.lbl[i];
        f(fmt::format("label-{}-enabled", i), l.enabled);
        f(fmt::format("label-{}-x", i), l.x);
        f(fmt::format("label-{}-y", i), l.y);
        f(fmt::format("label-{}-align", i), l.align);
        f(fmt::format("label-{}-font", i), l.font);
        f(fmt::format("label-{}-scale", i), l.scale);
    }

    f("accent", c.accent);
    f("ui-scale", c.uiScale);
    f("animations", c.animations);
    f("starfield", c.starfield);
    f("bg-dim", c.bgDim);

    f("menu-key", c.menuKey);
    f("noclip-key", c.noclipKey);
    f("jump-keys", c.jumpKeys);
    f("p2-jump-key", c.p2JumpKey);
    f("left-key", c.leftKey);
    f("right-key", c.rightKey);
}

template <class T>
static void loadOne(std::string const& key, T& v) {
    auto mod = Mod::get();
    if (!mod->hasSavedValue(key)) return;
    if constexpr (std::is_same_v<T, bool>) v = mod->getSavedValue<bool>(key, v);
    else if constexpr (std::is_same_v<T, std::string>) v = mod->getSavedValue<std::string>(key, v);
    else if constexpr (std::is_same_v<T, int>) v = mod->getSavedValue<int>(key, v);
    else v = static_cast<float>(mod->getSavedValue<double>(key, v));
}

template <class T>
static void saveOne(std::string const& key, T const& v) {
    auto mod = Mod::get();
    if constexpr (std::is_same_v<T, bool>) mod->setSavedValue<bool>(key, v);
    else if constexpr (std::is_same_v<T, std::string>) mod->setSavedValue<std::string>(key, v);
    else if constexpr (std::is_same_v<T, int>) mod->setSavedValue<int>(key, v);
    else mod->setSavedValue<double>(key, static_cast<double>(v));
}

void loadConfig() {
    visit(cfg(), [](std::string const& key, auto& v) {
        using T = std::remove_cvref_t<decltype(v)>;
        if constexpr (std::is_array_v<T>) {
            for (size_t i = 0; i < std::extent_v<T>; i++) loadOne(fmt::format("{}-{}", key, i), v[i]);
        } else {
            loadOne(key, v);
        }
    });
}

void saveConfig() {
    visit(cfg(), [](std::string const& key, auto& v) {
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
    return c.noclip || (c.speedhack && c.speed != 1.f) || c.showHitboxes || c.jumpHack || c.autoClicker || c.instantComplete ||
           c.forcePlatformer;
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
