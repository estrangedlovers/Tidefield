#include "Phaser.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kStageChoices[] = { "2", "4", "6", "8", "12" };
constexpr int kStageCounts[] = { 2, 4, 6, 8, 12 };
constexpr float kSweepOctaves = 2.5f;
constexpr float kMaxFeedback = 0.9f;

inline float softLimit(float x) noexcept { return 1.5f * std::tanh(x * (1.0f / 1.5f)); }
}

using Curve = DisplayMap::Curve;

const ProcessorInfo Phaser::kInfo {
    "tf.phaser", "Phaser",
    { { { "Rate", 0.3f, { Curve::Exp, 0.02f, 500.0f, "Hz", 2 } },
        { "Depth", 0.7f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Feedback", 0.75f, { Curve::Linear, -90.0f, 180.0f, "%" } },
        { "Stages", 0.5f, { Curve::Choice, 0.0f, 1.0f, "", 0, kStageChoices, 5 } },
        { "Centre", 0.5f, { Curve::Exp, 100.0f, 40.0f, "Hz" } },
        { "Stereo", 0.5f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    false
};

void Phaser::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    smoothCoeff = onePoleCoefficient(0.03f, fs);
    setControls({ 0.3f, 0.7f, 0.75f, 0.5f, 0.5f, 0.5f }, {});
    reset();
}

void Phaser::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.state.fill(0.0f);
        ch.out.fill(0.0f);
        ch.tap = 0.0f;
    }
    tapWeight = tapTarget;
    depth = depthTarget;
    feedback = feedbackTarget;
    centre = centreTarget;
    stereo = stereoTarget;
    lfoPhase = 0.0f;
}

void Phaser::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    rateHz = kInfo.controls[0].display.value(std::clamp(c[0], 0.0f, 1.0f));
    depthTarget = std::clamp(c[1], 0.0f, 1.0f);
    feedbackTarget = std::clamp(2.0f * c[2] - 1.0f, -1.0f, 1.0f) * kMaxFeedback;
    const int stages = std::clamp(static_cast<int>(c[3] * kNumStageChoices), 0, kNumStageChoices - 1);
    for (int s = 0; s < kNumStageChoices; ++s)
        tapTarget[static_cast<std::size_t>(s)] = s == stages ? 1.0f : 0.0f;
    centreTarget = std::log2(kInfo.controls[4].display.value(std::clamp(c[4], 0.0f, 1.0f)));
    stereoTarget = std::clamp(c[5], 0.0f, 1.0f);
    timeScale = std::clamp(ctx.timeScale, 0.0f, 16.0f);
}

float Phaser::coefficientFor(float octave) const noexcept
{
    const float hz = std::clamp(std::exp2(octave), 20.0f, 0.45f * static_cast<float>(fs));
    const float t = std::tan(kPi * hz / static_cast<float>(fs));
    return (t - 1.0f) / (t + 1.0f);
}

float Phaser::processChannel(Channel& ch, float x, float a) noexcept
{
    float s = softLimit(x + feedback * ch.tap);
    for (std::size_t k = 0; k < kMaxStages; ++k)
    {
        const float y = a * s + ch.state[k];
        ch.state[k] = flushDenormal(s - a * y);
        ch.out[k] = y;
        s = y;
    }
    float tap = 0.0f;
    for (std::size_t j = 0; j < kNumStageChoices; ++j)
        tap += tapWeight[j] * ch.out[static_cast<std::size_t>(kStageCounts[j] - 1)];
    ch.tap = flushDenormal(tap);
    return 0.5f * (x + tap);
}

void Phaser::process(float* left, float* right, int n) noexcept
{
    const float inc = rateHz * timeScale / static_cast<float>(fs);
    const float k = smoothCoeff;
    for (int i = 0; i < n; ++i)
    {
        depth += k * (depthTarget - depth);
        feedback += k * (feedbackTarget - feedback);
        centre += k * (centreTarget - centre);
        stereo += k * (stereoTarget - stereo);
        for (std::size_t j = 0; j < kNumStageChoices; ++j)
            tapWeight[j] += k * (tapTarget[j] - tapWeight[j]);

        float phaseR = lfoPhase + 0.5f * stereo;
        phaseR -= std::floor(phaseR);
        const float swing = depth * kSweepOctaves;
        const float aL = coefficientFor(centre + swing * fastSin01(lfoPhase));
        const float aR = coefficientFor(centre + swing * fastSin01(phaseR));
        left[i] = processChannel(channels[0], left[i], aL);
        right[i] = processChannel(channels[1], right[i], aR);

        lfoPhase += inc;
        lfoPhase -= std::floor(lfoPhase);
    }
}
}
