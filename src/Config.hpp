#pragma once

#include <functional>
#include <string>

namespace starlight {

// Label ids (order = default stacking order)
enum LabelId {
    LabelCheat,
    LabelFps,
    LabelCps,
    LabelNoclipAcc,
    LabelNoclipDeaths,
    LabelAttempt,
    LabelPercent,
    LabelTime,
    LabelClock,
    LabelRunFrom,
    LabelBestRun,
    kLabelCount,
};

struct LabelCfg {
    bool enabled = false;
    float x = 0.f;     // 0..1 of screen width
    float y = 0.f;     // 0..1 of screen height
    int align = 0;     // 0 left, 1 center, 2 right
    int font = -1;     // -1 = use the global label font
    float scale = 1.f; // multiplier on top of the global label scale
};

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
    bool pitchShift = false;
    float pitch = 1.f;
    bool lockCursor = false;
    bool tpsBypass = false;
    float tps = 240.f;
    bool frameStepper = false;
    bool pauseCountdown = false;
    bool showTaps = false;
    bool noShaders = false;
    bool noObjectGlow = false;
    bool unlockShop = false;
    bool botIgnoreInputs = true;

    // ---- Level
    bool noclip = false;
    bool noclipFlash = true;
    float noclipFlashColor[3] = {1.f, 0.15f, 0.35f};
    bool showHitboxes = false;
    bool noMirror = false;
    bool noParticles = false;
    bool hidePauseMenu = false;
    bool accuratePercent = false;
    bool autoLDM = false;
    bool autoPractice = false;
    bool freezeAttempts = false;
    bool instantMirror = false;
    bool noCameraShake = false;
    bool noEndShake = false;
    bool noPortalLightning = false;
    bool hideCompleteVfx = false;
    bool confirmRestart = false;
    bool confirmPractice = false;
    bool forcePlatformer = false;
    bool instantComplete = false;
    bool freeScroll = false;

    // ---- Player
    bool noDeathEffect = false;
    bool hidePlayer = false;
    bool noTrail = false;
    bool noWaveTrail = false;
    bool customRespawn = false;
    float respawnDelay = 0.5f;
    bool jumpHack = false;
    bool autoClicker = false;
    float autoClickerCps = 10.f;
    bool autoKill = false;
    float autoKillPercent = 50.f;
    bool noRespawnFlash = false;
    bool noSpiderDash = false;
    bool noRobotFire = false;
    bool noGhostTrail = false;
    bool forceGhostTrail = false;
    bool solidWaveTrail = false;
    bool muteOnDeath = false;
    bool nickChanger = false;
    std::string nick = "StarLight Team";
    bool cosmicProfile = true; // gradient name + "S" badge + UID on your profile
    std::string uid = "000";

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
    bool cosmicTrail = false;
    float trailWidth = 1.f;
    float trailLength = 1.f;
    float trailColorA[3] = {1.f, 0.45f, 0.85f};  // head
    float trailColorB[3] = {0.62f, 0.35f, 1.f};  // middle
    float trailColorC[3] = {0.30f, 0.55f, 1.f};  // tail
    bool trailSparkles = true;
    bool trailAnimate = true;
    int skybox = 0; // 0 off, 1 night, 2 evening, 3 nebula, 4 blood moon
    float starDensity = 1.f;
    bool shootingStars = true;
    bool skyParallax = true;
    bool hideGround = false;

    // ---- Labels
    bool labels = true;
    int labelFont = 0; // index into labels font list, 0 = Starlight Theme
    float labelScale = 1.f;
    float labelOpacity = 0.8f;
    LabelCfg lbl[kLabelCount];

    // ---- Interface
    float accent[3] = {0.66f, 0.33f, 0.97f};
    float uiScale = 1.f;
    bool animations = true;
    bool starfield = true;
    float bgDim = 0.45f;

    // ---- Keybinds (cocos2d::enumKeyCodes, 0 = none)
    int menuKey = 0x09; // KEY_Tab
    int noclipKey = 0;
    int stepKey = 0x46;     // F: frame stepper
    int teleportKey = 0;    // click teleport
    int jumpKeys[3] = {0, 0, 0}; // extra P1 jump keys
    int p2JumpKey = 0;
    int leftKey = 0;  // platformer
    int rightKey = 0; // platformer

    Config() {
        resetLabelLayout();
        lbl[LabelCheat].enabled = true;
    }

    // stacks the labels in the top-left corner
    void resetLabelLayout() {
        for (int i = 0; i < kLabelCount; i++) {
            lbl[i].x = 0.012f;
            lbl[i].y = 0.965f - i * 0.05f;
            lbl[i].align = 0;
            lbl[i].font = -1;
            lbl[i].scale = 1.f;
        }
    }
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
