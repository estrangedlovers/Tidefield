#pragma once

#include "../core/ProcessSpec.h"

#include <vector>

namespace tf::dsp {

/** Stereo-linked lookahead true-peak limiter.

    Detection looks between samples as well as at them: three points per interval
    are reconstructed with a windowed-sinc interpolator (4x), as in ITU-R BS.1770, so
    the ceiling holds for the signal a DAC rebuilds, not only for its samples (bright
    transients otherwise overshoot by 1-3 dB). The audio is delayed by the
    interpolator's look-ahead so detection and audio stay aligned.

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

    int getLatencySamples() const noexcept { return window + kTruePeakDelay; }
    float getCurrentGain() const noexcept { return lastGain; }

    /** Samples of look-ahead the true-peak interpolator needs (added to the latency). */
    static constexpr int kTruePeakDelay = 4;

private:
    float pushMin(float g) noexcept;
    float truePeak(const float* history) const noexcept;

    double fs = 48000.0;
    int window = 1;
    float ceiling = 0.89f;
    float releaseCoeff = 0.001f;
    float released = 1.0f;
    float lastGain = 1.0f;

    // True-peak detection: the last 9 input samples per channel (h[8] newest; h[4]
    // is the sample entering the limiter), and 3 x 8 sinc taps.
    float histL[9] {}, histR[9] {};
    float phaseTaps[3][8] {};

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
