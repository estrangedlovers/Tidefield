#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace tf::engine {
struct GuardLimits
{
    int cloudGrains;
    int resonatorModes;
    float droneVoices;
    int bloomVoices;
};

inline constexpr std::array<GuardLimits, 6> kGuardLevels { {
    { 96, 24, 6.0f, 8 },
    { 48, 24, 6.0f, 8 },
    { 32, 12, 6.0f, 8 },
    { 20, 12, 4.0f, 6 },
    { 12, 8, 3.0f, 4 },
    { 6, 6, 2.0f, 3 },
} };
inline constexpr int kMaxGuardLevel = static_cast<int>(kGuardLevels.size()) - 1;

class DegradationPolicy
{
public:
    struct Settings
    {
        float highLoad = 0.80f;
        float lowLoad = 0.50f;
        float overload = 1.0f;
        float stepDownHold = 0.35f;
        float stepUpHold = 6.0f;
        float maxStepUpHold = 60.0f;
        float riseSeconds = 0.05f;
        float fallSeconds = 0.6f;
    };

    DegradationPolicy() = default;
    explicit DegradationPolicy(const Settings& s) : settings(s), upHold(s.stepUpHold) {}

    void reset() noexcept
    {
        level = 0;
        smoothed = 0.0f;
        aboveFor = belowFor = sinceChange = 0.0f;
        upHold = settings.stepUpHold;
        lastWasUp = false;
    }

    bool update(float blockLoad, float blockSeconds) noexcept
    {
        if (! std::isfinite(blockLoad) || blockSeconds <= 0.0f)
            return false;
        const float tc = blockLoad > smoothed ? settings.riseSeconds : settings.fallSeconds;
        smoothed += (blockLoad - smoothed) * (1.0f - std::exp(-blockSeconds / tc));
        sinceChange += blockSeconds;

        if (sinceChange > settings.maxStepUpHold)
            upHold = settings.stepUpHold;

        aboveFor = smoothed > settings.highLoad ? aboveFor + blockSeconds : 0.0f;
        belowFor = smoothed < settings.lowLoad ? belowFor + blockSeconds : 0.0f;

        const bool spike = blockLoad > settings.overload && sinceChange > 0.1f;
        if (level < kMaxGuardLevel && (aboveFor >= settings.stepDownHold || spike))
        {
            if (lastWasUp && sinceChange < 2.0f * upHold)
                upHold = std::min(settings.maxStepUpHold, upHold * 2.0f);
            return change(level + 1, false);
        }
        if (level > 0 && belowFor >= upHold)
            return change(level - 1, true);
        return false;
    }

    int getLevel() const noexcept { return level; }
    float getSmoothedLoad() const noexcept { return smoothed; }
    const GuardLimits& getLimits() const noexcept { return kGuardLevels[static_cast<std::size_t>(level)]; }

private:
    bool change(int newLevel, bool up) noexcept
    {
        level = std::clamp(newLevel, 0, kMaxGuardLevel);
        aboveFor = belowFor = sinceChange = 0.0f;
        lastWasUp = up;
        return true;
    }

    Settings settings;
    int level = 0;
    float smoothed = 0.0f;
    float aboveFor = 0.0f, belowFor = 0.0f, sinceChange = 0.0f;
    float upHold = 6.0f;
    bool lastWasUp = false;
};
}
