#include "Saturator.h"

#include "../../core/Denormal.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
constexpr float kMaxBias = 0.8f;
constexpr float kReferenceLevel = 0.25f;
constexpr float kWarmthHz = 220.0f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo Saturator::kInfo {
    "tf.saturator", "Saturator",
    { { { "Drive", 0.33f, { Curve::Linear, 0.0f, 36.0f, "dB", 1 } },
        { "Bias", 0.2f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Tone", 0.7f, { Curve::Exp, 1000.0f, 20.0f, "Hz" } },
        { "Warmth", 0.3f, { Curve::Linear, 0.0f, 9.0f, "dB", 1 } },
        { "Compensate", 1.0f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Output", 0.5f, { Curve::Linear, -12.0f, 24.0f, "dB", 1 } } } },
    false
};

float Saturator::compensationDb(float driveGain, float b) noexcept
{
    const float swing = 0.5f * (std::tanh(kReferenceLevel * driveGain + b) - std::tanh(b - kReferenceLevel * driveGain));
    return gainToDb(kReferenceLevel / std::max(swing, 1.0e-4f));
}

void Saturator::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    for (auto& ch : channels)
    {
        ch.preShelf.prepare(fs);
        ch.postShelf.prepare(fs);
        ch.dc.prepare(fs, 10.0f);
        ch.tone.prepare(fs);
    }
    smoothCoeff = onePoleCoefficient(0.015f, fs);
    setControls({ 0.33f, 0.2f, 0.7f, 0.3f, 1.0f, 0.5f }, {});
    reset();
}

void Saturator::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.preShelf.reset();
        ch.postShelf.reset();
        ch.shaper.reset();
        ch.dc.reset();
        ch.tone.reset();
    }
    drive = driveTarget;
    bias = biasTarget;
    output = outputTarget;
}

void Saturator::setControls(const std::array<float, 6>& c, const ModContext&) noexcept
{
    driveTarget = dbToGain(kInfo.controls[0].display.value(std::clamp(c[0], 0.0f, 1.0f)));
    biasTarget = kMaxBias * std::clamp(c[1], 0.0f, 1.0f);
    const float tone = kInfo.controls[2].display.value(std::clamp(c[2], 0.0f, 1.0f));
    const float warmth = kInfo.controls[3].display.value(std::clamp(c[3], 0.0f, 1.0f));
    for (auto& ch : channels)
    {
        ch.tone.setCutoff(tone);
        ch.preShelf.setLowShelf(kWarmthHz, warmth);
        ch.postShelf.setLowShelf(kWarmthHz, -warmth);
    }
    const float compensate = std::clamp(c[4], 0.0f, 1.0f) * compensationDb(driveTarget, biasTarget);
    outputTarget = dbToGain(compensate + kInfo.controls[5].display.value(std::clamp(c[5], 0.0f, 1.0f)));
}

void Saturator::process(float* left, float* right, int n) noexcept
{
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        drive += smoothCoeff * (driveTarget - drive);
        bias += smoothCoeff * (biasTarget - bias);
        output += smoothCoeff * (outputTarget - output);
        const float offset = std::tanh(bias);
        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            auto& ch = channels[c];
            const float pre = ch.preShelf.process(io[c][i]);
            const float shaped = ch.shaper.process(pre * drive + bias) - offset;
            const float body = ch.postShelf.process(ch.dc.process(shaped));
            io[c][i] = flushDenormal(ch.tone.processLow(body) * output);
        }
    }
}
}
