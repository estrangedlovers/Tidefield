#pragma once

#include "../Processor.h"

#include "../../core/MathUtil.h"

#include <array>

namespace tf::dsp {
class MultimodeFilter final : public Processor
{
public:
    static const ProcessorInfo kInfo;
    static constexpr int kNumModes = 4;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;

private:
    struct Channel
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        TanhAdaa drive;
    };

    float processChannel(Channel& ch, float x, float g, float k, float driveGain, float driveNorm) noexcept;

    double fs = 48000.0;
    std::array<Channel, 2> channels {};
    std::array<float, kNumModes> modeWeight {}, modeTarget {};
    float octave = 0.0f, octaveTarget = 0.0f;
    float k = 1.4f, kTarget = 1.4f;
    float driveDb = 0.0f, driveDbTarget = 0.0f;
    float sweep = 0.0f, sweepTarget = 0.0f;
    float rateHz = 0.2f, timeScale = 1.0f;
    float lfoPhase = 0.0f;
    float smoothCoeff = 0.01f;
};
}
