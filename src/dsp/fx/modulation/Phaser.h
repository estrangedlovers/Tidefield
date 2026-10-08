#pragma once

#include "../Processor.h"

#include <array>

namespace tf::dsp {
class Phaser final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kMaxStages = 12;
    static constexpr int kNumStageChoices = 5;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

private:
    struct Channel
    {
        std::array<float, kMaxStages> state {};
        std::array<float, kMaxStages> out {};
        float tap = 0.0f;
    };

    float processChannel(Channel& ch, float x, float a) noexcept;
    float coefficientFor(float octave) const noexcept;

    double fs = 48000.0;
    std::array<Channel, 2> channels {};
    std::array<float, kNumStageChoices> tapWeight {}, tapTarget {};
    float rateHz = 0.1f, timeScale = 1.0f;
    float depth = 0.7f, depthTarget = 0.7f;
    float feedback = 0.0f, feedbackTarget = 0.0f;
    float centre = 9.0f, centreTarget = 9.0f;
    float stereo = 0.5f, stereoTarget = 0.5f;
    float lfoPhase = 0.0f;
    float smoothCoeff = 0.01f;
};
}
