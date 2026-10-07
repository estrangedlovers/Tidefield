#pragma once

#include "../Processor.h"

#include "../../core/DelayLine.h"
#include "../../core/MathUtil.h"
#include "../../core/Random.h"
#include "../../filters/Biquad.h"
#include "../../filters/OnePole.h"
#include "../../filters/Svf.h"
#include "../../mod/Drift.h"

#include <array>
#include <vector>

namespace tf::dsp {

/** The "recording type": makes everything sound printed to a medium.

      Digital        clean
      Cassette       hiss, wow and flutter, head bump, HF loss with age, asymmetric
                     tape saturation, crosstalk, dropouts when worn
      Vinyl          crackle and pops, surface noise, rumble, 33 rpm wow, mono lows,
                     inner-groove HF loss
      Noisy sampler  sample-rate reduction, 8 to 14-bit quantisation, input clipping,
                     noise floor, warm reconstruction filter

    Every model runs behind the same fixed base delay (kBaseDelayMs), so dry/wet mix
    and type crossfades are phase-coherent. Changing type crossfades over 300 ms.

    Controls are in 0..1. Noise is the medium's own noise (hiss, crackle, floor); it
    is independent of the input, so a cassette hisses even in silence, as it should. */
class Medium
{
public:
    enum class Type : int { Digital = 0, Cassette = 1, Vinyl = 2, Sampler = 3 };
    static constexpr int kNumTypes = 4;
    static constexpr float kBaseDelayMs = 3.0f;
    static constexpr float kCrossfadeSeconds = 0.3f;

    struct Params
    {
        Type type = Type::Digital;
        float age = 0.3f;     // how worn: HF loss, dropouts, bit depth, crackle rate
        float noise = 0.4f;   // level of the medium's own noise
        float wobble = 0.3f;  // wow/flutter (tape, vinyl), clock jitter (sampler)
        float drive = 0.3f;   // saturation / clipping
        float mix = 1.0f;     // dry/wet
    };

    static const char* typeName(Type t) noexcept;

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;
    void setParams(const Params& p) noexcept;
    void setTimeScale(float t) noexcept { timeScale = t; }
    void process(float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return baseDelay; }
    Type getType() const noexcept { return params.type; }

private:
    struct Cassette
    {
        DelayLine dl, dr;
        Drift wow;
        float flutterPhase = 0.0f;
        Biquad bumpL, bumpR;
        Svf hfL, hfR;
        OnePole hissHp, hissLp;
        Random rng;
        TanhAdaa satL, satR; // anti-aliased tape saturation
        float dropoutGain = 1.0f, dropoutTarget = 1.0f;
        int dropoutRemaining = 0;
    };

    struct Vinyl
    {
        DelayLine dl, dr;
        float wowPhase = 0.0f;
        Drift wowDrift;
        Svf hfL, hfR;
        OnePole sideHp, crackleHp, rumbleLp, surfaceLp;
        Random rng;
        float clickEnv = 0.0f, clickSign = 1.0f, clickPan = 0.0f;
        float popEnv = 0.0f;
        OnePole popLp;
    };

    struct Sampler
    {
        DelayLine dl, dr;
        Svf preL, preR, postL, postR;
        Random rng;
        float phase = 0.0f, holdL = 0.0f, holdR = 0.0f;
    };

    void runModel(Type t, const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runCassette(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runVinyl(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runSampler(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runDigital(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

    double fs = 48000.0;
    int maxBlock = 512;
    int baseDelay = 144;
    Params params;
    Type previousType = Type::Digital;
    float fade = 1.0f;       // 0 -> 1 while crossfading previous -> current
    float fadeStep = 0.0f;
    float timeScale = 1.0f;

    Cassette cassette;
    Vinyl vinyl;
    Sampler sampler;
    DelayLine digitalL, digitalR, dryL, dryR;
    std::vector<float> inL, inR, curL, curR, prevL, prevR;
};

/** Medium as an FX-slot processor (inserts, delay feedback experiments). p1 selects
    the type in four bands; p2..p5 are age, noise, wobble, drive; p6 is unused. */
class MediumProcessor final : public Processor
{
public:
    static const ProcessorInfo kInfo;

    const ProcessorInfo& info() const noexcept override { return kInfo; }
    void prepare(const ProcessSpec& spec) override { medium.prepare(spec, 0x6d656469ull); }
    void reset() noexcept override { medium.reset(); }
    void setControls(const std::array<float, 6>& c, const ModContext& ctx) noexcept override;
    void process(float* left, float* right, int n) noexcept override { medium.process(left, right, n); }
    int getLatencySamples() const noexcept override { return medium.getLatencySamples(); }

    static Medium::Type typeFrom01(float v) noexcept;

private:
    Medium medium;
};

} // namespace tf::dsp
