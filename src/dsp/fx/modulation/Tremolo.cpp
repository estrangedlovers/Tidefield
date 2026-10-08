#include "Tremolo.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
const char* const kShapeChoices[] = { "Sine", "Triangle", "Square", "Ramp", "Random" };
constexpr float kSharpestHz = 200.0f;
}

using Curve = DisplayMap::Curve;

const ProcessorInfo Tremolo::kInfo {
    "tf.tremolo", "Tremolo",
    { { { "Rate", 0.5f, { Curve::Exp, 0.05f, 400.0f, "Hz", 2 } },
        { "Depth", 0.6f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Shape", 0.1f, { Curve::Choice, 0.0f, 1.0f, "", 0, kShapeChoices, 5 } },
        { "Stereo", 0.0f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Smooth", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Wander", 0.0f, { Curve::Linear, 0.0f, 100.0f, "%" } } } },
    false
};

void Tremolo::prepare(const ProcessSpec& spec)
{
    fs = spec.sampleRate;
    smoothCoeff = onePoleCoefficient(0.02f, fs);
    setControls({ 0.5f, 0.6f, 0.1f, 0.0f, 0.3f, 0.0f }, {});
    reset();
}

void Tremolo::reset() noexcept
{
    for (std::size_t c = 0; c < channels.size(); ++c)
    {
        auto& ch = channels[c];
        ch.rng.setSeed(0x7472656dull + c);
        ch.phase = 0.0f;
        ch.held = ch.rng.nextBipolar();
        ch.gain = 1.0f;
    }
    wander = Drift {};
    wander.setSeed(0x77616e64ull);
    depth = depthTarget;
    stereo = stereoTarget;
}

void Tremolo::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    rateHz = kInfo.controls[0].display.value(std::clamp(c[0], 0.0f, 1.0f));
    depthTarget = std::clamp(c[1], 0.0f, 1.0f);
    shape = std::clamp(static_cast<int>(c[2] * kNumShapes), 0, kNumShapes - 1);
    stereoTarget = std::clamp(c[3], 0.0f, 1.0f);
    smooth = std::clamp(c[4], 0.0f, 1.0f);
    wanderAmount = std::clamp(c[5], 0.0f, 1.0f);
    timeScale = std::clamp(ctx.timeScale, 0.0f, 16.0f);
}

float Tremolo::shapeAt(Channel& ch) const noexcept
{
    const float p = ch.phase;
    switch (shape)
    {
        case 0: return fastSin01(p);
        case 1: return 4.0f * std::fabs(p - 0.5f) - 1.0f;
        case 2: return p < 0.5f ? 1.0f : -1.0f;
        case 3: return 1.0f - 2.0f * p;
        default: return ch.held;
    }
}

void Tremolo::process(float* left, float* right, int n) noexcept
{
    const float dt = static_cast<float>(1.0 / fs);
    wander.setRate(0.3f + 0.2f * rateHz);
    const float slowestHz = std::max(rateHz * timeScale, 0.05f) * 4.0f;
    const float cutoff = std::exp(lerp(std::log(kSharpestHz), std::log(std::min(slowestHz, kSharpestHz)), smooth));
    const float gainCoeff = 1.0f - std::exp(-kTwoPi * cutoff * dt);
    float* io[2] = { left, right };
    float master = channels[0].phase;
    for (int i = 0; i < n; ++i)
    {
        depth += smoothCoeff * (depthTarget - depth);
        stereo += smoothCoeff * (stereoTarget - stereo);
        const float drift = wander.advance(dt * timeScale);
        const float inc = rateHz * timeScale * (1.0f + 0.6f * wanderAmount * drift) * dt;
        master += inc;
        master -= std::floor(master);
        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            auto& ch = channels[c];
            float p = master + (c == 0 ? 0.0f : 0.5f * stereo);
            p -= std::floor(p);
            if (p < ch.phase && ch.phase - p > 0.5f)
                ch.held = ch.rng.nextBipolar();
            ch.phase = p;
            const float wave = lerp(shapeAt(ch), fastSin01(p), smooth);
            const float target = 1.0f - depth * 0.5f * (1.0f - wave);
            ch.gain = flushDenormal(ch.gain + gainCoeff * (target - ch.gain));
            io[c][i] *= ch.gain;
        }
    }
}
}
