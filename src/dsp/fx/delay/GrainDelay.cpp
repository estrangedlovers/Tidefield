#include "GrainDelay.h"

#include "../../core/Denormal.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
constexpr float kMaxSeconds = 4.0f;
constexpr float kMaxSpreadSemitones = 12.0f;
constexpr float kLoopDampHz = 7000.0f;
constexpr std::uint64_t kSeed = 0x67726169ull;
constexpr float kSqrt2 = 1.41421356f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo GrainDelay::kInfo {
    "tf.grainDelay", "Grain Delay",
    { { { "Time", 0.6f, { Curve::Exp, 20.0f, 100.0f, "ms" } },
        { "Size", 0.55f, { Curve::Exp, 20.0f, 25.0f, "ms" } },
        { "Density", 0.45f, { Curve::Exp, 2.0f, 40.0f, "/s", 1 } },
        { "Pitch", 0.1f, { Curve::Linear, 0.0f, kMaxSpreadSemitones, "st", 1 } },
        { "Feedback", 0.35f, { Curve::Linear, 0.0f, 95.0f, "%" } },
        { "Jitter", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    true
};

void GrainDelay::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>(kMaxSeconds * fs) + 64;
    lineL.prepare(maxSamples);
    lineR.prepare(maxSamples);
    dcL.prepare(fs);
    dcR.prepare(fs);
    dampL.prepare(fs);
    dampR.prepare(fs);
    dampL.setCutoff(kLoopDampHz);
    dampR.setCutoff(kLoopDampHz);
    smoothCoeff = onePoleCoefficient(0.05f, fs);
    normCoeff = onePoleCoefficient(0.01f, fs);
    setControls({ 0.6f, 0.55f, 0.45f, 0.1f, 0.35f, 0.3f }, {});
    reset();
}

void GrainDelay::reset() noexcept
{
    lineL.reset();
    lineR.reset();
    dcL.reset();
    dcR.reset();
    dampL.reset();
    dampR.reset();
    loopSatL.reset();
    loopSatR.reset();
    for (auto& g : grains)
        g = {};
    rng.setSeed(kSeed);
    countdown = 0.0f;
    feedback = feedbackTarget;
    norm = 1.0f;
}

int GrainDelay::getActiveGrains() const noexcept
{
    int n = 0;
    for (const auto& g : grains)
        n += g.active ? 1 : 0;
    return n;
}

void GrainDelay::setControls(const std::array<float, 6>& c, const ModContext&) noexcept
{
    const float sr = static_cast<float>(fs);
    timeSamples = kInfo.controls[0].display.value(std::clamp(c[0], 0.0f, 1.0f)) * 0.001f * sr;
    sizeSamples = kInfo.controls[1].display.value(std::clamp(c[1], 0.0f, 1.0f)) * 0.001f * sr;
    density = kInfo.controls[2].display.value(std::clamp(c[2], 0.0f, 1.0f));
    pitchSpread = kInfo.controls[3].display.value(std::clamp(c[3], 0.0f, 1.0f));
    feedbackTarget = 0.95f * std::clamp(c[4], 0.0f, 1.0f);
    jitter = std::clamp(c[5], 0.0f, 1.0f);
    normExponent = 1.0f - 0.5f * std::clamp(jitter + pitchSpread * 0.25f, 0.0f, 1.0f);
}

void GrainDelay::spawn() noexcept
{
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active)
        {
            slot = &g;
            break;
        }
    if (slot == nullptr)
        return;
    const float length = std::max(16.0f, sizeSamples);
    const float rate = std::exp2(pitchSpread * rng.nextBipolar() / 12.0f);
    const float drift = 1.0f - rate;
    const float capacity = static_cast<float>(lineL.capacity()) - 4.0f;
    const float lowest = 4.0f + std::max(0.0f, -drift) * length;
    const float highest = capacity - std::max(0.0f, drift) * length;
    const float start = timeSamples * (1.0f + 0.5f * jitter * rng.nextBipolar()) + 0.5f * jitter * length * rng.nextFloat();
    const auto pan = equalPowerPan(0.7f * jitter * rng.nextBipolar());
    slot->active = true;
    slot->delay = std::clamp(start, lowest, std::max(lowest, highest));
    slot->drift = drift;
    slot->age = 0.0f;
    slot->invLength = 1.0f / length;
    slot->gainL = pan.left * kSqrt2;
    slot->gainR = pan.right * kSqrt2;
}

void GrainDelay::process(float* left, float* right, int n) noexcept
{
    const float interval = static_cast<float>(fs) / std::max(0.1f, density);
    for (int i = 0; i < n; ++i)
    {
        feedback += smoothCoeff * (feedbackTarget - feedback);
        countdown -= 1.0f;
        if (countdown <= 0.0f)
        {
            spawn();
            countdown += interval * (0.5f + rng.nextFloat());
        }

        float wetL = 0.0f, wetR = 0.0f, envSum = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;
            const float t = g.age * g.invLength;
            float q = t + 0.25f;
            q -= std::floor(q);
            const float env = 0.5f - 0.5f * fastSin01(q);
            envSum += env;
            wetL += env * g.gainL * lineL.read(g.delay);
            wetR += env * g.gainR * lineR.read(g.delay);
            g.delay += g.drift;
            g.age += 1.0f;
            if (t >= 1.0f)
                g.active = false;
        }
        const float normTarget = envSum > 1.0f ? std::exp2(-normExponent * std::log2(envSum)) : 1.0f;
        norm += normCoeff * (normTarget - norm);
        wetL *= norm;
        wetR *= norm;

        const float fbL = dampL.processLow(dcL.process(wetL));
        const float fbR = dampR.processLow(dcR.process(wetR));
        lineL.push(flushDenormal(1.2f * loopSatL.process((left[i] + feedback * fbL) * (1.0f / 1.2f))));
        lineR.push(flushDenormal(1.2f * loopSatR.process((right[i] + feedback * fbR) * (1.0f / 1.2f))));
        left[i] = wetL;
        right[i] = wetR;
    }
}
}
