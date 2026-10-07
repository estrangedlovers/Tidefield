#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../filters/DcBlocker.h"
#include "../medium/Medium.h"

#include <array>

namespace tf::dsp {

/** An echo whose every repeat is re-recorded through a Medium (cassette, vinyl or
    noisy sampler), so the tail wears out the way a dubbed-off tape does: each
    generation hissier, wobblier and duller than the last.

    The loop is processed in short chunks so the Medium (which works on blocks) sits
    inside the feedback path; the Medium's own latency is taken out of the delay time
    so the echo lands where Time says. Feedback is bounded by a saturator. */
class WornEcho final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kChunk = 32;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    float getTailSeconds() const noexcept override { return 30.0f; }

private:
    void processChunk(float* left, float* right, int n) noexcept;

    double fs = 48000.0;
    DelayLine lineL, lineR;
    Medium medium;
    DcBlocker dcL, dcR;
    std::array<float, kChunk> echoL {}, echoR {};
    float currentDelay = 0.0f, targetDelay = 0.0f, glide = 0.0002f;
    float feedback = 0.5f, spread = 0.3f;
};

} // namespace tf::dsp
