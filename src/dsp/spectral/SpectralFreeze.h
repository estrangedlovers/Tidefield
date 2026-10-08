#pragma once

#include "../core/Fft.h"
#include "../core/ProcessSpec.h"
#include "../core/Random.h"

#include <vector>

namespace tf::dsp {
class SpectralFreeze
{
public:
    static constexpr int kOrder = 12;
    static constexpr int kSize = 1 << kOrder;
    static constexpr int kHop = kSize / 4;
    static constexpr int kBins = kSize / 2 + 1;

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;

    void setFrozen(bool frozen) noexcept;
    void setReleaseSeconds(float s) noexcept { releaseSeconds = s < 0.05f ? 0.05f : s; }
    void setDrift(float d) noexcept { drift = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }

    void process(const float* in, float* outL, float* outR, int numSamples) noexcept;

    bool isFrozen() const noexcept { return frozen; }
    float getGain() const noexcept { return gain; }

private:
    void analyse() noexcept;
    void synthesise() noexcept;

    ProcessSpec spec;
    Fft fft;
    Random rng;
    std::vector<float> window;
    std::vector<float> inRing;
    std::vector<Fft::Complex> work;
    std::vector<float> avgMag, frozenMag;
    std::vector<float> rotLRe, rotLIm, rotRRe, rotRIm;
    std::vector<float> olaL, olaR;
    int inPos = 0;
    int hopCount = 0;
    int olaPos = 0;
    bool frozen = false;
    bool haveSpectrum = false;
    float gain = 0.0f;
    float releaseSeconds = 2.0f;
    float drift = 0.35f;
};
}
