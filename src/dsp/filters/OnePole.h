#pragma once

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

/** TPT one-pole lowpass with a matching highpass output. */
class OnePole
{
public:
    void prepare(double sampleRate) noexcept
    {
        fs = static_cast<float>(sampleRate);
        setCutoff(cutoff);
        reset();
    }

    void reset(float value = 0.0f) noexcept { s = value; }

    void setCutoff(float hz) noexcept
    {
        cutoff = std::clamp(hz, 1.0f, fs * 0.49f);
        const float g = std::tan(kPi * cutoff / fs);
        G = g / (1.0f + g);
    }

    float processLow(float x) noexcept
    {
        const float v = (x - s) * G;
        const float y = v + s;
        s = flushDenormal(y + v);
        return y;
    }

    float processHigh(float x) noexcept { return x - processLow(x); }

private:
    float fs = 48000.0f;
    float cutoff = 1000.0f;
    float G = 0.0f;
    float s = 0.0f;
};

} // namespace tf::dsp
