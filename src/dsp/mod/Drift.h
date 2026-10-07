#pragma once

#include "../core/MathUtil.h"
#include "../core/Random.h"

#include <algorithm>

namespace tf::dsp {

/** Slow wandering value in [-1, 1]. Moves between random targets with eased segments
    whose lengths are jittered around 1 / rateHz, so it never sounds periodic.
    Time advances by dt * timeScale, which is how Tide reaches every modulator. */
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

    /** Advances by dtSeconds of (tide-scaled) time and returns the new value. */
    float advance(float dtSeconds) noexcept
    {
        phase += dtSeconds / segmentSeconds;
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
        segmentSeconds = std::max(0.02f, (0.5f + rng.nextFloat()) / rateHz);
    }

    Random rng;
    float rateHz = 0.1f;
    float segmentSeconds = 10.0f;
    float phase = 0.0f;
    float from = 0.0f;
    float to = 0.0f;
    float value = 0.0f;
};

} // namespace tf::dsp
