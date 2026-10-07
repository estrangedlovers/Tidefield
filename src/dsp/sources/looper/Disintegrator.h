#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/DcBlocker.h"
#include "../../filters/OnePole.h"

#include <vector>

namespace tf::dsp {

/** A disintegrating tape loop (after William Basinski's Disintegration Loops).

    Looper-pedal control: record() starts a take; record() again closes the loop at
    that length and plays it; record() again toggles overdub. clear() fades out and
    empties it.

    Every pass rewrites the loop through an erosion chain, so the damage is permanent
    and accumulates: a low-pass and a high-pass narrow the band a little each time,
    a soft saturator thickens it, the level sinks, a trace of hiss settles in, and at
    random moments the "oxide flakes off": a short stretch loses part of its level for
    good. With erosion at 0 the loop repeats unchanged.

    Memory is allocated in prepare() (kMaxSeconds of stereo); realtime-safe after. */
class Disintegrator
{
public:
    static constexpr float kMaxSeconds = 60.0f;
    static constexpr float kEdgeSeconds = 0.008f; // fades at the loop seam

    enum class State : int { Empty = 0, Recording = 1, Playing = 2, Overdubbing = 3, Clearing = 4 };

    struct Params
    {
        float erosion = 0.4f; // 0..1: how much each pass wears the loop
        float flakes = 0.3f;  // 0..1: how often the oxide flakes off
        float overdub = 0.7f; // 0..1: level of new material while overdubbing
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }

    /** The pedal: Empty -> Recording -> Playing <-> Overdubbing. */
    void record() noexcept;
    /** Fades out over 0.5 s and empties the loop. */
    void clear() noexcept;

    /** Records/overdubs inL/inR (may be null for silence) and writes the loop's playback. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    State getState() const noexcept { return state; }
    /** Playhead 0..1 (or the recorded fraction of kMaxSeconds while recording). */
    float getPosition() const noexcept;
    float getLengthSeconds() const noexcept;
    int getPasses() const noexcept { return passes; }

private:
    void closeLoop() noexcept;
    float erode(float x, int ch) noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    std::vector<float> bufL, bufR;
    int capacity = 0;
    int length = 0;   // loop length in samples (0 while empty)
    int pos = 0;      // playhead / record head
    int passes = 0;
    State state = State::Empty;
    float clearGain = 1.0f;
    int edge = 64;

    // Erosion chain, per channel.
    OnePole lowPass[2], highPass[2];
    DcBlocker dc[2];
    // Current flake: a stretch whose level is being permanently reduced.
    int flakeRemaining = 0, flakeLength = 1;
    float flakeDepth = 0.0f;
};

} // namespace tf::dsp
