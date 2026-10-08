#pragma once

#include "../core/MathUtil.h"
#include "../core/Random.h"

#include <algorithm>

namespace tf::dsp {
class Drift
{
public:
    void setSeed(std::uint64_t seed) noexcept
    {
        rng.setSeed(seed);
        from = to = rng.nextBipolar();
        startSegment();
    }

    void setRate(float hz) noexcept { rateHz = std::clamp(hz, 0.0005f, 20.0f); }

    float advance(float dtSeconds) noexcept
    {
        phase += dtSeconds * rateHz / segmentLength;
        if (phase >= 1.0f)
        {
            from = to;
            phase -= std::floor(phase);
            startSegment();
        }
        value = lerp(from, to, smoothstep(phase));
        return value;
    }

    float getValue() const noexcept { return value; }

private:
    void startSegment() noexcept
    {
        to = rng.nextBipolar();
        segmentLength = 0.5f + rng.nextFloat();
    }

    Random rng;
    float rateHz = 0.1f;
    float segmentLength = 1.0f;
    float phase = 0.0f;
    float from = 0.0f;
    float to = 0.0f;
    float value = 0.0f;
};
}
