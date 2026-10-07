#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../core/Random.h"
#include "../../filters/OnePole.h"

#include <array>

namespace tf::dsp {

/** 8-line feedback delay network with a Householder matrix, input diffusion,
    modulated delay lines and per-line damping. Tuned for long, smooth ambient tails.

    `hold` crossfades into an infinite freeze: input is muted, feedback becomes
    exactly lossless (Householder is orthogonal) and damping is bypassed, so the tail
    sustains indefinitely at a constant level. Without hold, feedback gain is always
    below 1 and the tail decays to exact zero. */
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

    // Mapping from controls, public for tests and UI formatting.
    static float sizeFrom01(float v) noexcept { return 0.3f + 1.7f * v; }               // delay scale
    static float decayFrom01(float v) noexcept;                                          // seconds
    static float dampingFrom01(float v) noexcept;                                        // Hz
    static float predelayFrom01(float v) noexcept { return 250.0f * v * v; }             // ms

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
};

} // namespace tf::dsp
