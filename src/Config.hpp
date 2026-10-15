#pragma once

#include <functional>

namespace starlight {

// Every option in the menu lives here. Saved/loaded through Geode saved values.
struct Config {
    // ---- Global
    bool fpsBypass = false;
    float fps = 240.f;
    bool speedhack = false;
    float speed = 1.f;
    bool speedhackAudio = true;
    bool safeMode = false;
    bool autoSafeMode = true;
    bool customTransition = false;
    float transitionTime = 0.5f;

    // ---- Level
    bool noclip = false;
    bool noclipFlash = true;
    float noclipFlashColor[3] = {1.f, 0.15f, 0.35f};
    bool showHitboxes = false;
    bool noMirror = false;
    bool noParticles = false;
    bool hidePauseMenu = false;

    // ---- Player
    bool noDeathEffect = false;
    bool hidePlayer = false;
    bool noTrail = false;
    bool noWaveTrail = false;
    bool customRespawn = false;
    float respawnDelay = 0.5f;

    // ---- Bypass
    bool unlockIcons = false;
    bool practiceMusic = false;
    bool textLengthBypass = false;
    bool charFilterBypass = false;

    // ---- Visuals
    bool orbs = false;
    int orbCount = 4;
    float orbSize = 1.f;
    float orbSpeed = 1.f;
    int orbTrail = 10;
    bool orbRainbow = false;
    float orbColor1[3] = {0.78f, 0.45f, 1.f};
    float orbColor2[3] = {0.35f, 0.62f, 1.f};
    bool playerAura = false;
    int skybox = 0; // 0 off, 1 night, 2 evening, 3 nebula, 4 blood moon
    float starDensity = 1.f;
    bool shootingStars = true;
    bool skyParallax = true;
    bool hideGround = false;

    // ---- Labels
    bool labels = true;
    int labelCorner = 0; // 0 TL, 1 TR, 2 BL, 3 BR
    float labelScale = 0.4f;
    float labelOpacity = 0.6f;
    bool lblCheat = true;
    bool lblFps = false;
    bool lblCps = false;
    bool lblNoclipAcc = false;
    bool lblNoclipDeaths = false;
    bool lblAttempt = false;
    bool lblPercent = false;
    bool lblTime = false;
    bool lblClock = false;

    // ---- Interface
    float accent[3] = {0.66f, 0.33f, 0.97f};
    float uiScale = 1.f;
    bool animations = true;
    bool starfield = true;
    float bgDim = 0.45f;

    // ---- Keybinds (cocos2d::enumKeyCodes, 0 = none)
    int menuKey = 0x09; // KEY_Tab
    int noclipKey = 0;
};

Config& cfg();
void loadConfig();
void saveConfig();

// true when anything that should count as a cheat is active
bool cheatsActive();

// Runs a function on the next scheduler tick (safe place to touch game nodes).
void defer(std::function<void()> fn);
void runDeferred();

} // namespace starlight
