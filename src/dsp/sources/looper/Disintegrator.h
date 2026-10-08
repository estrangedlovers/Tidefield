#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/DcBlocker.h"
#include "../../filters/OnePole.h"

#include <vector>

namespace tf::dsp {
class Disintegrator
{
public:
    static constexpr float kMaxSeconds = 60.0f;
    static constexpr float kEdgeSeconds = 0.008f;

    enum class State : int { Empty = 0, Recording = 1, Playing = 2, Overdubbing = 3, Clearing = 4 };

    struct Params
    {
        float erosion = 0.4f;
        float flakes = 0.3f;
        float overdub = 0.7f;
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }

    void record() noexcept;
    void clear() noexcept;

    void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    State getState() const noexcept { return state; }
    float getPosition() const noexcept;
    float getLengthSeconds() const noexcept;
    int getPasses() const noexcept { return passes; }

private:
    void closeLoop() noexcept;
    float erode(float x, int ch) noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    std::vector<float> bufL, bufR;
    int capacity = 0;
    int length = 0;
    int pos = 0;
    int passes = 0;
    State state = State::Empty;
    float clearGain = 1.0f;
    int edge = 64;

    OnePole lowPass[2], highPass[2];
    DcBlocker dc[2];
    int flakeRemaining = 0, flakeLength = 1;
    float flakeDepth = 0.0f;
};
}
