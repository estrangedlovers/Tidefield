#pragma once

#include <dsp/core/MathUtil.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace tf::engine {
inline constexpr float kFaderFloorDb = -59.9f;

inline float faderGain(float db) noexcept { return db <= kFaderFloorDb ? 0.0f : dsp::dbToGain(db); }

class ChannelStrip
{
public:
    struct Settings
    {
        float levelDb = 0.0f;
        float pan = 0.0f;
        float width = 1.0f;
        float sendADb = -60.0f;
        float sendBDb = -60.0f;
        float gate = 1.0f;
    };

    void update(const Settings& s) noexcept
    {
        previous = target;
        const float level = faderGain(s.levelDb) * std::clamp(s.gate, 0.0f, 1.0f);
        const auto pan = dsp::equalPowerPan(s.pan);
        target.mid = level;
        target.side = level * std::clamp(s.width, 0.0f, 2.0f);
        target.panL = pan.left;
        target.panR = pan.right;
        target.sendA = faderGain(s.sendADb);
        target.sendB = faderGain(s.sendBDb);
    }

    void settle() noexcept { previous = target; }

    bool isSilent() const noexcept { return previous.mid == 0.0f && target.mid == 0.0f; }

    void processAdd(const float* inL, const float* inR, float* outL, float* outR, float* aL, float* aR, float* bL, float* bR,
                    int n, int tickPos, int tickLength, float* stemL = nullptr, float* stemR = nullptr) noexcept
    {
        if (isSilent())
            return;
        const float invTick = 1.0f / static_cast<float>(tickLength);
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(tickPos + i + 1) * invTick;
            const float mid = dsp::lerp(previous.mid, target.mid, t);
            const float side = dsp::lerp(previous.side, target.side, t);
            const float pl = dsp::lerp(previous.panL, target.panL, t);
            const float pr = dsp::lerp(previous.panR, target.panR, t);
            const float sa = dsp::lerp(previous.sendA, target.sendA, t);
            const float sb = dsp::lerp(previous.sendB, target.sendB, t);

            const float m = 0.5f * (inL[i] + inR[i]) * mid;
            const float s = 0.5f * (inL[i] - inR[i]) * side;
            const float l = (m + s) * pl;
            const float r = (m - s) * pr;
            outL[i] += l;
            outR[i] += r;
            if (stemL != nullptr)
            {
                stemL[i] = l;
                stemR[i] = r;
            }
            aL[i] += l * sa;
            aR[i] += r * sa;
            bL[i] += l * sb;
            bR[i] += r * sb;
            peakL = std::max(peakL, std::fabs(l));
            peakR = std::max(peakR, std::fabs(r));
        }
    }

    std::pair<float, float> takePeak() noexcept
    {
        const auto p = std::make_pair(peakL, peakR);
        peakL = peakR = 0.0f;
        return p;
    }

private:
    struct Gains { float mid = 0.0f, side = 0.0f, panL = 1.0f, panR = 1.0f, sendA = 0.0f, sendB = 0.0f; };
    Gains previous, target;
    float peakL = 0.0f, peakR = 0.0f;
};
}
