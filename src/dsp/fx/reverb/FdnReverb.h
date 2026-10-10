#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../core/Random.h"
#include "../../filters/OnePole.h"

#include <array>

namespace tf::dsp {
class FdnReverb final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kLines = 8;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return decaySeconds * 1.5f; }

    static float sizeFrom01(float v) noexcept { return 0.3f + 1.7f * v; }
    static float decayFrom01(float v) noexcept;
    static float dampingFrom01(float v) noexcept;
    static float predelayFrom01(float v) noexcept { return 250.0f * v * v; }

private:
    double fs = 48000.0;
    std::array<DelayLine, kLines> lines;
    std::array<float, kLines> baseDelay {}, gains {}, modPhase {}, modRate {};
    std::array<OnePole, kLines> damping;
    std::array<DelayLine, 4> diffusers;
    std::array<float, 4> diffuserDelay {};
    DelayLine predelayL, predelayR;
    float decaySeconds = 4.0f;
    float size = 1.0f;
    float predelay = 0.0f;
    float modDepth = 0.0f;
    float hold = 0.0f;
    float dampingHz = 6000.0f;
    float timeScale = 1.0f;
    std::array<float, 6> lastControls {};
    bool controlsSet = false;
};
}
