#include "LoFi.h"

#include "../../core/Denormal.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
constexpr float kFullRateHz = 47000.0f;
constexpr float kBaseDelayMs = 4.0f;
constexpr float kWowMs = 3.0f;
constexpr float kNoiseLevel = 0.04f;
constexpr std::uint64_t kNoiseSeed = 0x6c6f6669ull;
constexpr std::uint64_t kWowSeed = 0x776f77ull;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo LoFi::kInfo {
    "tf.lofi", "Lo-fi",
    { { { "Bits", 0.25f, { Curve::Exp, 16.0f, 0.125f, "bit", 1 } },
        { "Rate", 0.3f, { Curve::Exp, 48000.0f, 1.0f / 48.0f, "Hz" } },
        { "Noise", 0.15f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Wow", 0.2f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Filter", 0.75f, { Curve::Exp, 500.0f, 36.0f, "Hz" } },
        { "Drive", 0.2f, { Curve::Linear, 0.0f, 18.0f, "dB", 1 } } } },
    false
};

void LoFi::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>((kBaseDelayMs + 2.0f * kWowMs) * 0.001 * fs) + 16;
    for (auto& ch : channels)
    {
        ch.line.prepare(maxSamples);
        ch.filter.prepare(fs);
    }
    smoothCoeff = onePoleCoefficient(0.03f, fs);
    setControls({ 0.25f, 0.3f, 0.15f, 0.2f, 0.75f, 0.2f }, {});
    reset();
}

void LoFi::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.line.reset();
        ch.shaper.reset();
        ch.filter.reset();
        ch.held = 0.0f;
    }
    noiseRng.setSeed(kNoiseSeed);
    wowDrift = Drift {};
    wowDrift.setSeed(kWowSeed);
    holdPhase = 0.0f;
    flutterPhase = 0.0f;
    bits = bitsTarget;
    rateLog = rateLogTarget;
    noise = noiseTarget;
    wow = wowTarget;
    drive = driveTarget;
}

void LoFi::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    bitsTarget = kInfo.controls[0].display.value(std::clamp(c[0], 0.0f, 1.0f));
    rateLogTarget = std::log2(kInfo.controls[1].display.value(std::clamp(c[1], 0.0f, 1.0f)));
    const float n01 = std::clamp(c[2], 0.0f, 1.0f);
    noiseTarget = kNoiseLevel * n01 * n01;
    wowTarget = std::clamp(c[3], 0.0f, 1.0f);
    const float cutoff = kInfo.controls[4].display.value(std::clamp(c[4], 0.0f, 1.0f));
    for (auto& ch : channels)
        ch.filter.setLowPass(cutoff, 0.8f);
    driveTarget = dbToGain(kInfo.controls[5].display.value(std::clamp(c[5], 0.0f, 1.0f)));
    timeScale = std::clamp(ctx.timeScale, 0.0f, 16.0f);
}

void LoFi::process(float* left, float* right, int n) noexcept
{
    const float sr = static_cast<float>(fs);
    const float dt = 1.0f / sr;
    const float msToSamples = sr * 0.001f;
    wowDrift.setRate(0.8f);
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        bits += smoothCoeff * (bitsTarget - bits);
        rateLog += smoothCoeff * (rateLogTarget - rateLog);
        noise += smoothCoeff * (noiseTarget - noise);
        wow += smoothCoeff * (wowTarget - wow);
        drive += smoothCoeff * (driveTarget - drive);

        const float drift = wowDrift.advance(dt * timeScale);
        flutterPhase += 5.5f * timeScale * dt;
        flutterPhase -= std::floor(flutterPhase);
        const float wobble = wow * kWowMs * (0.8f * drift + 0.2f * fastSin01(flutterPhase));
        const float delay = (kBaseDelayMs + wobble) * msToSamples;

        const float rate = std::exp2(rateLog);
        holdPhase += rate >= kFullRateHz ? 1.0f : rate * dt;
        const bool take = holdPhase >= 1.0f;
        if (take)
            holdPhase -= std::floor(holdPhase);

        const float step = std::exp2(1.0f - bits);
        const float makeup = 1.0f / std::sqrt(drive);
        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            auto& ch = channels[c];
            ch.line.push(ch.shaper.process(io[c][i] * drive) * makeup);
            const float x = ch.line.read(delay);
            if (take)
                ch.held = std::clamp(x, -2.0f, 2.0f);
            const float crushed = step * std::floor(ch.held / step + 0.5f);
            const float hiss = noise * noiseRng.nextBipolar();
            io[c][i] = ch.filter.process(crushed + hiss);
        }
    }
}
}
