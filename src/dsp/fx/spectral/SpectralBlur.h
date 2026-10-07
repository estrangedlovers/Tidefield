#pragma once

#include "../Processor.h"

#include "../../core/Fft.h"
#include "../../core/Random.h"

#include <vector>

namespace tf::dsp {

/** Smears sound in time and frequency (2048-point STFT, 4x overlap).

      Blur     each bin's magnitude follows the input with this time constant, so
               attacks melt into washes (up to ~20 s; Freeze holds it)
      Smear    averages neighbouring bins: a chord turns into a coloured band
      Drift    lets phases wander from the input's: from intact to breathy
      Shimmer  adds the spectrum an octave up, a ghostly upper partial layer
      Tone     tilts the spectrum dark or bright
      Freeze   holds the current spectrum indefinitely

    With every control at zero it resynthesises the input unchanged (one window of
    latency, which is not reported: it is a wet texture effect). */
class SpectralBlur final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kOrder = 11;
    static constexpr int kSize = 1 << kOrder;
    static constexpr int kHop = kSize / 4;
    static constexpr int kBins = kSize / 2 + 1;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return 20.0f; }

private:
    struct Channel
    {
        std::vector<float> in, ola, mag, drift;
        std::vector<Fft::Complex> spectrum;
    };
    void hop(Channel& ch) noexcept;
    void updateTilt() noexcept;

    double fs = 48000.0;
    Fft fft;
    Random rng { 77 };
    std::vector<float> window, tilt, scratch;
    Channel chans[2];
    int pos = 0, hopCount = 0;
    float blurSeconds = 0.0f, smear = 0.0f, drift = 0.0f, shimmer = 0.0f, tone = 0.5f, appliedTone = -1.0f;
    bool freeze = false;
    float timeScale = 1.0f;
};

} // namespace tf::dsp
