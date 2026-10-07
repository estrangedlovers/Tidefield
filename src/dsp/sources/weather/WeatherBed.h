#pragma once

#include "../../core/ProcessSpec.h"
#include "../../core/Random.h"
#include "../../filters/OnePole.h"
#include "../../filters/Svf.h"
#include "../../mod/Drift.h"

#include <array>

namespace tf::dsp {

/** A procedural weather bed: wind, rain and surf from shaped noise, never looping.

      Wind   band-passed noise whose level and centre follow a slow gust process;
             strong gusts open a faint whistle and a low rumble
      Rain   a steady high hiss plus individual drops (short rising "plinks"), denser
             as rain goes up
      Surf   waves of low-passed noise that crash and recede on 7-13 s cycles, each
             from its own side of the stereo field

    `distance` pulls the whole bed back (darker, narrower). Gust and wave rates follow
    Tide. Realtime-safe; nothing is allocated. */
class WeatherBed
{
public:
    struct Params
    {
        float wind = 0.5f;     // 0..1 levels
        float rain = 0.0f;
        float surf = 0.0f;
        float gust = 0.5f;     // 0..1 how restless the wind is
        float tone = 0.5f;     // 0 dark .. 1 bright
        float distance = 0.3f; // 0 close .. 1 far
    };

    void prepare(const ProcessSpec& spec, std::uint64_t seed);
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }
    void process(float* left, float* right, int numSamples, float timeScale) noexcept;

    /** 0..1: current gust strength and surf wave envelope, for visuals. */
    float getGust() const noexcept { return gustEnv; }
    float getWave() const noexcept { return waveEnv; }

private:
    static constexpr int kControlInterval = 32;
    static constexpr int kMaxDrops = 12;

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

    // Wind.
    Drift gustDrift, centreDrift;
    Svf windL, windR, whistle;
    OnePole rumbleL, rumbleR;
    float gustEnv = 0.5f, windGain = 0.0f, whistleGain = 0.0f;

    // Rain.
    OnePole hissHpL, hissHpR, hissLpL, hissLpR;
    std::array<Drop, kMaxDrops> drops {};
    float dropRate = 0.0f; // per sample

    // Surf.
    OnePole surfL, surfR, washL, washR;
    float wavePhase = 0.0f, wavePeriod = 9.0f, waveAmp = 1.0f, wavePan = 0.0f, waveEnv = 0.0f;

    // Distance.
    OnePole farL, farR;
    float width = 1.0f;
};

} // namespace tf::dsp
