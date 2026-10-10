#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace tf::dsp {
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain(float db) noexcept { return db <= -120.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }

inline float gainToDb(float gain) noexcept { return gain <= 1.0e-6f ? -120.0f : 20.0f * std::log10(gain); }

inline float midiToHz(float note) noexcept { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

inline float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

inline float fastSin01(float phase) noexcept
{
    const float x = phase * 2.0f - 1.0f;
    const float y = 4.0f * x * (1.0f - std::fabs(x));
    const float corrected = 0.225f * (y * std::fabs(y) - y) + y;
    return -corrected;
}

inline float smoothstep(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct PanGains { float left; float right; };

inline PanGains equalPowerPan(float pan) noexcept
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    return { std::cos(angle), std::sin(angle) };
}

inline float onePoleCoefficient(float timeSeconds, double sampleRate) noexcept
{
    if (timeSeconds <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (timeSeconds * static_cast<float>(sampleRate)));
}

class TanhAdaa
{
public:
    void reset() noexcept
    {
        x1 = 0.0;
        f1 = logCosh(0.0);
        steadyBits = kNoSteady;
    }

    float process(float in) noexcept
    {
        const double x = static_cast<double>(in);
        const auto bits = std::bit_cast<std::uint64_t>(x);
        if (bits == std::bit_cast<std::uint64_t>(x1))
        {
            if (bits != steadyBits)
            {
                steadyBits = bits;
                steadyOut = static_cast<float>(std::tanh(0.5 * (x + x1)));
            }
            return steadyOut;
        }
        const double f = logCosh(x);
        const double d = x - x1;
        const double y = std::fabs(d) < 1.0e-4 ? std::tanh(0.5 * (x + x1)) : (f - f1) / d;
        x1 = x;
        f1 = f;
        return static_cast<float>(y);
    }

private:
    static double logCosh(double x) noexcept
    {
        const double a = std::fabs(x);
        return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994530942;
    }

    static constexpr std::uint64_t kNoSteady = 0x7ff8dead0000beefull;
    double x1 = 0.0, f1 = 0.0;
    std::uint64_t steadyBits = kNoSteady;
    float steadyOut = 0.0f;
};
}
