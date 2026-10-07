#pragma once

#include "../core/ProcessSpec.h"

#include <vector>

namespace tf::dsp {

/** Stereo-linked lookahead peak limiter.

    Gain path: required gain per sample -> sliding minimum over the lookahead window ->
    instant-attack / one-pole release -> box filter over the same window. The box
    average of values that are all <= the required gain at a peak is itself <= that
    gain, so the delayed signal never exceeds the ceiling. A final hard clip at the
    ceiling stays in place as a last line of defence. */
class Limiter
{
public:
    void prepare(const ProcessSpec& spec, float lookaheadMs = 3.0f);
    void reset() noexcept;

    void setCeilingDb(float db) noexcept;
    void setReleaseMs(float ms) noexcept;

    /** In-place processing. */
    void process(float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return window; }
    float getCurrentGain() const noexcept { return lastGain; }

private:
    float pushMin(float g) noexcept;

    double fs = 48000.0;
    int window = 1;
    float ceiling = 0.89f;
    float releaseCoeff = 0.001f;
    float released = 1.0f;
    float lastGain = 1.0f;

    // Delay lines for audio and the box filter (length = window).
    std::vector<float> delayL, delayR, boxBuffer;
    int writeIndex = 0;
    double boxSum = 0.0;

    // Monotonic deque for the sliding minimum: stores (value, expiry index).
    std::vector<float> dequeValue;
    std::vector<long long> dequeIndex;
    int dequeHead = 0;
    int dequeSize = 0;
    long long sampleCounter = 0;
    int resumCounter = 0;
};

} // namespace tf::dsp
