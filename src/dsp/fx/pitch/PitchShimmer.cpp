#include "PitchShimmer.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kIntervalChoices[] = { "-12 st", "-5 st", "+5 st", "+7 st", "+12 st", "+19 st", "+24 st" };
constexpr float kIntervals[] = { -12.0f, -5.0f, 5.0f, 7.0f, 12.0f, 19.0f, 24.0f };
constexpr float kMaxWindowMs = 200.0f;
constexpr float kMinDelay = 2.0f;
constexpr float kDetuneCents = 30.0f;
constexpr float kEchoMs[] = { 150.0f, 190.0f };

inline float softLimit(float x) noexcept { return 1.2f * std::tanh(x * (1.0f / 1.2f)); }
}

using Curve = DisplayMap::Curve;

const ProcessorInfo PitchShimmer::kInfo {
    "tf.pitchShimmer", "Pitch Shimmer",
    { { { "Interval", 4.5f / 7.0f, { Curve::Choice, 0.0f, 1.0f, "", 0, kIntervalChoices, 7 } },
        { "Shimmer", 0.5f, { Curve::Linear, 0.0f, 95.0f, "%" } },
        { "Tone", 0.6f, { Curve::Exp, 1000.0f, 16.0f, "Hz" } },
        { "Size", 0.5f, { Curve::Exp, 30.0f, kMaxWindowMs / 30.0f, "ms" } },
        { "Detune", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Low Cut", 0.4f, { Curve::Exp, 20.0f, 25.0f, "Hz" } } } },
    false
};

float PitchShimmer::semitonesFrom01(float v) noexcept
{
    return kIntervals[std::clamp(static_cast<int>(v * kNumIntervals), 0, kNumIntervals - 1)];
}

void PitchShimmer::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>(kMaxWindowMs * 0.001 * fs) + 16;
    for (std::size_t c = 0; c < channels.size(); ++c)
    {
        auto& ch = channels[c];
        ch.echoDelay = kEchoMs[c] * 0.001f * static_cast<float>(fs);
        ch.echo.prepare(static_cast<std::size_t>(ch.echoDelay) + 16);
        ch.line.prepare(maxSamples);
        ch.tone.prepare(fs);
        ch.lowCut.prepare(fs);
    }
    smoothCoeff = onePoleCoefficient(0.05f, fs);
    setControls({ 4.5f / 7.0f, 0.5f, 0.6f, 0.5f, 0.3f, 0.4f }, {});
    reset();
}

void PitchShimmer::reset() noexcept
{
    for (std::size_t c = 0; c < channels.size(); ++c)
    {
        auto& ch = channels[c];
        ch.line.reset();
        ch.echo.reset();
        ch.tone.reset();
        ch.lowCut.reset();
        ch.phase = c == 0 ? 0.0f : 0.37f;
        ch.loop = 0.0f;
    }
    feedback = feedbackTarget;
    window = windowTarget;
}

void PitchShimmer::setControls(const std::array<float, 6>& c, const ModContext&) noexcept
{
    ratio = std::exp2(semitonesFrom01(c[0]) / 12.0f);
    feedbackTarget = 0.95f * std::clamp(c[1], 0.0f, 1.0f);
    const float tone = kInfo.controls[2].display.value(std::clamp(c[2], 0.0f, 1.0f));
    const float maxWindow = static_cast<float>(channels[0].line.capacity()) - kMinDelay - 4.0f;
    windowTarget = std::clamp(kInfo.controls[3].display.value(std::clamp(c[3], 0.0f, 1.0f)) * 0.001f * static_cast<float>(fs), 64.0f, maxWindow);
    detune = std::clamp(c[4], 0.0f, 1.0f);
    const float lowCut = kInfo.controls[5].display.value(std::clamp(c[5], 0.0f, 1.0f));
    for (auto& ch : channels)
    {
        ch.tone.setCutoff(tone);
        ch.lowCut.setCutoff(lowCut);
    }
}

float PitchShimmer::shift(Channel& ch, float r) noexcept
{
    float p2 = ch.phase + 0.5f;
    p2 -= std::floor(p2);
    float q = ch.phase + 0.25f;
    q -= std::floor(q);
    const float g1 = 0.5f - 0.5f * fastSin01(q);
    const float y = g1 * ch.line.read(kMinDelay + ch.phase * window) + (1.0f - g1) * ch.line.read(kMinDelay + p2 * window);
    ch.phase += (1.0f - r) / window;
    ch.phase -= std::floor(ch.phase);
    return y;
}

void PitchShimmer::process(float* left, float* right, int n) noexcept
{
    const float spread = std::exp2(detune * kDetuneCents / 1200.0f);
    const float ratios[2] = { ratio * spread, ratio / spread };
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        feedback += smoothCoeff * (feedbackTarget - feedback);
        window += smoothCoeff * (windowTarget - window);
        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            auto& ch = channels[c];
            const float shifted = shift(ch, ratios[c]);
            ch.loop = flushDenormal(softLimit(ch.tone.processLow(ch.echo.read(ch.echoDelay))));
            ch.echo.push(shifted);
            ch.line.push(flushDenormal(softLimit(ch.lowCut.processHigh(io[c][i] + feedback * ch.loop))));
            io[c][i] = shifted;
        }
    }
}
}
