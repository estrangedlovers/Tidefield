#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/OnePole.h"
#include "../../harmony/HarmonicGravity.h"
#include "../../mod/Drift.h"

#include <array>
#include <cstdint>

namespace tf::dsp {

/** A bank of tuned two-pole resonators: struck glass, sympathetic strings, singing
    bowls. Excited by its own sparse "rain" of noise bursts and by external signals
    (live input, drone, clouds) mixed in by the engine.

    Tuning comes from `structure`:
      0    harmonic series on the root
      0.5  scale tones from harmonic gravity across octaves (chordal)
      1    inharmonic, bell-like ratios
    with smooth blending in between. Mode frequencies glide when the key changes.

    Excitation is scaled per mode by sin(w), so an impulse of a given area rings every
    mode at about the same level whatever its pitch or decay. Rain strikes are short
    raised-cosine "mallet" pulses whose width sets their brightness. Sustained,
    in-tune excitation (a drone in the same key) would otherwise build up without
    bound at long decays, so an output follower ducks the excitation above ~-10 dBFS.
    Pole radii are strictly below 1 for every setting. */
class ResonatorBank
{
public:
    static constexpr int kMaxModes = 24;

    struct Params
    {
        float rootNote = 50.0f;
        int modes = 16;             // active modes (CPU guardrails may lower this)
        float structure = 0.5f;     // see class comment
        float decaySeconds = 6.0f;  // T60 of the lowest mode
        float brightness = 0.5f;    // 0 = high modes die fast, 1 = all ring equally
        float rain = 0.3f;          // self-excitation: bursts per second, 0..1 -> 0..8 Hz
        float rainColour = 0.5f;    // burst noise brightness
        float spread = 0.7f;        // stereo scatter of modes
        float gravity = 1.0f;       // how strongly modes snap to the scale
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }
    /** A mallet strike (0..1) at the next sample, like one drop of rain. */
    void strike(float amplitude) noexcept { pendingStrike = amplitude; }

    void setModeLimit(int limit) noexcept { modeLimit = limit < 1 ? 1 : (limit > kMaxModes ? kMaxModes : limit); }

    /** excite: optional mono excitation (may be null). Writes stereo output. */
    void process(const float* excite, float* left, float* right, int numSamples, float timeScale) noexcept;

    float getModeLevel(int mode) const noexcept;
    float getModeNote(int mode) const noexcept;

private:
    struct Mode
    {
        float note = 60.0f;        // current (gliding) MIDI note
        float targetNote = 60.0f;
        float b1 = 0.0f, b2 = 0.0f, inGain = 0.0f; // y = g*(x - x2) + b1*y1 - b2*y2
        float y1 = 0.0f, y2 = 0.0f, x1 = 0.0f, x2 = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        float level = 0.0f;        // envelope follower for telemetry
        float seed = 0.0f;         // stable per mode, for gravity migration
        float amp = 1.0f;          // per-mode random loudness
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
    float pendingStrike = 0.0f; // modes still processed: the active ones plus any dying out after a cut
    // Rain strikes: raised-cosine pulses.
    int strikePos = 0, strikeLength = 0;
    float strikeAmp = 0.0f;
    double samplesToBurst = 0.0;
    // Excitation ducking.
    float outputLevel = 0.0f;
    float followAttack = 0.0f, followRelease = 0.0f;
    int samplesUntilControl = 0;
    bool snapNextUpdate = true; // first tuning after prepare jumps instead of gliding
};

} // namespace tf::dsp
