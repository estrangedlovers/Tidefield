#pragma once

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

/** RBJ-cookbook biquad, transposed direct form II. Coefficients are recomputed only
    when set*() is called, so keep those calls at control rate. */
class Biquad
{
public:
    void prepare(double sampleRate) noexcept
    {
        fs = static_cast<float>(sampleRate);
        reset();
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    void setPeak(float hz, float q, float gainDb) noexcept
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w = omega(hz);
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        setNormalised(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a, 1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setLowShelf(float hz, float gainDb) noexcept
    {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w = omega(hz);
        const float c = std::cos(w);
        const float alpha = std::sin(w) / 2.0f * std::sqrt(2.0f);
        const float sa = 2.0f * std::sqrt(a) * alpha;
        setNormalised(a * ((a + 1) - (a - 1) * c + sa), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - sa),
                      (a + 1) + (a - 1) * c + sa, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - sa);
    }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal(b1 * x - a1 * y + z2);
        z2 = flushDenormal(b2 * x - a2 * y);
        return y;
    }

private:
    float omega(float hz) const noexcept { return kTwoPi * std::clamp(hz, 10.0f, fs * 0.45f) / fs; }

    void setNormalised(float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
    {
        b0 = nb0 / na0;
        b1 = nb1 / na0;
        b2 = nb2 / na0;
        a1 = na1 / na0;
        a2 = na2 / na0;
    }

    float fs = 48000.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace tf::dsp
