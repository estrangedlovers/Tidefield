#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/OnePole.h"
#include "../../filters/Svf.h"
#include "../../mod/Drift.h"

#include <array>

namespace tf::dsp {
class WeatherBed
{
public:
    struct Params
    {
        float wind = 0.5f;
        float rain = 0.0f;
        float surf = 0.0f;
        float gust = 0.5f;
        float tone = 0.5f;
        float distance = 0.3f;
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }
    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    float getGust() const noexcept { return gustEnv; }
    float getWave() const noexcept { return waveEnv; }

private:
    static constexpr int kControlInterval = 32;
    static constexpr int kMaxDrops = 12;
    static constexpr float kMeanGust = 0.55f;

    struct Drop
    {
        bool active = false;
        float phase = 0.0f, freq = 0.0f, chirp = 1.0f, amp = 0.0f, decay = 0.99f, gainL = 0.0f, gainR = 0.0f;
    };

    void control(float dt) noexcept;
    void spawnDrop() noexcept;

    ProcessSpec spec;
    Params params;
    Random rng;
    int untilControl = 0;

    Drift gustDrift, centreDrift;
    Svf windL, windR, whistle;
    OnePole rumbleL, rumbleR;
    float gustEnv = 0.5f, windGain = 0.0f, whistleGain = 0.0f;

    OnePole hissHpL, hissHpR, hissLpL, hissLpR;
    std::array<Drop, kMaxDrops> drops {};
    float dropRate = 0.0f;
    float rainGust = 1.0f;

    OnePole surfL, surfR, washL, washR;
    float wavePhase = 0.0f, wavePeriod = 9.0f, waveAmp = 1.0f, wavePan = 0.0f, waveEnv = 0.0f;

    OnePole farL, farR;
    float width = 1.0f;
};
}
