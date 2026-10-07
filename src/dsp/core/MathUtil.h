#pragma once

#include <algorithm>
#include <cmath>

namespace tf::dsp {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain(float db) noexcept { return db <= -120.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }

inline float gainToDb(float gain) noexcept { return gain <= 1.0e-6f ? -120.0f : 20.0f * std::log10(gain); }

inline float midiToHz(float note) noexcept { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

inline float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

inline float smoothstep(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/** Equal-power pan law. pan in [-1, 1]; returns {left, right} gains. */
struct PanGains { float left; float right; };

inline PanGains equalPowerPan(float pan) noexcept
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    return { std::cos(angle), std::sin(angle) };
}

/** One-pole coefficient so the filter reaches ~63% of a step in timeSeconds. */
inline float onePoleCoefficient(float timeSeconds, double sampleRate) noexcept
{
    if (timeSeconds <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (timeSeconds * static_cast<float>(sampleRate)));
}

} // namespace tf::dsp
