#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/Svf.h"
#include "../../mod/Drift.h"

#include <array>
#include <cstdint>

namespace tf::dsp {

/** A small ensemble of slowly breathing voices built on a shared root.

    Each voice is three detuned band-limited saws blended toward a sine, plus a little
    filtered noise, through its own drifting state-variable filter. Voices fade in and
    out with `density`, and with probability `evolve` a voice will occasionally fade
    out, move to a new interval and fade back in, so the drone keeps changing when
    nobody touches it. All modulation time runs at `timeScale` (Tide). */
class DroneGenerator
{
public:
    static constexpr int kMaxVoices = 6;
    static constexpr int kControlInterval = 32;

    struct Params
    {
        float rootNote = 38.0f;        // MIDI note of the root (D2)
        float detuneCents = 8.0f;      // spread between the three oscillators
        float shape = 0.3f;            // 0 = saw, 1 = sine
        float cutoffHz = 900.0f;       // base filter cutoff
        float resonance = 0.2f;        // 0..0.95
        float noise = 0.1f;            // breath noise into each voice
        float driftDepth = 0.5f;       // 0..1 scales all per-voice drift
        float driftRate = 0.05f;       // Hz, before Tide
        float density = 3.0f;          // number of sounding voices, fractional
        float evolve = 0.3f;           // 0..1, how often voices re-voice
        float spread = 0.7f;           // stereo width of the ensemble
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }

    /** Writes (does not add) numSamples of stereo output. timeScale multiplies all
        modulation and evolution rates. */
    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    /** Per-voice loudness for telemetry/visuals, 0..1. */
    float getVoiceLevel(int voice) const noexcept;

    /** Current interval, in semitones from the root, of each voice. */
    float getVoiceInterval(int voice) const noexcept;

private:
    struct Voice
    {
        std::array<double, 3> phase {};
        Svf filter;
        Random noise;                // per-voice so breath noise is decorrelated across the field
        Drift pitchDrift, cutoffDrift, panDrift, ampDrift;
        float interval = 0.0f;
        float pendingInterval = 0.0f;
        float densityGain = 0.0f;   // smoothed target from density
        float revoiceGain = 1.0f;   // dips to 0 while re-voicing
        bool revoicing = false;
        bool fadingIn = false;
        float level = 0.0f;         // for telemetry
        float basePan = 0.0f;

        // Per-control-tick cached values.
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
};

} // namespace tf::dsp
