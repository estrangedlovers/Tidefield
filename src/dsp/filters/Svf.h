#pragma once

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

/** Topology-preserving state-variable filter (Simper/Zavalishin). Stable under fast
    modulation, which matters because every filter here is drifting all the time. */
class Svf
{
public:
    struct Outputs { float low; float band; float high; };

    void prepare(double sampleRate) noexcept
    {
        fs = static_cast<float>(sampleRate);
        reset();
        setCutoff(cutoff, resonance);
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** cutoffHz is clamped below Nyquist; resonance in [0, 1) maps to Q 0.5..~25. */
    void setCutoff(float cutoffHz, float resonance01) noexcept
    {
        cutoff = std::clamp(cutoffHz, 10.0f, fs * 0.49f);
        resonance = std::clamp(resonance01, 0.0f, 0.99f);
        const float q = 0.5f / (1.0f - resonance * 0.98f);
        g = std::tan(kPi * cutoff / fs);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    Outputs process(float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal(2.0f * v1 - ic1);
        ic2 = flushDenormal(2.0f * v2 - ic2);
        return { v2, v1, x - k * v1 - v2 };
    }

    float processLow(float x) noexcept { return process(x).low; }

private:
    float fs = 48000.0f;
    float cutoff = 1000.0f;
    float resonance = 0.0f;
    float g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

} // namespace tf::dsp
