#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/OnePole.h"
#include "../../harmony/HarmonicGravity.h"
#include "../../mod/Drift.h"

#include <array>
#include <cstdint>

namespace tf::dsp {
class ResonatorBank
{
public:
    static constexpr int kMaxModes = 24;

    struct Params
    {
        float rootNote = 50.0f;
        int modes = 16;
        float structure = 0.5f;
        float decaySeconds = 6.0f;
        float brightness = 0.5f;
        float rain = 0.3f;
        float rainColour = 0.5f;
        float spread = 0.7f;
        float gravity = 1.0f;
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }
    void strike(float amplitude) noexcept { pendingStrike = amplitude; }

    void setModeLimit(int limit) noexcept { modeLimit = limit < 1 ? 1 : (limit > kMaxModes ? kMaxModes : limit); }

    void process(const float* excite, float* left, float* right, int numSamples, float timeScale) noexcept;

    float getModeLevel(int mode) const noexcept;
    float getModeNote(int mode) const noexcept;

private:
    struct Mode
    {
        float note = 60.0f;
        float targetNote = 60.0f;
        float b1 = 0.0f, b2 = 0.0f, inGain = 0.0f;
        float y1 = 0.0f, y2 = 0.0f, x1 = 0.0f, x2 = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        float level = 0.0f;
        float seed = 0.0f;
        float amp = 1.0f;
    };

    void updateModes(float dt) noexcept;
    float modeTargetNote(int index) const noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    Drift tuningDrift;
    const HarmonicGravity* harmony = nullptr;
    std::array<Mode, kMaxModes> modes {};
    int modeLimit = kMaxModes;
    int ringingModes = 0;
    float pendingStrike = 0.0f;
    int strikePos = 0, strikeLength = 0;
    float strikeAmp = 0.0f;
    double samplesToBurst = 0.0;
    float outputLevel = 0.0f;
    float followAttack = 0.0f, followRelease = 0.0f;
    int samplesUntilControl = 0;
    bool snapNextUpdate = true;
};
}
