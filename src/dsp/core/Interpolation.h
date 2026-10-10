#pragma once

#include <cstddef>
#include <cstdint>

namespace tf::dsp {
inline float hermite(float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c = 0.5f * (x1 - xm1);
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + 0.5f * (x2 - x0);
    const float b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}

inline float readHermite(const float* data, std::size_t size, double position) noexcept
{
    if (position < 0.0 || size < 2)
        return 0.0f;
    const auto whole = static_cast<std::int64_t>(position);
    const auto i = static_cast<std::size_t>(whole);
    if (i >= size)
        return 0.0f;
    const float t = static_cast<float>(position - static_cast<double>(whole));
    const float xm1 = i > 0 ? data[i - 1] : 0.0f;
    const float x0 = data[i];
    const float x1 = i + 1 < size ? data[i + 1] : 0.0f;
    const float x2 = i + 2 < size ? data[i + 2] : 0.0f;
    return hermite(xm1, x0, x1, x2, t);
}
}
