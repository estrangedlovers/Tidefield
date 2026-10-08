#include "GlueCompressor.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kRatioChoices[] = { "1.5:1", "2:1", "4:1", "10:1" };
constexpr float kRatios[] = { 1.5f, 2.0f, 4.0f, 10.0f };
constexpr float kKneeDb = 6.0f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo GlueCompressor::kInfo {
    "tf.compressor", "Glue Compressor",
    { { { "Threshold", 0.6f, { Curve::Linear, -48.0f, 48.0f, "dB", 1 } },
        { "Ratio", 0.375f, { Curve::Choice, 0.0f, 1.0f, "", 0, kRatioChoices, 4 } },
        { "Attack", 0.6f, { Curve::Exp, 0.1f, 300.0f, "ms", 1 } },
        { "Release", 0.5f, { Curve::Exp, 50.0f, 40.0f, "ms" } },
        { "Makeup", 0.0f, { Curve::Linear, 0.0f, 18.0f, "dB", 1 } },
        { "SC High-pass", 0.3f, { Curve::Exp, 20.0f, 15.0f, "Hz" } } } },
    false
};

float GlueCompressor::staticCurveDb(float levelDb, float threshold, float r) noexcept
{
    const float over = levelDb - threshold;
    const float slope = 1.0f / r - 1.0f;
    if (over <= -0.5f * kKneeDb)
        return 0.0f;
    if (over < 0.5f * kKneeDb)
    {
        const float x = over + 0.5f * kKneeDb;
        return slope * x * x / (2.0f * kKneeDb);
    }
    return slope * over;
}

void GlueCompressor::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    sidechainL.prepare(fs);
    sidechainR.prepare(fs);
    smoothCoeff = onePoleCoefficient(0.02f, fs);
    peakCoeff = onePoleCoefficient(0.02f, fs);
    setControls({ 0.6f, 0.375f, 0.6f, 0.5f, 0.0f, 0.3f }, {});
    reset();
}

void GlueCompressor::reset() noexcept
{
    sidechainL.reset();
    sidechainR.reset();
    reductionDb = 0.0f;
    envelope = 0.0f;
    makeup = makeupTarget;
}

void GlueCompressor::setControls(const std::array<float, 6>& c, const ModContext&) noexcept
{
    thresholdDb = kInfo.controls[0].display.value(std::clamp(c[0], 0.0f, 1.0f));
    ratio = kRatios[std::clamp(static_cast<int>(c[1] * kNumRatios), 0, kNumRatios - 1)];
    attackCoeff = onePoleCoefficient(kInfo.controls[2].display.value(std::clamp(c[2], 0.0f, 1.0f)) * 0.001f, fs);
    releaseCoeff = onePoleCoefficient(kInfo.controls[3].display.value(std::clamp(c[3], 0.0f, 1.0f)) * 0.001f, fs);
    makeupTarget = dbToGain(kInfo.controls[4].display.value(std::clamp(c[4], 0.0f, 1.0f)));
    const float hp = kInfo.controls[5].display.value(std::clamp(c[5], 0.0f, 1.0f));
    sidechainL.setHighPass(hp);
    sidechainR.setHighPass(hp);
}

void GlueCompressor::process(float* left, float* right, int n) noexcept
{
    constexpr float kDbToLog = 0.11512925f;
    for (int i = 0; i < n; ++i)
    {
        makeup += smoothCoeff * (makeupTarget - makeup);
        const float level = std::max(std::fabs(sidechainL.process(left[i])), std::fabs(sidechainR.process(right[i])));
        envelope = flushDenormal(std::max(level, envelope + peakCoeff * (level - envelope)));
        const float levelDb = envelope > 1.0e-6f ? 20.0f * std::log10(envelope) : -120.0f;
        const float target = staticCurveDb(levelDb, thresholdDb, ratio);
        const float coeff = target < reductionDb ? attackCoeff : releaseCoeff;
        reductionDb = flushDenormal(reductionDb + coeff * (target - reductionDb));
        const float gain = std::exp(reductionDb * kDbToLog) * makeup;
        left[i] *= gain;
        right[i] *= gain;
    }
}
}
