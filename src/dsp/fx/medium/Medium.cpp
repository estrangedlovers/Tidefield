#include "Medium.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf::dsp {
namespace {
inline float softClip(float x) noexcept { return std::tanh(x); }

constexpr const char* kTypeNames[] = { "Digital", "Cassette", "Vinyl", "Sampler" };
}

const char* Medium::typeName(Type t) noexcept
{
    const int i = static_cast<int>(t);
    return i >= 0 && i < kNumTypes ? kTypeNames[i] : "";
}

void Medium::prepare(const ProcessSpec& spec, std::uint64_t seed)
{
    fs = spec.sampleRate;
    maxBlock = std::max(32, spec.maxBlockSize);
    baseDelay = static_cast<int>(std::lround(kBaseDelayMs * 0.001 * fs));
    const auto lineSize = static_cast<std::size_t>(0.02 * fs) + static_cast<std::size_t>(baseDelay);

    for (auto* d : { &cassette.dl, &cassette.dr, &vinyl.dl, &vinyl.dr, &sampler.dl, &sampler.dr, &digitalL, &digitalR, &dryL, &dryR })
        d->prepare(lineSize);

    cassette.wow.setSeed(seed + 1);
    cassette.wow.setRate(0.5f);
    cassette.rng.setSeed(seed + 2);
    vinyl.wowDrift.setSeed(seed + 3);
    vinyl.wowDrift.setRate(0.2f);
    vinyl.rng.setSeed(seed + 4);
    sampler.rng.setSeed(seed + 5);

    for (auto* b : { &cassette.bumpL, &cassette.bumpR })
        b->prepare(fs);
    for (auto* s : { &cassette.hfL, &cassette.hfR, &vinyl.hfL, &vinyl.hfR, &sampler.preL, &sampler.preR, &sampler.postL, &sampler.postR })
        s->prepare(fs);
    for (auto* o : { &cassette.hissHp, &cassette.hissLp, &vinyl.sideHp, &vinyl.crackleHp, &vinyl.rumbleLp, &vinyl.surfaceLp, &vinyl.popLp })
        o->prepare(fs);

    cassette.hissHp.setCutoff(900.0f);
    cassette.hissLp.setCutoff(11000.0f);
    vinyl.sideHp.setCutoff(150.0f);
    vinyl.crackleHp.setCutoff(1800.0f);
    vinyl.rumbleLp.setCutoff(25.0f);
    vinyl.surfaceLp.setCutoff(4000.0f);
    vinyl.popLp.setCutoff(900.0f);

    const auto n = static_cast<std::size_t>(maxBlock);
    for (auto* v : { &inL, &inR, &curL, &curR, &prevL, &prevR })
        v->assign(n, 0.0f);

    fadeStep = 1.0f / static_cast<float>(kCrossfadeSeconds * fs);
    coefficientsValid = false;
    setParams(params);
    reset();
}

void Medium::reset() noexcept
{
    for (auto* d : { &cassette.dl, &cassette.dr, &vinyl.dl, &vinyl.dr, &sampler.dl, &sampler.dr, &digitalL, &digitalR, &dryL, &dryR })
        d->reset();
    for (auto* b : { &cassette.bumpL, &cassette.bumpR })
        b->reset();
    for (auto* s : { &cassette.hfL, &cassette.hfR, &vinyl.hfL, &vinyl.hfR, &sampler.preL, &sampler.preR, &sampler.postL, &sampler.postR })
        s->reset();
    for (auto* o : { &cassette.hissHp, &cassette.hissLp, &vinyl.sideHp, &vinyl.crackleHp, &vinyl.rumbleLp, &vinyl.surfaceLp, &vinyl.popLp })
        o->reset();
    cassette.dropoutGain = cassette.dropoutTarget = 1.0f;
    cassette.satL.reset();
    cassette.satR.reset();
    cassette.dropoutRemaining = 0;
    vinyl.clickEnv = vinyl.popEnv = 0.0f;
    sampler.phase = sampler.holdL = sampler.holdR = 0.0f;
    fade = 1.0f;
    previousType = params.type;
}

