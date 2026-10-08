#pragma once

#include "../Processor.h"

#include "../../core/MathUtil.h"
#include "../../filters/Biquad.h"
#include "../../filters/DcBlocker.h"
#include "../../filters/OnePole.h"

#include <array>

namespace tf::dsp {
class Saturator final : public Processor
{
public:
    static const ProcessorInfo kInfo;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

    static float compensationDb(float driveGain, float bias) noexcept;

private:
    struct Channel
    {
        Biquad preShelf, postShelf;
        TanhAdaa shaper;
        DcBlocker dc;
        OnePole tone;
    };

    double fs = 48000.0;
    std::array<Channel, 2> channels;
    float drive = 1.0f, driveTarget = 1.0f;
    float bias = 0.0f, biasTarget = 0.0f;
    float output = 1.0f, outputTarget = 1.0f;
    float smoothCoeff = 0.01f;
};
}
