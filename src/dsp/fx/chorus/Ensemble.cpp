#include "Ensemble.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kVoiceChoices[] = { "Two", "Three", "Four", "Six" };
constexpr int kVoiceCounts[] = { 2, 3, 4, 6 };
constexpr float kBaseMs = 9.0f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo Ensemble::kInfo {
    "tf.ensemble", "Ensemble",
    { { { "Rate", 0.45f, { Curve::Exp, 0.05f, 40.0f, "Hz", 2 } },
        { "Depth", 0.6f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Vibrato", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Voices", 0.4f, { Curve::Choice, 0.0f, 1.0f, "", 0, kVoiceChoices, 4 } },
        { "Spread", 0.8f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Tone", 0.7f, { Curve::Exp, 1500.0f, 12.0f, "Hz" } } } },
    false
};

void Ensemble::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>(0.03 * fs) + 8;
    lineL.prepare(maxSamples);
    lineR.prepare(maxSamples);
    toneL.prepare(fs);
    toneR.prepare(fs);
    reset();
}

void Ensemble::reset() noexcept
{
    lineL.reset();
    lineR.reset();
    toneL.reset();
    toneR.reset();
}

void Ensemble::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    rateHz = kInfo.controls[0].display.value(c[0]);
    depth = std::clamp(c[1], 0.0f, 1.0f);
    vibrato = std::clamp(c[2], 0.0f, 1.0f);
    voices = kVoiceCounts[std::clamp(static_cast<int>(c[3] * 4.0f), 0, 3)];
    spread = std::clamp(c[4], 0.0f, 1.0f);
    const float tone = kInfo.controls[5].display.value(c[5]);
    toneL.setCutoff(tone);
    toneR.setCutoff(tone);
    timeScale = ctx.timeScale;
}

void Ensemble::process(float* left, float* right, int n) noexcept
{
    const float msToSamples = static_cast<float>(fs) * 0.001f;
    const float slowInc = rateHz * timeScale / static_cast<float>(fs);
    const float fastInc = 6.2f / static_cast<float>(fs);
    const float invVoices = 1.0f / static_cast<float>(voices);
    const float norm = 1.0f / std::sqrt(static_cast<float>(voices));
    const float slowMs = 4.0f * depth;
    const float fastMs = 0.35f * vibrato;
    auto sinAt = [](float phase) { return fastSin01(phase - std::floor(phase)); };
    for (int i = 0; i < n; ++i)
    {
        lineL.push(left[i]);
        lineR.push(right[i]);
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < voices; ++v)
        {
            const float off = static_cast<float>(v) * invVoices;
            const float offR = off + 0.25f * spread;
            const float dl = kBaseMs + slowMs * sinAt(slowPhase + off) + fastMs * sinAt(fastPhase + off);
            const float dr = kBaseMs + slowMs * sinAt(slowPhase + offR) + fastMs * sinAt(fastPhase + offR);
            l += lineL.read(dl * msToSamples);
            r += lineR.read(dr * msToSamples);
        }
        left[i] = toneL.processLow(l * norm);
        right[i] = toneR.processLow(r * norm);
        slowPhase += slowInc;
        slowPhase -= std::floor(slowPhase);
        fastPhase += fastInc;
        fastPhase -= std::floor(fastPhase);
    }
}
}
