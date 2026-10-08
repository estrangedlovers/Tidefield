#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../core/MathUtil.h"
#include "../../core/Random.h"
#include "../../filters/Biquad.h"
#include "../../mod/Drift.h"

#include <array>

namespace tf::dsp {
class LoFi final : public Processor
{
public:
    static const ProcessorInfo kInfo;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

private:
    struct Channel
    {
        DelayLine line;
        TanhAdaa shaper;
        Biquad filter;
        float held = 0.0f;
    };

    double fs = 48000.0;
    std::array<Channel, 2> channels;
    Random noiseRng;
    Drift wowDrift;
    float holdPhase = 0.0f;
    float flutterPhase = 0.0f;
    float bits = 12.0f, bitsTarget = 12.0f;
    float rateLog = 0.0f, rateLogTarget = 0.0f;
    float noise = 0.0f, noiseTarget = 0.0f;
    float wow = 0.0f, wowTarget = 0.0f;
    float drive = 1.0f, driveTarget = 1.0f;
    float timeScale = 1.0f;
    float smoothCoeff = 0.01f;
};
}
