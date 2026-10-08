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
class GranularCloud
{
public:
    static constexpr int kMaxGrains = 96;
    static constexpr int kWindowSize = 1024;
    static constexpr int kTelemetryGrains = 24;

    struct Params
    {
        float density = 12.0f;
        float grainMs = 180.0f;
        float position = 0.3f;
        float spray = 0.15f;
        float scan = 0.0f;
        float pitch = 0.0f;
        float pitchSpread = 0.1f;
        float harmonize = 0.0f;
        float reverse = 0.0f;
        float shape = 0.5f;
        float stereo = 0.7f;
        float rootNote = 50.0f;
        float gravity = 0.0f;
    };

    struct GrainView
    {
        float position = 0.0f;
        float amplitude = 0.0f;
        float pan = 0.0f;
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    void setBuffer(const SampleBuffer* buffer) noexcept;
    void setHarmony(const HarmonicGravity* h) noexcept { harmony = h; }

    void setGrainLimit(int limit) noexcept { grainLimit = limit < 1 ? 1 : (limit > kMaxGrains ? kMaxGrains : limit); }

    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    int getActiveGrains() const noexcept { return activeCount; }
    float getPlayhead() const noexcept { return scanPosition; }
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
        float windowMix = 0.5f;
        float pan = 0.0f;
        int mip = 0;
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
    float scanPosition = 0.0f;
};
}
