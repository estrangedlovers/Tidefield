#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../core/MathUtil.h"
#include "../../core/Random.h"
#include "../../filters/DcBlocker.h"
#include "../../filters/OnePole.h"
#include "../../mod/Drift.h"

namespace tf::dsp {
class TapeDelay final : public Processor
{
public:
    static const ProcessorInfo kInfo;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return 20.0f; }

    static float timeMsFrom01(float v) noexcept { return 20.0f * std::pow(100.0f, v); }
    static float feedbackFrom01(float v) noexcept { return 1.1f * v; }
    static float toneHzFrom01(float v) noexcept { return 500.0f * std::pow(32.0f, v); }

private:
    double fs = 48000.0;
    DelayLine lineL, lineR;
    OnePole toneL, toneR, lowCutL, lowCutR;
    DcBlocker dcL, dcR;
    TanhAdaa loopSatL, loopSatR;
    Drift wowDrift;
    float flutterPhase = 0.0f;
    float currentDelay = 4800.0f, targetDelay = 4800.0f, glideCoeff = 0.0002f;
    float feedback = 0.4f, spread = 0.3f, wobble = 0.2f, age = 0.2f;
    float timeScale = 1.0f;
};
}
