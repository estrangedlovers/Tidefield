#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../filters/OnePole.h"

#include <array>

namespace tf::dsp {
class PitchShimmer final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kNumIntervals = 7;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return 20.0f; }

    static float semitonesFrom01(float v) noexcept;

private:
    struct Channel
    {
        DelayLine line, echo;
        OnePole tone, lowCut;
        float echoDelay = 9000.0f;
        float phase = 0.0f;
        float loop = 0.0f;
    };

    float shift(Channel& ch, float ratio) noexcept;

    double fs = 48000.0;
    std::array<Channel, 2> channels;
    float ratio = 2.0f, detune = 0.0f;
    float feedback = 0.0f, feedbackTarget = 0.0f;
    float window = 4000.0f, windowTarget = 4000.0f;
    float smoothCoeff = 0.01f;
};
}