void Medium::setParams(const Params& p) noexcept
{
    if (p.type != params.type)
    {
        previousType = params.type;
        fade = 0.0f;
    }
    params = p;

    const float age = std::clamp(p.age, 0.0f, 1.0f);
    if (coefficientsValid && age == coefficientAge)
        return;
    coefficientsValid = true;
    coefficientAge = age;
    cassette.bumpL.setPeak(75.0f, 0.9f, 1.5f + 2.0f * age);
    cassette.bumpR.setPeak(75.0f, 0.9f, 1.5f + 2.0f * age);
    const float tapeHf = 16000.0f * std::pow(0.33f, age);
    cassette.hfL.setCutoff(tapeHf, 0.1f);
    cassette.hfR.setCutoff(tapeHf, 0.1f);

    const float vinylHf = 18000.0f * std::pow(0.5f, age);
    vinyl.hfL.setCutoff(vinylHf, 0.05f);
    vinyl.hfR.setCutoff(vinylHf, 0.05f);

    const float rate = 26000.0f * std::pow(0.3f, age);
    for (auto* s : { &sampler.preL, &sampler.preR })
        s->setCutoff(rate * 0.42f, 0.2f);
    for (auto* s : { &sampler.postL, &sampler.postR })
        s->setCutoff(std::min(rate * 0.45f, 12000.0f), 0.1f);
}

void Medium::runDigital(const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        digitalL.push(iL[i]);
        digitalR.push(iR[i]);
        oL[i] = digitalL.at(static_cast<std::size_t>(baseDelay));
        oR[i] = digitalR.at(static_cast<std::size_t>(baseDelay));
    }
}

