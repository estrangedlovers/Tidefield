#pragma once

#include "Scale.h"

#include <algorithm>

namespace tf::dsp {
class HarmonicGravity
{
public:
    void setTarget(const Scale& s) noexcept
    {
        if (s.mask == target.mask && s.root == target.root)
            return;
        previous = progress >= 0.5f ? target : previous;
        target = s;
        progress = 0.0f;
    }

    void snapTo(const Scale& s) noexcept
    {
        previous = target = s;
        progress = 1.0f;
    }

    void setMorphSeconds(float seconds) noexcept { morphSeconds = std::max(0.05f, seconds); }

    void advance(float dtSeconds) noexcept
    {
        if (progress < 1.0f)
            progress = std::min(1.0f, progress + dtSeconds / morphSeconds);
    }

    const Scale& scaleFor(float voiceSeed01) const noexcept { return voiceSeed01 < progress ? target : previous; }

    float quantize(float note, float voiceSeed01, float amount) const noexcept
    {
        if (amount <= 0.0f)
            return note;
        const float snapped = scaleFor(voiceSeed01).nearest(note);
        return note + (snapped - note) * std::min(amount, 1.0f);
    }

    const Scale& getTarget() const noexcept { return target; }
    float getProgress() const noexcept { return progress; }
    bool isMorphing() const noexcept { return progress < 1.0f; }

private:
    Scale previous, target;
    float progress = 1.0f;
    float morphSeconds = 8.0f;
};
}
