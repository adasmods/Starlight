#pragma once

#include <deque>

namespace starlight {

// Runtime stats shared between hooks, labels and the menu.
struct State {
    // noclip
    bool noclipHitThisFrame = false;
    bool noclipWasHit = false;
    int noclipFrames = 0;
    int noclipDeadFrames = 0;
    int noclipDeaths = 0;

    // a cheat was used during the current attempt
    bool cheatedAttempt = false;

    // timing
    float fps = 0.f;
    double sessionTime = 0.0;

    // clicks (timestamps in seconds)
    std::deque<double> clicks;

    void resetAttempt() {
        noclipHitThisFrame = false;
        noclipWasHit = false;
        noclipFrames = 0;
        noclipDeadFrames = 0;
        noclipDeaths = 0;
    }

    float noclipAccuracy() const {
        if (noclipFrames <= 0) return 100.f;
        return 100.f * (1.f - static_cast<float>(noclipDeadFrames) / static_cast<float>(noclipFrames));
    }
};

inline State& state() {
    static State s;
    return s;
}

double nowSeconds();

} // namespace starlight
