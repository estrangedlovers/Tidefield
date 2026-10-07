#pragma once

#include "../core/Fft.h"
#include "../core/ProcessSpec.h"
#include "../core/Random.h"

#include <vector>

namespace tf::dsp {

/** Holds a sound forever as a spectral pad: "freeze the cello note".

    The input is analysed continuously (4096-point STFT, 4x overlap, magnitudes
    averaged over the last few frames). Freezing copies that averaged spectrum; from
    then on every hop resynthesises it with phases that advance at each bin's centre
    frequency plus a little random drift, separately for left and right, so the pad
    is steady in colour but alive and wide rather than a looping grain.

    The pad fades in over 0.3 s and out over `releaseSeconds` after unfreezing.
    Pre-allocated in prepare(); realtime-safe afterwards. Output latency: one hop. */
class SpectralFreeze
{
public:
    static constexpr int kOrder = 12;
    static constexpr int kSize = 1 << kOrder;
    static constexpr int kHop = kSize / 4;
    static constexpr int kBins = kSize / 2 + 1;

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    /** Rising edge captures the current spectrum; falling edge releases the pad. */
    void setFrozen(bool frozen) noexcept;
    void setReleaseSeconds(float s) noexcept { releaseSeconds = s < 0.05f ? 0.05f : s; }
    /** 0 keeps phases locked to each bin (glassy), 1 drifts freely (breathy). */
    void setDrift(float d) noexcept { drift = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }

    /** Reads mono input, writes the pad (overwrites outL/outR). */
    void process(const float* in, float* outL, float* outR, int numSamples) noexcept;

    bool isFrozen() const noexcept { return frozen; }
    /** 0..1 pad gain, for telemetry. */
    float getGain() const noexcept { return gain; }

private:
    void analyse() noexcept;
    void synthesise() noexcept;

    ProcessSpec spec;
    Fft fft;
    Random rng;
    std::vector<float> window;
    std::vector<float> inRing;            // last kSize input samples
    std::vector<Fft::Complex> work;
    std::vector<float> avgMag, frozenMag;
    std::vector<float> rotLRe, rotLIm, rotRRe, rotRIm; // per-bin unit phasors
    std::vector<float> olaL, olaR;        // overlap-add accumulators (kSize)
    int inPos = 0;                        // write position in inRing
    int hopCount = 0;
    int olaPos = 0;                       // read position in the OLA rings
    bool frozen = false;
    bool haveSpectrum = false;
    float gain = 0.0f;
    float releaseSeconds = 2.0f;
    float drift = 0.35f;
};

} // namespace tf::dsp
