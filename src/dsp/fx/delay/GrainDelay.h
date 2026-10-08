#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../core/MathUtil.h"
#include "../../core/Random.h"
#include "../../filters/DcBlocker.h"
#include "../../filters/OnePole.h"

#include <array>

namespace tf::dsp {
class GrainDelay final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kMaxGrains = 24;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return 20.0f; }

    int getActiveGrains() const noexcept;

private:
    struct Grain
    {
        bool active = false;
        float delay = 0.0f;
        float drift = 0.0f;
        float age = 0.0f;
        float invLength = 0.0f;
        float gainL = 1.0f, gainR = 1.0f;
    };

    void spawn() noexcept;

    double fs = 48000.0;
    DelayLine lineL, lineR;
    DcBlocker dcL, dcR;
    OnePole dampL, dampR;
    TanhAdaa loopSatL, loopSatR;
    std::array<Grain, kMaxGrains> grains {};
    Random rng;
    float countdown = 0.0f;
    float timeSamples = 14000.0f, sizeSamples = 5000.0f;
    float density = 10.0f, pitchSpread = 1.0f, jitter = 0.3f;
    float feedback = 0.3f, feedbackTarget = 0.3f;
    float norm = 1.0f, normExponent = 1.0f;
    float smoothCoeff = 0.01f, normCoeff = 0.01f;
};
}
