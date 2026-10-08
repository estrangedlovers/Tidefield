#include "MultimodeFilter.h"

#include "../../core/Denormal.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kModeChoices[] = { "Low", "Band", "High", "Notch" };
constexpr float kSweepOctaves = 3.0f;
constexpr float kMinK = 0.08f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo MultimodeFilter::kInfo {
    "tf.filter", "Filter",
    { { { "Mode", 0.0f, { Curve::Choice, 0.0f, 1.0f, "", 0, kModeChoices, 4 } },
        { "Cutoff", 0.6f, { Curve::Exp, 20.0f, 1000.0f, "Hz" } },
        { "Resonance", 0.25f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Drive", 0.0f, { Curve::Linear, 0.0f, 24.0f, "dB", 1 } },
        { "Sweep", 0.0f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Rate", 0.35f, { Curve::Exp, 0.02f, 500.0f, "Hz", 2 } } } },
    false
};

void MultimodeFilter::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    smoothCoeff = onePoleCoefficient(0.02f, fs);
    setControls({ 0.0f, 0.6f, 0.25f, 0.0f, 0.0f, 0.35f }, {});
    reset();
}

void MultimodeFilter::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.ic1 = ch.ic2 = 0.0f;
        ch.drive.reset();
    }
    modeWeight = modeTarget;
    octave = octaveTarget;
    k = kTarget;
    driveDb = driveDbTarget;
    sweep = sweepTarget;
    lfoPhase = 0.0f;
}

void MultimodeFilter::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    const int mode = std::clamp(static_cast<int>(c[0] * kNumModes), 0, kNumModes - 1);
    for (int m = 0; m < kNumModes; ++m)
        modeTarget[static_cast<std::size_t>(m)] = m == mode ? 1.0f : 0.0f;
    octaveTarget = std::log2(kInfo.controls[1].display.value(std::clamp(c[1], 0.0f, 1.0f)));
    kTarget = 2.0f - (2.0f - kMinK) * std::clamp(c[2], 0.0f, 1.0f);
    driveDbTarget = kInfo.controls[3].display.value(std::clamp(c[3], 0.0f, 1.0f));
    sweepTarget = std::clamp(c[4], 0.0f, 1.0f);
    rateHz = kInfo.controls[5].display.value(std::clamp(c[5], 0.0f, 1.0f));
    timeScale = std::clamp(ctx.timeScale, 0.0f, 16.0f);
}

float MultimodeFilter::processChannel(Channel& ch, float x, float g, float kk, float driveGain, float driveNorm) noexcept
{
    const float in = ch.drive.process(x * driveGain) * driveNorm;
    const float a1 = 1.0f / (1.0f + g * (g + kk));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = in - ch.ic2;
    const float v1 = a1 * ch.ic1 + a2 * v3;
    const float v2 = ch.ic2 + a2 * ch.ic1 + a3 * v3;
    ch.ic1 = flushDenormal(2.0f * v1 - ch.ic1);
    ch.ic2 = flushDenormal(2.0f * v2 - ch.ic2);
    const float peakTrim = 1.0f / (1.0f + 0.04f / (kk * kk));
    const float low = v2 * peakTrim;
    const float band = kk * v1;
    const float high = (in - kk * v1 - v2) * peakTrim;
    const float notch = in - kk * v1;
    return modeWeight[0] * low + modeWeight[1] * band + modeWeight[2] * high + modeWeight[3] * notch;
}

void MultimodeFilter::process(float* left, float* right, int n) noexcept
{
    const float inc = rateHz * timeScale / static_cast<float>(fs);
    const float nyquistOctave = std::log2(0.45f * static_cast<float>(fs));
    const float a = smoothCoeff;
    for (int i = 0; i < n; ++i)
    {
        octave += a * (octaveTarget - octave);
        k += a * (kTarget - k);
        driveDb += a * (driveDbTarget - driveDb);
        sweep += a * (sweepTarget - sweep);
        for (std::size_t m = 0; m < modeWeight.size(); ++m)
            modeWeight[m] += a * (modeTarget[m] - modeWeight[m]);

        const float driveGain = std::exp2(driveDb * (1.0f / 6.0206f));
        const float driveNorm = 1.0f / std::sqrt(driveGain);
        const float swing = sweep * kSweepOctaves;
        const float octL = std::clamp(octave + swing * fastSin01(lfoPhase), 4.0f, nyquistOctave);
        float phaseR = lfoPhase + 0.25f;
        phaseR -= std::floor(phaseR);
        const float octR = std::clamp(octave + swing * fastSin01(phaseR), 4.0f, nyquistOctave);
        const float gL = std::tan(kPi * std::exp2(octL) / static_cast<float>(fs));
        const float gR = std::tan(kPi * std::exp2(octR) / static_cast<float>(fs));

        left[i] = processChannel(channels[0], left[i], gL, k, driveGain, driveNorm);
        right[i] = processChannel(channels[1], right[i], gR, k, driveGain, driveNorm);

        lfoPhase += inc;
        lfoPhase -= std::floor(lfoPhase);
    }
}
}
