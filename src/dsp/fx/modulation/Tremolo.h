#pragma once

#include "../Processor.h"

#include "../../core/Random.h"
#include "../../mod/Drift.h"

#include <array>

namespace tf::dsp {
class Tremolo final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kNumShapes = 5;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

private:
    struct Channel
    {
        float phase = 0.0f;
        float held = 0.0f;
        float gain = 1.0f;
        Random rng;
    };

    float shapeAt(Channel& ch) const noexcept;

    double fs = 48000.0;
    std::array<Channel, 2> channels {};
    Drift wander;
    int shape = 0;
    float rateHz = 1.0f, timeScale = 1.0f;
    float depth = 0.6f, depthTarget = 0.6f;
    float stereo = 0.0f, stereoTarget = 0.0f;
    float smooth = 0.0f;
    float wanderAmount = 0.0f;
    float smoothCoeff = 0.01f;
};
}
