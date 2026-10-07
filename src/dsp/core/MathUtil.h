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

/** sin(2*pi*phase) for phase in [0, 1). Parabolic approximation with one correction
    step: worst-case error ~0.1% of full scale, a few multiplies, no transcendental.
    Use for oscillators and modulators where std::sin per sample would dominate. */
inline float fastSin01(float phase) noexcept
{
    // Map to [-pi, pi] then apply Bhaskara-style parabola y = 4x(1-|x|) in units of pi.
    const float x = phase * 2.0f - 1.0f;                   // -1..1
    const float y = 4.0f * x * (1.0f - std::fabs(x));       // parabola, sign-correct for -sin
    const float corrected = 0.225f * (y * std::fabs(y) - y) + y;
    return -corrected;
}

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

/** tanh with first-order antiderivative anti-aliasing (Parker, Zavalishin, Le Bivic
    2016). The output is the average of tanh over the segment between successive
    inputs, (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) with F = log cosh, which
    suppresses the aliasing a plain tanh produces when driven hard, at the cost of a
    half-sample delay. One per channel; reset() with the filter states. */
class TanhAdaa
{
public:
    void reset() noexcept
    {
        x1 = 0.0;
        f1 = logCosh(0.0);
    }

    float process(float in) noexcept
    {
        const double x = static_cast<double>(in);
        const double f = logCosh(x);
        const double d = x - x1;
        // Nearly equal inputs: the difference quotient is ill-conditioned, use the
        // midpoint instead (the same value in the limit).
        const double y = std::fabs(d) < 1.0e-4 ? std::tanh(0.5 * (x + x1)) : (f - f1) / d;
        x1 = x;
        f1 = f;
        return static_cast<float>(y);
    }

private:
    static double logCosh(double x) noexcept
    {
        const double a = std::fabs(x);
        return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994530942; // stable for any |x|
    }

    double x1 = 0.0, f1 = 0.0;
};

} // namespace tf::dsp
