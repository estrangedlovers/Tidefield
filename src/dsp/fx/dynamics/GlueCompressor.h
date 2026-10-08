#pragma once

#include "../Processor.h"

#include "../../filters/Biquad.h"

namespace tf::dsp {
class GlueCompressor final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kNumRatios = 4;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

    float getGainReductionDb() const noexcept { return -reductionDb; }
    static float staticCurveDb(float levelDb, float thresholdDb, float ratio) noexcept;

private:
    double fs = 48000.0;
    Biquad sidechainL, sidechainR;
    float thresholdDb = -20.0f, ratio = 2.0f;
    float attackCoeff = 0.01f, releaseCoeff = 0.001f;
    float makeup = 1.0f, makeupTarget = 1.0f;
    float reductionDb = 0.0f, envelope = 0.0f;
    float smoothCoeff = 0.01f, peakCoeff = 0.01f;
};
}
