#include "FdnReverb.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf::dsp {
namespace {
constexpr std::array<float, 8> kLineMs { 31.7f, 37.3f, 41.9f, 47.3f, 53.1f, 59.9f, 67.7f, 73.1f };
constexpr std::array<float, 4> kDiffuserMs { 4.7f, 6.1f, 7.9f, 11.3f };
constexpr float kMaxSize = 2.0f;
constexpr float kMaxModMs = 1.2f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo FdnReverb::kInfo {
    "tf.reverb", "Cloud Reverb",
    { { { "Size", 0.6f, { Curve::Linear, 30.0f, 170.0f, "%" } },
        { "Decay", 0.55f, { Curve::Exp, 0.3f, 200.0f, "s", 1 } },
        { "Damping", 0.6f, { Curve::Exp, 1000.0f, 16.0f, "Hz" } },
        { "Pre-delay", 0.15f, { Curve::Power, 250.0f, 2.0f, "ms" } },
        { "Modulation", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Hold", 0.0f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    true
};

float FdnReverb::decayFrom01(float v) noexcept { return 0.3f * std::pow(200.0f, std::clamp(v, 0.0f, 1.0f)); }

float FdnReverb::dampingFrom01(float v) noexcept { return 1000.0f * std::pow(16.0f, std::clamp(v, 0.0f, 1.0f)); }

void FdnReverb::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto msToSamples = [&](float ms) { return static_cast<std::size_t>(ms * 0.001 * fs) + 8; };

    for (int i = 0; i < kLines; ++i)
    {
        lines[static_cast<size_t>(i)].prepare(msToSamples(kLineMs[static_cast<size_t>(i)] * kMaxSize + kMaxModMs * 2.0f));
        damping[static_cast<size_t>(i)].prepare(fs);
        modPhase[static_cast<size_t>(i)] = static_cast<float>(i) / kLines;
        modRate[static_cast<size_t>(i)] = 0.07f + 0.031f * static_cast<float>(i);
    }
    for (int i = 0; i < 4; ++i)
    {
        diffusers[static_cast<size_t>(i)].prepare(msToSamples(kDiffuserMs[static_cast<size_t>(i)]));
        diffuserDelay[static_cast<size_t>(i)] = kDiffuserMs[static_cast<size_t>(i)] * 0.001f * static_cast<float>(fs);
    }
    predelayL.prepare(msToSamples(260.0f));
    predelayR.prepare(msToSamples(260.0f));
    setControls({ 0.6f, 0.55f, 0.6f, 0.15f, 0.3f, 0.0f }, {});
    reset();
}

void FdnReverb::reset() noexcept
{
    for (auto& l : lines)
        l.reset();
    for (auto& d : diffusers)
        d.reset();
    for (auto& d : damping)
        d.reset();
    predelayL.reset();
    predelayR.reset();
}

void FdnReverb::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    size = sizeFrom01(c[0]);
    decaySeconds = decayFrom01(c[1]);
    dampingHz = dampingFrom01(c[2]);
    predelay = predelayFrom01(c[3]) * 0.001f * static_cast<float>(fs);
    hold = std::clamp(c[5], 0.0f, 1.0f);
    modDepth = c[4] * (1.0f - hold) * kMaxModMs * 0.001f * static_cast<float>(fs);
    timeScale = ctx.timeScale;

    for (int i = 0; i < kLines; ++i)
    {
        const float delaySamples = std::round(kLineMs[static_cast<size_t>(i)] * size * 0.001f * static_cast<float>(fs));
        baseDelay[static_cast<size_t>(i)] = delaySamples;
        const float g = std::pow(10.0f, -3.0f * delaySamples / (decaySeconds * static_cast<float>(fs)));
        gains[static_cast<size_t>(i)] = std::min(1.0f, lerp(g, 1.0f, hold));
        damping[static_cast<size_t>(i)].setCutoff(dampingHz);
    }
}

void FdnReverb::process(float* left, float* right, int n) noexcept
{
    constexpr float kDiffusion = 0.6f;
    const float inputGain = 1.0f - hold;
    const float invFs = 1.0f / static_cast<float>(fs);

    for (int s = 0; s < n; ++s)
    {
        predelayL.push(left[s] * inputGain);
        predelayR.push(right[s] * inputGain);
        const float inL = predelay >= 1.0f ? predelayL.read(predelay) : predelayL.at(0);
        const float inR = predelay >= 1.0f ? predelayR.read(predelay) : predelayR.at(0);

        float x = 0.5f * (inL + inR);
        for (int d = 0; d < 4; ++d)
        {
            auto& ap = diffusers[static_cast<size_t>(d)];
            const float delayed = ap.read(diffuserDelay[static_cast<size_t>(d)]);
            const float v = x + kDiffusion * delayed;
            ap.push(flushDenormal(v));
            x = delayed - kDiffusion * v;
        }
        const float side = 0.5f * (inL - inR);

        std::array<float, kLines> out {};
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const auto ui = static_cast<size_t>(i);
            modPhase[ui] += modRate[ui] * timeScale * invFs;
            if (modPhase[ui] >= 1.0f)
                modPhase[ui] -= 1.0f;
            const float mod = modDepth * fastSin01(modPhase[ui]);
            const float raw = mod != 0.0f ? lines[ui].read(baseDelay[ui] + mod) : lines[ui].at(static_cast<std::size_t>(baseDelay[ui]) - 1);
            const float damped = damping[ui].processLow(raw);
            const float y = lerp(damped, raw, hold) * gains[ui];
            out[ui] = y;
            sum += y;
        }

        const float householder = sum * (2.0f / kLines);
        for (int i = 0; i < kLines; ++i)
        {
            const auto ui = static_cast<size_t>(i);
            const float injected = (i % 2 == 0 ? x + side : x - side) * 0.5f;
            lines[ui].push(flushDenormal(out[ui] - householder + injected));
        }

        left[s] = (out[0] - out[2] + out[4] - out[6]) * 0.5f;
        right[s] = (out[1] - out[3] + out[5] - out[7]) * 0.5f;
    }
}
}
