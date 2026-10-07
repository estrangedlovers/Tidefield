#pragma once

#include "ParamRegistry.h"

#include <dsp/core/Smoother.h>

#include <vector>

namespace tf::engine {

/** Audio-thread-owned parameter values. Targets arrive from ControlEvents; every
    control tick the smoothers advance and consumers read the smoothed values.
    `previous()` and `current()` bracket the tick so per-sample consumers can
    interpolate across it. */
class ParamState
{
public:
    /** Re-preparing (for example after an audio device change) keeps the current
        targets, so the performer's settings survive a sample-rate switch. */
    void prepare(const ParamRegistry& registry, double sampleRate)
    {
        const auto n = registry.size();
        const bool keepValues = specs == &registry && smoothers.size() == n;
        std::vector<float> kept;
        if (keepValues)
            for (std::size_t i = 0; i < n; ++i)
                kept.push_back(smoothers[i].target());

        specs = &registry;
        smoothers.assign(n, {});
        prev.assign(n, 0.0f);
        cur.assign(n, 0.0f);

        for (std::size_t i = 0; i < n; ++i)
        {
            const auto& s = registry.spec(static_cast<ParamIndex>(i));
            auto& sm = smoothers[i];
            sm.kind = s.smoothing;
            if (sm.kind == Smoothing::Linear)
                sm.linear.prepare(sampleRate, s.smoothingSeconds);
            else
                sm.onePole.prepare(sampleRate, s.smoothingSeconds, sm.kind == Smoothing::LogExponential);
            const float initial = keepValues ? kept[i] : s.defaultValue;
            sm.reset(initial);
            prev[i] = cur[i] = initial;
        }
    }

    /** Sets a target (clamped). Audio thread only. */
    void setTarget(ParamIndex i, float plain) noexcept
    {
        if (i >= smoothers.size())
            return;
        smoothers[i].setTarget(specs->spec(i).clamp(plain));
    }

    /** Jumps straight to a value with no smoothing (session load, reset). */
    void snap(ParamIndex i, float plain) noexcept
    {
        if (i >= smoothers.size())
            return;
        const float v = specs->spec(i).clamp(plain);
        smoothers[i].reset(v);
        prev[i] = cur[i] = v;
    }

    /** Advances every parameter by one control tick of numSamples. */
    void advance(int numSamples) noexcept
    {
        for (std::size_t i = 0; i < smoothers.size(); ++i)
        {
            prev[i] = cur[i];
            cur[i] = smoothers[i].skip(numSamples);
        }
    }

    float current(P p) const noexcept { return cur[idx(p)]; }
    float previous(P p) const noexcept { return prev[idx(p)]; }
    float current(ParamIndex i) const noexcept { return cur[i]; }
    float target(ParamIndex i) const noexcept { return smoothers[i].target(); }

private:
    struct Smoother
    {
        Smoothing kind = Smoothing::Linear;
        dsp::LinearSmoother linear;
        dsp::OnePoleSmoother onePole;

        void reset(float v) noexcept { kind == Smoothing::Linear ? linear.reset(v) : onePole.reset(v); }
        void setTarget(float v) noexcept { kind == Smoothing::Linear ? linear.setTarget(v) : onePole.setTarget(v); }
        float skip(int n) noexcept { return kind == Smoothing::Linear ? linear.skip(n) : onePole.skip(n); }
        float target() const noexcept { return kind == Smoothing::Linear ? linear.getTarget() : onePole.getTarget(); }
    };

    const ParamRegistry* specs = nullptr;
    std::vector<Smoother> smoothers;
    std::vector<float> prev, cur;
};

} // namespace tf::engine
