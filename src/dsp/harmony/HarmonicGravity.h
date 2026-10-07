#pragma once

#include "Scale.h"

#include <algorithm>

namespace tf::dsp {

/** Global key that pitched sources pull toward.

    A key change does not move every voice at once. Each voice has a stable seed in
    [0, 1); it switches to the new scale when the crossfade progress passes its seed,
    so the harmony migrates voice by voice over `morphSeconds`. Sources glide each
    voice to its new pitch. Time advances with Tide. */
class HarmonicGravity
{
public:
    void setTarget(const Scale& s) noexcept
    {
        if (s.mask == target.mask && s.root == target.root)
            return;
        // A change during a morph starts from wherever each voice currently is: voices
        // already migrated keep the target as their old scale.
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

    /** The scale a given voice currently follows. */
    const Scale& scaleFor(float voiceSeed01) const noexcept { return voiceSeed01 < progress ? target : previous; }

    /** Pulls `note` toward the voice's scale: amount 0 leaves it, 1 snaps it. */
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

} // namespace tf::dsp
