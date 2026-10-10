#pragma once

#include "ParamRegistry.h"

#include <dsp/core/Smoother.h>

#include <array>
#include <cstdint>
#include <vector>

namespace tf::engine {
class ParamState
{
public:
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
        mod.assign(n, 0.0f);
        numModded = 0;
        numLastModded = 0;
        moving.assign(n, 0);
        movingList.assign(n, 0);
        numMoving = 0;

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
            markMoving(static_cast<ParamIndex>(i));
        }
    }

    void setTarget(ParamIndex i, float plain) noexcept
    {
        if (i >= smoothers.size())
            return;
        smoothers[i].setTarget(specs->spec(i).clamp(plain));
        markMoving(i);
    }

    void snap(ParamIndex i, float plain) noexcept
    {
        if (i >= smoothers.size())
            return;
        const float v = specs->spec(i).clamp(plain);
        smoothers[i].reset(v);
        prev[i] = cur[i] = v;
        markMoving(i);
    }

    static constexpr int kMaxModulated = 96;

    void clearModulation() noexcept
    {
        for (int k = 0; k < numModded; ++k)
            mod[modded[static_cast<std::size_t>(k)]] = 0.0f;
        numModded = 0;
    }

    void addModulation(ParamIndex i, float normalisedOffset) noexcept
    {
        if (i >= mod.size() || normalisedOffset == 0.0f || (specs->spec(i).flags & ParamFlag::kDiscrete) != 0)
            return;
        if (mod[i] == 0.0f)
        {
            if (numModded >= kMaxModulated)
                return;
            modded[static_cast<std::size_t>(numModded++)] = i;
        }
        mod[i] += normalisedOffset;
        if (mod[i] == 0.0f)
            mod[i] = 1.0e-9f;
    }

    void advance(int numSamples) noexcept
    {
        for (int k = 0; k < numLastModded; ++k)
            markMoving(lastModded[static_cast<std::size_t>(k)]);
        for (int k = 0; k < numModded; ++k)
            markMoving(modded[static_cast<std::size_t>(k)]);
        lastModded = modded;
        numLastModded = numModded;
        std::size_t kept = 0;
        for (std::size_t k = 0; k < numMoving; ++k)
        {
            const auto i = movingList[k];
            prev[i] = cur[i];
            cur[i] = smoothers[i].skip(numSamples);
            if (smoothers[i].isSmoothing() || prev[i] != cur[i])
                movingList[kept++] = i;
            else
                moving[i] = 0;
        }
        numMoving = kept;
        for (int k = 0; k < numModded; ++k)
        {
            const auto i = modded[static_cast<std::size_t>(k)];
            const auto& s = specs->spec(i);
            cur[i] = s.fromNormalised(s.toNormalised(cur[i]) + mod[i]);
        }
    }

    void markMoving(ParamIndex i) noexcept
    {
        if (moving[i] != 0)
            return;
        moving[i] = 1;
        movingList[numMoving++] = i;
    }

    float modulation(ParamIndex i) const noexcept { return mod[i]; }

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
        bool isSmoothing() const noexcept { return kind == Smoothing::Linear ? linear.isSmoothing() : onePole.isSmoothing(); }
        void setTarget(float v) noexcept { kind == Smoothing::Linear ? linear.setTarget(v) : onePole.setTarget(v); }
        float skip(int n) noexcept { return kind == Smoothing::Linear ? linear.skip(n) : onePole.skip(n); }
        float target() const noexcept { return kind == Smoothing::Linear ? linear.getTarget() : onePole.getTarget(); }
    };

    const ParamRegistry* specs = nullptr;
    std::vector<Smoother> smoothers;
    std::vector<float> prev, cur, mod;
    std::array<ParamIndex, kMaxModulated> modded {}, lastModded {};
    int numModded = 0, numLastModded = 0;
    std::vector<std::uint8_t> moving;
    std::vector<ParamIndex> movingList;
    std::size_t numMoving = 0;

};
}