void Medium::runCassette(const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
{
    auto& c = cassette;
    const float age = std::clamp(params.age, 0.0f, 1.0f);
    const float wobble = std::clamp(params.wobble, 0.0f, 1.0f);
    const float driveGain = dbToGain(18.0f * params.drive);
    const float makeup = 1.0f / std::sqrt(driveGain);
    const float bias = 0.12f * age;
    const float biasOffset = std::tanh(bias);
    const float hiss = params.noise * dbToGain(-54.0f + 12.0f * age);
    const float dt = static_cast<float>(1.0 / fs);
    const float fsf = static_cast<float>(fs);
    const float dropoutRate = std::max(0.0f, age - 0.4f) * 0.6f;

    for (int i = 0; i < n; ++i)
    {
        c.dl.push(iL[i]);
        c.dr.push(iR[i]);

        const float wow = c.wow.advance(dt * timeScale) * wobble * 0.0015f * fsf;
        c.flutterPhase += 9.5f * timeScale * dt;
        if (c.flutterPhase >= 1.0f)
            c.flutterPhase -= 1.0f;
        const float flutter = fastSin01(c.flutterPhase) * wobble * 0.00012f * fsf;
        const float d = static_cast<float>(baseDelay) + wow + flutter;

        float l = c.dl.read(d);
        float r = c.dr.read(d);

        const float lx = l + 0.03f * r;
        const float rx = r + 0.03f * l;

        l = (c.satL.process(lx * driveGain + bias) - biasOffset) * makeup;
        r = (c.satR.process(rx * driveGain + bias) - biasOffset) * makeup;

        l = c.hfL.processLow(c.bumpL.process(l));
        r = c.hfR.processLow(c.bumpR.process(r));

        if (c.dropoutRemaining > 0)
        {
            if (--c.dropoutRemaining == 0)
                c.dropoutTarget = 1.0f;
        }
        else if (dropoutRate > 0.0f && c.rng.chance(dropoutRate * dt))
        {
            c.dropoutTarget = dbToGain(-6.0f - 12.0f * c.rng.nextFloat());
            c.dropoutRemaining = static_cast<int>((0.04f + 0.16f * c.rng.nextFloat()) * fsf);
        }
        c.dropoutGain += 0.003f * (c.dropoutTarget - c.dropoutGain);

        const float h = c.hissLp.processLow(c.hissHp.processHigh(c.rng.nextBipolar())) * hiss;
        const float h2 = c.rng.nextBipolar() * hiss * 0.3f;

        oL[i] = l * c.dropoutGain + h + h2;
        oR[i] = r * c.dropoutGain + h - h2;
    }
}

void Medium::runVinyl(const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
{
    auto& v = vinyl;
    const float age = std::clamp(params.age, 0.0f, 1.0f);
    const float wobble = std::clamp(params.wobble, 0.0f, 1.0f);
    const float noise = std::clamp(params.noise, 0.0f, 1.0f);
    const float driveGain = dbToGain(9.0f * params.drive);
    const float makeup = 1.0f / std::sqrt(driveGain);
    const float dt = static_cast<float>(1.0 / fs);
    const float fsf = static_cast<float>(fs);
    const float crackleRate = noise * (4.0f + 50.0f * age);
    const float popRate = noise * (0.05f + 0.6f * age);
    const float clickDecay = std::exp(-1.0f / (0.00025f * fsf));
    const float popDecay = std::exp(-1.0f / (0.004f * fsf));
    const float surface = noise * dbToGain(-62.0f + 10.0f * age);
    const float rumble = noise * dbToGain(-40.0f + 8.0f * age);

    for (int i = 0; i < n; ++i)
    {
        v.dl.push(iL[i]);
        v.dr.push(iR[i]);

        v.wowPhase += 0.555f * timeScale * dt;
        if (v.wowPhase >= 1.0f)
            v.wowPhase -= 1.0f;
        const float wow = (fastSin01(v.wowPhase) + 0.3f * v.wowDrift.advance(dt * timeScale)) * wobble * 0.0012f * fsf;
        const float d = static_cast<float>(baseDelay) + wow;
        float l = v.dl.read(d);
        float r = v.dr.read(d);

        const float mid = 0.5f * (l + r);
        const float side = v.sideHp.processHigh(0.5f * (l - r));
        l = v.hfL.processLow(softClip((mid + side) * driveGain) * makeup);
        r = v.hfR.processLow(softClip((mid - side) * driveGain) * makeup);

        if (v.rng.chance(crackleRate * dt))
        {
            const float a = v.rng.nextFloat();
            v.clickEnv = 0.03f + 0.35f * a * a * a;
            v.clickSign = v.rng.nextFloat() < 0.5f ? -1.0f : 1.0f;
            v.clickPan = v.rng.nextBipolar() * 0.6f;
        }
        if (v.rng.chance(popRate * dt))
            v.popEnv = 0.2f + 0.3f * v.rng.nextFloat();
        const float click = v.crackleHp.processHigh(v.clickSign * v.clickEnv * (0.6f + 0.4f * v.rng.nextBipolar()));
        const float pop = v.popLp.processLow(v.popEnv * v.rng.nextBipolar());
        v.clickEnv = flushDenormal(v.clickEnv * clickDecay);
        v.popEnv = flushDenormal(v.popEnv * popDecay);

        const float bed = v.surfaceLp.processLow(v.rng.nextBipolar()) * surface + v.rumbleLp.processLow(v.rng.nextBipolar()) * rumble;
        const float crackle = (click + pop) * noise;

        oL[i] = l + bed + crackle * (1.0f - v.clickPan);
        oR[i] = r + bed + crackle * (1.0f + v.clickPan);
    }
}

void Medium::runSampler(const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
{
    auto& s = sampler;
    const float age = std::clamp(params.age, 0.0f, 1.0f);
    const float rate = 26000.0f * std::pow(0.3f, age);
    const float step = rate / static_cast<float>(fs);
    const float bits = 14.0f - 6.0f * age;
    const float levels = std::exp2(bits - 1.0f);
    const float driveGain = dbToGain(15.0f * params.drive);
    const float jitter = 0.15f * std::clamp(params.wobble, 0.0f, 1.0f);
    const float floorNoise = params.noise * dbToGain(-56.0f + 10.0f * age);

    for (int i = 0; i < n; ++i)
    {
        s.dl.push(iL[i]);
        s.dr.push(iR[i]);
        const float l = s.preL.processLow(s.dl.at(static_cast<std::size_t>(baseDelay)));
        const float r = s.preR.processLow(s.dr.at(static_cast<std::size_t>(baseDelay)));

        s.phase += step * (1.0f + jitter * s.rng.nextBipolar());
        if (s.phase >= 1.0f)
        {
            s.phase -= std::floor(s.phase);
            const float cl = std::clamp(l * driveGain, -1.0f, 1.0f);
            const float cr = std::clamp(r * driveGain, -1.0f, 1.0f);
            s.holdL = std::round(cl * levels) / levels / std::sqrt(driveGain);
            s.holdR = std::round(cr * levels) / levels / std::sqrt(driveGain);
        }

        oL[i] = s.postL.processLow(s.holdL) + s.rng.nextBipolar() * floorNoise;
        oR[i] = s.postR.processLow(s.holdR) + s.rng.nextBipolar() * floorNoise;
    }
}

void Medium::runModel(Type t, const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
{
    switch (t)
    {
        case Type::Digital: runDigital(iL, iR, oL, oR, n); break;
        case Type::Cassette: runCassette(iL, iR, oL, oR, n); break;
        case Type::Vinyl: runVinyl(iL, iR, oL, oR, n); break;
        case Type::Sampler: runSampler(iL, iR, oL, oR, n); break;
    }
}

void Medium::process(float* left, float* right, int numSamples) noexcept
{
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min(numSamples - done, maxBlock);
        float* l = left + done;
        float* r = right + done;
        std::copy_n(l, n, inL.data());
        std::copy_n(r, n, inR.data());

        runModel(params.type, inL.data(), inR.data(), curL.data(), curR.data(), n);
        const bool fading = fade < 1.0f && previousType != params.type;
        if (fading)
            runModel(previousType, inL.data(), inR.data(), prevL.data(), prevR.data(), n);

        const float mix = std::clamp(params.mix, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            dryL.push(inL[static_cast<size_t>(i)]);
            dryR.push(inR[static_cast<size_t>(i)]);
            float wl = curL[static_cast<size_t>(i)];
            float wr = curR[static_cast<size_t>(i)];
            if (fading)
            {
                fade = std::min(1.0f, fade + fadeStep);
                const float a = std::sin(fade * 0.5f * kPi);
                const float b = std::cos(fade * 0.5f * kPi);
                wl = wl * a + prevL[static_cast<size_t>(i)] * b;
                wr = wr * a + prevR[static_cast<size_t>(i)] * b;
            }
            const float dl = dryL.at(static_cast<std::size_t>(baseDelay));
            const float dr = dryR.at(static_cast<std::size_t>(baseDelay));
            l[i] = lerp(dl, wl, mix);
            r[i] = lerp(dr, wr, mix);
        }
        if (fade >= 1.0f)
            previousType = params.type;
        done += n;
    }
}

using Curve = DisplayMap::Curve;

const ProcessorInfo MediumProcessor::kInfo {
    "tf.medium", "Medium",
    { { { "Type", 0.375f, { Curve::Choice, 0.0f, 1.0f, "", 0, kTypeNames, 4 } },
        { "Age", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Noise", 0.4f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Wobble", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "Drive", 0.3f, { Curve::Linear, 0.0f, 100.0f, "%" } },
        { "-", 0.0f, { Curve::Hidden } } } },
    false
};

Medium::Type MediumProcessor::typeFrom01(float v) noexcept
{
    return static_cast<Medium::Type>(std::clamp(static_cast<int>(v * Medium::kNumTypes), 0, Medium::kNumTypes - 1));
}

void MediumProcessor::setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept
{
    Medium::Params p;
    p.type = typeFrom01(c[0]);
    p.age = c[1];
    p.noise = c[2];
    p.wobble = c[3];
    p.drive = c[4];
    p.mix = 1.0f;
    medium.setParams(p);
    medium.setTimeScale(ctx.timeScale);
}
}
