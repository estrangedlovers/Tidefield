#include "WornEcho.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

namespace {
const char* const kMediumChoices[] = { "Cassette", "Vinyl", "Sampler" };
constexpr float kMaxSeconds = 2.2f;
} // namespace

using Curve = DisplayMap::Curve;

const ProcessorInfo WornEcho::kInfo {
    "tf.wornEcho", "Worn Echo",
    { { { "Time", 0.62f, { Curve::Exp, 20.0f, 100.0f, "ms" } },
        { "Feedback", 0.55f, { Curve::Linear, 0.0f, 105.0f, "%" } },
        { "Medium", 0.0f, { Curve::Choice, 0.0f, 1.0f, "", 0, kMediumChoices, 3 } },
        { "Age", 0.5f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Wobble", 0.4f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Spread", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    true
};

void WornEcho::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    const auto maxSamples = static_cast<std::size_t>(kMaxSeconds * fs) + kChunk + 64;
    lineL.prepare(maxSamples);
    lineR.prepare(maxSamples);
    medium.prepare({ fs, kChunk }, 4242);
    dcL.prepare(fs);
    dcR.prepare(fs);
    glide = onePoleCoefficient(0.35f, fs);
    setControls({ 0.62f, 0.55f, 0.0f, 0.5f, 0.4f, 0.3f }, {});
    currentDelay = targetDelay;
    reset();
}

void WornEcho::reset() noexcept
{
    lineL.reset();
    lineR.reset();
    medium.reset();
    dcL.reset();
    dcR.reset();
    loopSatL.reset();
    loopSatR.reset();
}

void WornEcho::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    targetDelay = kInfo.controls[0].display.value(c[0]) * 0.001f * static_cast<float>(fs);
    feedback = 1.05f * std::clamp(c[1], 0.0f, 1.0f);
    const int type = std::clamp(static_cast<int>(c[2] * 3.0f), 0, 2);
    const float age = std::clamp(c[3], 0.0f, 1.0f);
    Medium::Params m;
    m.type = static_cast<Medium::Type>(type + 1);
    m.age = age;
    m.noise = 0.1f + 0.25f * age; // hiss builds up generation after generation
    m.wobble = std::clamp(c[4], 0.0f, 1.0f);
    m.drive = 0.2f + 0.4f * age;
    m.mix = 1.0f;
    medium.setParams(m);
    medium.setTimeScale(ctx.timeScale);
    spread = std::clamp(c[5], 0.0f, 1.0f);
}

void WornEcho::processChunk(float* left, float* right, int n) noexcept
{
    // Read the whole chunk of echoes first (sample i is read i pushes early), then
    // run them through the Medium, then write input + feedback back to the tape.
    const float latency = static_cast<float>(medium.getLatencySamples());
    const float maxDelay = static_cast<float>(lineL.capacity()) - 4.0f;
    for (int i = 0; i < n; ++i)
    {
        currentDelay += glide * (targetDelay - currentDelay);
        const float d = std::clamp(currentDelay - latency, static_cast<float>(kChunk + 2), maxDelay) - static_cast<float>(i);
        const float dR = std::clamp((currentDelay - latency) * (1.0f + 0.5f * spread), static_cast<float>(kChunk + 2), maxDelay)
                         - static_cast<float>(i);
        echoL[static_cast<std::size_t>(i)] = lineL.read(d);
        echoR[static_cast<std::size_t>(i)] = lineR.read(dR);
    }
    medium.process(echoL.data(), echoR.data(), n);
    const float cross = 0.5f * spread;
    for (int i = 0; i < n; ++i)
    {
        const auto ui = static_cast<std::size_t>(i);
        const float fbL = dcL.process(echoL[ui]);
        const float fbR = dcR.process(echoR[ui]);
        lineL.push(flushDenormal(1.2f * loopSatL.process((left[i] + feedback * lerp(fbL, fbR, cross)) * (1.0f / 1.2f))));
        lineR.push(flushDenormal(1.2f * loopSatR.process((right[i] + feedback * lerp(fbR, fbL, cross)) * (1.0f / 1.2f))));
        left[i] = echoL[ui];
        right[i] = echoR[ui];
    }
}

void WornEcho::process(float* left, float* right, int n) noexcept
{
    for (int done = 0; done < n; done += kChunk)
        processChunk(left + done, right + done, std::min(kChunk, n - done));
}

} // namespace tf::dsp
