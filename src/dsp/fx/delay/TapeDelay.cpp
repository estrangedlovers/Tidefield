#include "TapeDelay.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"
#include "../../core/TempoSync.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf::dsp {
namespace {
inline float saturate(float x) noexcept { return 1.2f * std::tanh(x * (1.0f / 1.2f)); }

constexpr float kMaxDelaySeconds = 2.2f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo TapeDelay::kInfo {
    "tf.delay", "Tape Delay",
    { { { "Time", 0.62f, { Curve::Exp, 20.0f, 100.0f, "ms" } },
        { "Feedback", 0.45f, { Curve::Linear, 0.0f, 110.0f, "%" } },
        { "Tone", 0.55f, { Curve::Exp, 500.0f, 32.0f, "Hz" } },
        { "Spread", 0.35f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Wobble", 0.25f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Age", 0.25f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    true
};

void TapeDelay::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>(kMaxDelaySeconds * fs) + 64;
    lineL.prepare(maxSamples);
    lineR.prepare(maxSamples);
    for (auto* f : { &toneL, &toneR, &lowCutL, &lowCutR })
        f->prepare(fs);
    dcL.prepare(fs);
    dcR.prepare(fs);
    wowDrift.setSeed(91);
    glideCoeff = onePoleCoefficient(0.35f, fs);
    setControls({ 0.62f, 0.45f, 0.55f, 0.35f, 0.25f, 0.25f }, {});
    currentDelay = targetDelay;
    reset();
}

void TapeDelay::reset() noexcept
{
    lineL.reset();
    lineR.reset();
    for (auto* f : { &toneL, &toneR, &lowCutL, &lowCutR })
        f->reset();
    dcL.reset();
    dcR.reset();
    loopSatL.reset();
    loopSatR.reset();
}

void TapeDelay::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    targetDelay = syncedSeconds(timeMsFrom01(c[0]) * 0.001f, ctx.beatSeconds, 2.0f) * static_cast<float>(fs);
    feedback = feedbackFrom01(c[1]);
    age = std::clamp(c[5], 0.0f, 1.0f);
    const float tone = toneHzFrom01(c[2]) * (1.0f - 0.6f * age);
    toneL.setCutoff(tone);
    toneR.setCutoff(tone);
    lowCutL.setCutoff(30.0f + 220.0f * age);
    lowCutR.setCutoff(30.0f + 220.0f * age);
    spread = std::clamp(c[3], 0.0f, 1.0f);
    wobble = std::clamp(c[4], 0.0f, 1.0f);
    timeScale = ctx.timeScale;
}

void TapeDelay::process(float* left, float* right, int n) noexcept
{
    const float dt = static_cast<float>(1.0 / fs);
    wowDrift.setRate(0.6f);
    const float drive = 1.0f + 2.0f * age;
    const float maxDelay = static_cast<float>(lineL.capacity()) - 4.0f;

    for (int s = 0; s < n; ++s)
    {
        currentDelay += glideCoeff * (targetDelay - currentDelay);

        const float wow = wowDrift.advance(dt * timeScale) * wobble * 0.004f * static_cast<float>(fs);
        flutterPhase += 7.3f * timeScale * dt;
        if (flutterPhase >= 1.0f)
            flutterPhase -= 1.0f;
        const float flutter = fastSin01(flutterPhase) * wobble * 0.0004f * static_cast<float>(fs);

        const float dL = std::clamp(currentDelay + wow + flutter, 1.0f, maxDelay);
        const float dR = std::clamp(currentDelay * (1.0f + 0.5f * spread) - wow + flutter, 1.0f, maxDelay);

        const float echoL = lineL.read(dL);
        const float echoR = lineR.read(dR);

        float fbL = lowCutL.processHigh(toneL.processLow(echoL));
        float fbR = lowCutR.processHigh(toneR.processLow(echoR));
        fbL = saturate(dcL.process(fbL) * drive) / drive;
        fbR = saturate(dcR.process(fbR) * drive) / drive;
        const float cross = 0.5f * spread;
        const float inL = left[s];
        const float inR = right[s];
        lineL.push(flushDenormal(1.2f * loopSatL.process((inL + feedback * lerp(fbL, fbR, cross)) * (1.0f / 1.2f))));
        lineR.push(flushDenormal(1.2f * loopSatR.process((inR + feedback * lerp(fbR, fbL, cross)) * (1.0f / 1.2f))));

        left[s] = echoL;
        right[s] = echoR;
    }
}
}
