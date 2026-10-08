#pragma once

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <cmath>

namespace tf::dsp {
class DcBlocker
{
public:
    void prepare(double sampleRate, float cutoffHz = 8.0f) noexcept
    {
        r = std::exp(-kTwoPi * cutoffHz / static_cast<float>(sampleRate));
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process(float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = flushDenormal(y);
        return y;
    }

private:
    float r = 0.999f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};
}
