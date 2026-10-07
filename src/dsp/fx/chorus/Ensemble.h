#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../filters/OnePole.h"

namespace tf::dsp {

/** String-machine ensemble (Solina / bucket-brigade style): several short delay
    taps per side, each swept by a slow LFO and a fast vibrato with evenly spaced
    phases, so the sum shimmers without obvious pitch wobble. Wet only; the slot's
    mix sets the blend. */
class Ensemble final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kMaxVoices = 6;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

private:
    double fs = 48000.0;
    DelayLine lineL, lineR;
    OnePole toneL, toneR;
    float slowPhase = 0.0f, fastPhase = 0.0f;
    float rateHz = 0.5f, depth = 0.6f, vibrato = 0.3f, spread = 0.8f;
    int voices = 3;
    float timeScale = 1.0f;
};

} // namespace tf::dsp
