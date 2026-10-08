#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../harmony/HarmonicGravity.h"
#include "../../filters/Svf.h"
#include "../../mod/Drift.h"

#include <array>
#include <cstdint>

namespace tf::dsp {
class DroneGenerator
{
public:
    static constexpr int kMaxVoices = 6;
    static constexpr int kControlInterval = 32;

    struct Params
    {
        float rootNote = 38.0f;
        float detuneCents = 8.0f;
        float shape = 0.3f;
        float cutoffHz = 900.0f;
        float resonance = 0.2f;
        float noise = 0.1f;
        float driftDepth = 0.5f;
        float driftRate = 0.05f;
        float density = 3.0f;
        float evolve = 0.3f;
        float spread = 0.7f;
        float gravity = 0.0f;
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }

    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    float getVoiceLevel(int voice) const noexcept;

    float getVoiceInterval(int voice) const noexcept;

    float getVoiceNote(int voice) const noexcept;

private:
    struct Voice
    {
        std::array<double, 3> phase {};
        Svf filter;
        Random noise;
        Drift pitchDrift, cutoffDrift, panDrift, ampDrift;
        float interval = 0.0f;
        float pendingInterval = 0.0f;
        float densityGain = 0.0f;
        float revoiceGain = 1.0f;
        bool revoicing = false;
        bool fadingIn = false;
        float level = 0.0f;
        float basePan = 0.0f;
        float seed = 0.0f;
        float note = 0.0f;

        std::array<double, 3> increment {};
        float gainL = 0.0f, gainR = 0.0f;
        float prevGainL = 0.0f, prevGainR = 0.0f;
    };

    void updateControl(float dtSeconds) noexcept;
    float renderVoiceSample(Voice& v) noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    std::array<Voice, kMaxVoices> voices;
    float sineMix = 0.3f;
    float noiseGain = 0.0f;
    int samplesUntilControl = 0;
    const HarmonicGravity* harmony = nullptr;
    bool snapPitch = true;
};
}
