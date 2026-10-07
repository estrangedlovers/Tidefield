#pragma once

#include <array>
#include <cmath>

namespace tf::dsp {

/** Musical divisions a synced time can snap to, in beats (quarter notes). */
struct BeatDivision
{
    float beats;
    const char* name;
};

inline constexpr std::array<BeatDivision, 10> kBeatDivisions { {
    { 0.25f, "1/16" }, { 0.5f, "1/8" }, { 0.75f, "1/8 dotted" }, { 1.0f / 1.5f, "1/4 triplet" }, { 1.0f, "1/4" },
    { 1.5f, "1/4 dotted" }, { 2.0f, "1/2" }, { 3.0f, "1/2 dotted" }, { 4.0f, "1 bar" }, { 8.0f, "2 bars" },
} };

/** The division nearest (in ratio, so 1/8 vs 1/4 is judged like 1/2 vs 1 bar) to a
    free time, among those no longer than maxSeconds. -1 if none fits. */
inline int nearestDivision(float seconds, float beatSeconds, float maxSeconds) noexcept
{
    int best = -1;
    float bestDistance = 1.0e9f;
    for (int i = 0; i < static_cast<int>(kBeatDivisions.size()); ++i)
    {
        const float t = kBeatDivisions[static_cast<std::size_t>(i)].beats * beatSeconds;
        if (t > maxSeconds || t <= 0.0f)
            continue;
        const float d = std::abs(std::log(t / std::max(seconds, 1.0e-4f)));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

/** A free time locked to the beat when beatSeconds > 0 (otherwise unchanged). */
inline float syncedSeconds(float seconds, float beatSeconds, float maxSeconds) noexcept
{
    if (beatSeconds <= 0.0f)
        return seconds;
    const int i = nearestDivision(seconds, beatSeconds, maxSeconds);
    return i < 0 ? seconds : kBeatDivisions[static_cast<std::size_t>(i)].beats * beatSeconds;
}

} // namespace tf::dsp
