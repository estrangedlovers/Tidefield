#include "WeatherBed.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
void WeatherBed::prepare(const ProcessSpec& s, std::uint64_t seed)
{
    spec = s;
    rng.setSeed(seed);
    gustDrift.setSeed(seed * 3u + 1u);
    centreDrift.setSeed(seed * 5u + 2u);
    for (auto* f : { &windL, &windR, &whistle })
        f->prepare(spec.sampleRate);
    for (auto* f : { &rumbleL, &rumbleR, &hissHpL, &hissHpR, &hissLpL, &hissLpR, &surfL, &surfR, &washL, &washR, &farL, &farR })
        f->prepare(spec.sampleRate);
    rumbleL.setCutoff(110.0f);
    rumbleR.setCutoff(110.0f);
    hissHpL.setCutoff(1500.0f);
    hissHpR.setCutoff(1500.0f);
    washL.setCutoff(380.0f);
    washR.setCutoff(380.0f);
    reset();
}

void WeatherBed::reset() noexcept
{
    for (auto* f : { &windL, &windR, &whistle })
        f->reset();
    for (auto* f : { &rumbleL, &rumbleR, &hissHpL, &hissHpR, &hissLpL, &hissLpR, &surfL, &surfR, &washL, &washR, &farL, &farR })
        f->reset();
    for (auto& d : drops)
        d.active = false;
    untilControl = 0;
    wavePhase = rng.nextFloat();
    wavePeriod = rng.nextRange(7.0f, 13.0f);
    waveEnv = 0.0f;
}

void WeatherBed::control(float dt) noexcept
{
    const auto& p = params;
    const float tone = std::clamp(p.tone, 0.0f, 1.0f);

    gustDrift.setRate(0.04f + 0.25f * p.gust);
    centreDrift.setRate(0.03f + 0.1f * p.gust);
    const float g = gustDrift.advance(dt);
    gustEnv = std::clamp(0.55f + 0.6f * p.gust * g, 0.05f, 1.0f);
    const float centre = 220.0f * std::pow(4.0f, 0.5f + 0.5f * centreDrift.advance(dt)) * (0.6f + 0.9f * tone) * (0.7f + 0.6f * gustEnv);
    windL.setCutoff(centre, 0.25f + 0.3f * gustEnv);
    windR.setCutoff(centre * 1.07f, 0.25f + 0.3f * gustEnv);
    whistle.setCutoff(centre * 2.3f, 0.97f);
    windGain = p.wind * gustEnv;
    whistleGain = p.wind * std::max(0.0f, gustEnv - 0.7f) * 1.2f;

    hissLpL.setCutoff(3500.0f + 7000.0f * tone);
    hissLpR.setCutoff(3500.0f + 7000.0f * tone);
    dropRate = p.rain > 0.0f ? (3.0f + 160.0f * std::pow(p.rain, 1.5f)) / static_cast<float>(spec.sampleRate) : 0.0f;

    wavePhase += dt / wavePeriod;
    if (wavePhase >= 1.0f)
    {
        wavePhase -= 1.0f;
        wavePeriod = rng.nextRange(7.0f, 13.0f);
        waveAmp = rng.nextRange(0.55f, 1.0f);
        wavePan = rng.nextRange(-0.6f, 0.6f);
    }
    constexpr float kCrash = 0.15f;
    waveEnv = waveAmp * (wavePhase < kCrash ? smoothstep(wavePhase / kCrash) : std::exp(-(wavePhase - kCrash) * 4.0f));
    const float surfCut = 250.0f + 3200.0f * waveEnv * (0.5f + tone);
    surfL.setCutoff(surfCut * (1.0f + 0.1f * wavePan));
    surfR.setCutoff(surfCut * (1.0f - 0.1f * wavePan));

    const float far = std::clamp(p.distance, 0.0f, 1.0f);
    const float farCut = 18000.0f * std::pow(0.08f, far);
    farL.setCutoff(farCut);
    farR.setCutoff(farCut);
    width = 1.0f - 0.7f * far;
}

void WeatherBed::spawnDrop() noexcept
{
    for (auto& d : drops)
        if (! d.active)
        {
            const float tone = std::clamp(params.tone, 0.0f, 1.0f);
            const float fs = static_cast<float>(spec.sampleRate);
            d.active = true;
            d.phase = 0.0f;
            d.freq = rng.nextRange(1200.0f, 2600.0f + 2800.0f * tone) / fs;
            d.chirp = 1.0f + rng.nextRange(1.0e-5f, 6.0e-5f) * (48000.0f / fs);
            d.amp = rng.nextRange(0.05f, 0.35f) * params.rain;
            d.decay = std::exp(-1.0f / (rng.nextRange(0.004f, 0.018f) * fs));
            const float pan = rng.nextBipolar();
            d.gainL = std::sqrt(0.5f * (1.0f - pan));
            d.gainR = std::sqrt(0.5f * (1.0f + pan));
            return;
        }
}

void WeatherBed::process(float* left, float* right, int n, float timeScale) noexcept
{
    const float dtControl = static_cast<float>(kControlInterval / spec.sampleRate) * std::max(0.0f, timeScale);
    const float panL = std::sqrt(0.5f * (1.0f - wavePan));
    const float panR = std::sqrt(0.5f * (1.0f + wavePan));
    for (int i = 0; i < n; ++i)
    {
        if (--untilControl <= 0)
        {
            control(dtControl);
            untilControl = kControlInterval;
        }

        float l = 0.0f, r = 0.0f;

        if (params.wind > 0.0f)
        {
            const float nl = rng.nextBipolar();
            const float nr = rng.nextBipolar();
            l += windGain * (2.2f * windL.process(nl).band + 0.6f * rumbleL.processLow(nl));
            r += windGain * (2.2f * windR.process(nr).band + 0.6f * rumbleR.processLow(nr));
            const float w = whistleGain * whistle.process(0.5f * (nl + nr)).band;
            l += w;
            r += w;
        }

        if (params.rain > 0.0f || dropRate > 0.0f)
        {
            const float hiss = 0.12f * params.rain;
            l += hiss * hissLpL.processLow(hissHpL.processHigh(rng.nextBipolar()));
            r += hiss * hissLpR.processLow(hissHpR.processHigh(rng.nextBipolar()));
            if (dropRate > 0.0f && rng.chance(dropRate))
                spawnDrop();
            for (auto& d : drops)
            {
                if (! d.active)
                    continue;
                const float s = fastSin01(d.phase) * d.amp;
                l += s * d.gainL;
                r += s * d.gainR;
                d.phase += d.freq;
                d.phase -= std::floor(d.phase);
                d.freq *= d.chirp;
                d.amp *= d.decay;
                if (d.amp < 1.0e-4f)
                    d.active = false;
            }
        }

        if (params.surf > 0.0f)
        {
            const float nl = rng.nextBipolar();
            const float nr = rng.nextBipolar();
            const float crash = params.surf * waveEnv * 0.9f;
            l += crash * panL * surfL.processLow(nl) + params.surf * 0.25f * washL.processLow(nl);
            r += crash * panR * surfR.processLow(nr) + params.surf * 0.25f * washR.processLow(nr);
        }

        l = farL.processLow(l);
        r = farR.processLow(r);
        const float mid = 0.5f * (l + r);
        left[i] = flushDenormal(mid + width * (l - mid));
        right[i] = flushDenormal(mid + width * (r - mid));
    }
}
}
