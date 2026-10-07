#pragma once

#include <dsp/core/MathUtil.h>

#include <algorithm>

namespace tf::engine {

/** Level, pan and stereo width for one source. Gains are computed per control tick
    and ramped per sample. Sends and inserts arrive in phase 3. */
class ChannelStrip
{
public:
    struct Settings
    {
        float levelDb = 0.0f;
        float pan = 0.0f;    // -1..1
        float width = 1.0f;  // 0 = mono, 1 = as-is, 2 = exaggerated
    };

    /** Call once per control tick with the smoothed settings. */
    void update(const Settings& s) noexcept
    {
        previous = target;
        const float level = dsp::dbToGain(s.levelDb);
        const auto pan = dsp::equalPowerPan(s.pan);
        // Width is applied on mid/side. Equal-power pan law: -3 dB at centre, 0 dB at
        // the edges, the convention every mixer user expects when summing sources.
        target.mid = level;
        target.side = level * std::clamp(s.width, 0.0f, 2.0f);
        target.panL = pan.left;
        target.panR = pan.right;
    }

    /** Snaps ramps to the current target (after prepare or reset). */
    void settle() noexcept { previous = target; }

    /** Adds the processed source into the destination buffers.
        tickPos/tickLength place this chunk inside the current control tick. */
    void processAdd(const float* inL, const float* inR, float* outL, float* outR, int n, int tickPos, int tickLength) const noexcept
    {
        const float invTick = 1.0f / static_cast<float>(tickLength);
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(tickPos + i + 1) * invTick;
            const float mid = dsp::lerp(previous.mid, target.mid, t);
            const float side = dsp::lerp(previous.side, target.side, t);
            const float pl = dsp::lerp(previous.panL, target.panL, t);
            const float pr = dsp::lerp(previous.panR, target.panR, t);

            const float m = 0.5f * (inL[i] + inR[i]) * mid;
            const float s = 0.5f * (inL[i] - inR[i]) * side;
            outL[i] += (m + s) * pl;
            outR[i] += (m - s) * pr;
        }
    }

private:
    struct Gains { float mid = 0.0f, side = 0.0f, panL = 1.0f, panR = 1.0f; };
    Gains previous, target;
};

} // namespace tf::engine
