#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../core/SampleBuffer.h"
#include "../../harmony/HarmonicGravity.h"
#include "../../mod/Drift.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tf::dsp {

/** A cloud of grains read from a SampleBuffer.

    Grains start at a stochastic rate around `density`, read from around `position`
    (scattered by `spray`), at `pitch` semitones plus random spread. `harmonize` lets
    grains jump to scale intervals from harmonic gravity, `reverse` is the probability
    a grain plays backwards, and `scan` moves the read position through the buffer on
    its own (Tide-scaled), so a static patch keeps evolving.

    Grains are a fixed pool; when it is full new grains are skipped, never allocated. */
class GranularCloud
{
public:
    static constexpr int kMaxGrains = 96;
    static constexpr int kWindowSize = 1024;
    static constexpr int kTelemetryGrains = 24;

    struct Params
    {
        float density = 12.0f;      // grains per second
        float grainMs = 180.0f;     // grain length
        float position = 0.3f;      // 0..1 through the buffer
        float spray = 0.15f;        // 0..1 position scatter
        float scan = 0.0f;          // -1..1, buffer-lengths per minute at Tide 1
        float pitch = 0.0f;         // semitones
        float pitchSpread = 0.1f;   // 0..1, up to +-1 semitone of random detune
        float harmonize = 0.0f;     // 0..1 chance a grain jumps to a scale interval
        float reverse = 0.0f;       // 0..1 probability
        float shape = 0.5f;         // 0 = percussive (fast attack), 0.5 = Hann, 1 = flat-topped
        float stereo = 0.7f;        // 0..1 pan scatter
        float rootNote = 50.0f;     // where harmonize treats the sample's pitch to sit
        float gravity = 0.0f;       // amount of harmonic gravity applied to grain pitch
    };

    struct GrainView
    {
        float position = 0.0f; // 0..1 in the buffer
        float amplitude = 0.0f;
        float pan = 0.0f;      // -1..1
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    void setBuffer(const SampleBuffer* buffer) noexcept;
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }

    /** Grain cap from CPU guardrails, 1..kMaxGrains. */
    void setGrainLimit(int limit) noexcept { grainLimit = limit < 1 ? 1 : (limit > kMaxGrains ? kMaxGrains : limit); }

    /** Writes stereo output. timeScale (Tide) scales scan and drift only. */
    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    int getActiveGrains() const noexcept { return activeCount; }
    float getPlayhead() const noexcept { return scanPosition; }
    /** Fills up to kTelemetryGrains views; returns how many were written. */
    int getGrainViews(GrainView* out) const noexcept;

private:
    struct Grain
    {
        bool active = false;
        double readPos = 0.0;
        double increment = 1.0;
        int length = 1;
        int age = 0;
        float gainL = 0.0f, gainR = 0.0f;
        float windowMix = 0.5f; // 0 = perc, 0.5 = hann, 1 = tukey
        float pan = 0.0f;
        int mip = 0;            // band-limited level read (pitched up -> coarser)
    };

    void spawnGrain() noexcept;
    float windowAt(float phase, float mix) const noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    Drift positionDrift;
    const SampleBuffer* buffer = nullptr;
    const HarmonicGravity* harmony = nullptr;

    std::array<Grain, kMaxGrains> grains {};
    std::vector<float> hann, perc, tukey;
    int grainLimit = kMaxGrains;
    int activeCount = 0;
    double samplesToNextGrain = 0.0;
    float scanPosition = 0.0f; // offset added to position, wraps 0..1
};

} // namespace tf::dsp
