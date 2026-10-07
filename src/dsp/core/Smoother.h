#pragma once

#include "Denormal.h"
#include "MathUtil.h"

#include <cmath>

namespace tf::dsp {

/** Linear ramp to the target over a fixed time. No overshoot; good for gains. */
class LinearSmoother
{
public:
    void prepare(double sampleRate, float rampSeconds) noexcept
    {
        rampSamples = std::max(1, static_cast<int>(std::lround(rampSeconds * sampleRate)));
        reset(target);
    }

    void reset(float value) noexcept
    {
        current = target = value;
        remaining = 0;
        step = 0.0f;
    }

    void setTarget(float newTarget) noexcept
    {
        if (newTarget == target)
            return;
        target = newTarget;
        remaining = rampSamples;
        step = (target - current) / static_cast<float>(remaining);
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    /** Advances n samples at once and returns the value afterwards. */
    float skip(int n) noexcept
    {
        if (remaining <= 0)
            return current;
        if (n >= remaining)
        {
            current = target;
            remaining = 0;
        }
        else
        {
            current += step * static_cast<float>(n);
            remaining -= n;
        }
        return current;
    }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return remaining > 0; }

private:
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    int remaining = 0;
    int rampSamples = 1;
};

/** Exponential (one-pole) smoother. Optionally runs in the log domain so frequency
    sweeps move evenly in pitch. Snaps to the target once within a small epsilon. */
class OnePoleSmoother
{
public:
    void prepare(double sampleRate, float timeSeconds, bool logDomain) noexcept
    {
        fs = sampleRate;
        tau = timeSeconds;
        useLog = logDomain;
        coefficient = onePoleCoefficient(tau, fs);
        reset(getTarget());
    }

    void reset(float value) noexcept
    {
        state = targetState = toState(value);
    }

    /** Changes the time constant without disturbing the current value. */
    void setTimeConstant(float seconds) noexcept
    {
        if (seconds == tau)
            return;
        tau = seconds;
        coefficient = onePoleCoefficient(tau, fs);
    }

    void setTarget(float value) noexcept { targetState = toState(value); }

    float next() noexcept
    {
        const float before = state;
        state += coefficient * (targetState - state);
        snap(before);
        return fromState(state);
    }

    float skip(int n) noexcept
    {
        if (state == targetState)
            return fromState(state);
        // Closed form of n iterations: state moves by 1 - (1 - c)^n of the gap.
        const float before = state;
        const float k = 1.0f - std::pow(1.0f - coefficient, static_cast<float>(n));
        state += k * (targetState - state);
        snap(before);
        return fromState(state);
    }

    float getCurrent() const noexcept { return fromState(state); }
    float getTarget() const noexcept { return fromState(targetState); }
    bool isSmoothing() const noexcept { return state != targetState; }

private:
    float toState(float v) const noexcept { return useLog ? std::log(std::max(v, 1.0e-6f)) : v; }
    float fromState(float s) const noexcept { return useLog ? std::exp(s) : s; }

    /** Snaps when close enough, or when float precision means the step no longer
        moves the state (long time constants stall a few ulps short otherwise). */
    void snap(float before) noexcept
    {
        const float epsilon = useLog ? 1.0e-5f : 1.0e-6f * std::max(1.0f, std::fabs(targetState));
        if (state == before || std::fabs(targetState - state) < epsilon)
            state = targetState;
    }

    double fs = 48000.0;
    float tau = 0.02f;
    float coefficient = 1.0f;
    float state = 0.0f;
    float targetState = 0.0f;
    bool useLog = false;
};

} // namespace tf::dsp
