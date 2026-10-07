#include "DroneGenerator.h"

#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

namespace {

constexpr std::array<float, 6> kInitialIntervals { 0.0f, 12.0f, 7.0f, -12.0f, 19.0f, 24.0f };
constexpr std::array<float, 9> kIntervalPool { -12.0f, 0.0f, 5.0f, 7.0f, 12.0f, 14.0f, 17.0f, 19.0f, 24.0f };

// Seconds for a voice to fade out or in while re-voicing, before Tide.
constexpr float kRevoiceFadeSeconds = 4.0f;
// Seconds for density changes to be followed, before Tide.
constexpr float kDensityFadeSeconds = 3.0f;
// Mean seconds between re-voicing events per voice at evolve = 1.
constexpr float kRevoiceMeanSeconds = 25.0f;

inline double polyBlep(double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

} // namespace

void DroneGenerator::prepare(const ProcessSpec& newSpec, std::uint64_t seed)
{
    spec = newSpec;
    rng.setSeed(seed);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];
        const auto voiceSeed = seed * 31u + static_cast<std::uint64_t>(i) * 1009u;
        v.pitchDrift.setSeed(voiceSeed + 1);
        v.cutoffDrift.setSeed(voiceSeed + 2);
        v.panDrift.setSeed(voiceSeed + 3);
        v.ampDrift.setSeed(voiceSeed + 4);
        v.noise.setSeed(voiceSeed + 5);
        v.filter.prepare(spec.sampleRate);
        v.interval = v.pendingInterval = kInitialIntervals[static_cast<size_t>(i)];
        v.basePan = (i % 2 == 0 ? -1.0f : 1.0f) * (0.2f + 0.15f * static_cast<float>(i / 2));
        v.seed = (static_cast<float>(i) + 0.5f) / kMaxVoices; // voices migrate in a fixed order
        for (auto& ph : v.phase)
            ph = static_cast<double>(rng.nextFloat());
    }
    reset();
    snapPitch = true;
}

void DroneGenerator::reset() noexcept
{
    for (auto& v : voices)
    {
        v.filter.reset();
        v.densityGain = 0.0f;
        v.revoiceGain = 1.0f;
        v.revoicing = v.fadingIn = false;
        v.gainL = v.gainR = v.prevGainL = v.prevGainR = 0.0f;
        v.level = 0.0f;
    }
    samplesUntilControl = 0;
}

void DroneGenerator::updateControl(float dt) noexcept
{
    const auto& p = params;
    const float density = std::clamp(p.density, 0.0f, static_cast<float>(kMaxVoices));
    const float densityStep = dt / kDensityFadeSeconds;
    const float revoiceStep = dt / kRevoiceFadeSeconds;
    const float revoiceChance = std::clamp(p.evolve, 0.0f, 1.0f) * dt / kRevoiceMeanSeconds;
    const float depth = std::clamp(p.driftDepth, 0.0f, 1.0f);

    sineMix = std::clamp(p.shape, 0.0f, 1.0f);
    noiseGain = std::clamp(p.noise, 0.0f, 1.0f) * 0.35f;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];

        for (auto* d : { &v.pitchDrift, &v.cutoffDrift, &v.panDrift, &v.ampDrift })
        {
            d->setRate(p.driftRate);
            d->advance(dt);
        }

        // Density: voice i is fully on when density >= i + 1, partially in between.
        const float densityTarget = std::clamp(density - static_cast<float>(i), 0.0f, 1.0f);
        if (v.densityGain < densityTarget)
            v.densityGain = std::min(densityTarget, v.densityGain + densityStep);
        else
            v.densityGain = std::max(densityTarget, v.densityGain - densityStep);

        // Evolution: fade out, jump to a new interval while silent, fade back in.
        if (v.revoicing)
        {
            if (! v.fadingIn)
            {
                v.revoiceGain -= revoiceStep;
                if (v.revoiceGain <= 0.0f)
                {
                    v.revoiceGain = 0.0f;
                    v.interval = v.pendingInterval;
                    v.fadingIn = true;
                }
            }
            else
            {
                v.revoiceGain += revoiceStep;
                if (v.revoiceGain >= 1.0f)
                {
                    v.revoiceGain = 1.0f;
                    v.revoicing = v.fadingIn = false;
                }
            }
        }
        else if (i > 0 && v.densityGain > 0.0f && rng.chance(revoiceChance))
        {
            // Voice 0 holds the root so the drone keeps its centre.
            v.pendingInterval = kIntervalPool[static_cast<size_t>(rng.nextInt(static_cast<int>(kIntervalPool.size())))];
            v.revoicing = v.pendingInterval != v.interval;
        }

        // Pitch: root + interval pulled toward the key, gliding to new targets (key
        // changes, re-voicing), then slow drift (up to +-12 cents) and detune on top.
        float target = p.rootNote + v.interval;
        if (harmony != nullptr && p.gravity > 0.0f)
            target = harmony->quantize(target, v.seed, p.gravity);
        v.note = snapPitch ? target : v.note + (target - v.note) * std::min(1.0f, dt / 1.2f);
        const float note = v.note + depth * 0.12f * v.pitchDrift.getValue();
        const double baseHz = static_cast<double>(midiToHz(note));
        const double detune = static_cast<double>(p.detuneCents) / 1200.0;
        constexpr std::array<double, 3> spreadFactor { -1.0, 0.0, 1.0 };
        for (size_t o = 0; o < 3; ++o)
        {
            const double hz = baseHz * std::exp2(detune * spreadFactor[o]);
            v.increment[o] = std::min(hz / spec.sampleRate, 0.45);
        }

        // Filter: drifts up to +-1.5 octaves around the base cutoff.
        const float cutoff = p.cutoffHz * std::exp2(1.5f * depth * v.cutoffDrift.getValue());
        v.filter.setCutoff(cutoff, p.resonance);

        // Amplitude and pan.
        const float ampDrift = dbToGain(3.0f * depth * v.ampDrift.getValue());
        const float amp = v.densityGain * smoothstep(v.revoiceGain) * ampDrift;
        const float pan = std::clamp(p.spread * (v.basePan + 0.4f * depth * v.panDrift.getValue()), -1.0f, 1.0f);
        const auto gains = equalPowerPan(pan);

        v.prevGainL = v.gainL;
        v.prevGainR = v.gainR;
        v.gainL = amp * gains.left;
        v.gainR = amp * gains.right;
        v.level = amp;
    }
    snapPitch = false;
}

float DroneGenerator::renderVoiceSample(Voice& v) noexcept
{
    float saw = 0.0f;
    float sine = 0.0f;
    for (size_t o = 0; o < 3; ++o)
    {
        const double t = v.phase[o];
        const double dt = v.increment[o];
        saw += static_cast<float>(2.0 * t - 1.0 - polyBlep(t, dt));
        sine += fastSin01(static_cast<float>(t));
        double next = t + dt;
        if (next >= 1.0)
            next -= 1.0;
        v.phase[o] = next;
    }
    const float osc = lerp(saw, sine, sineMix) * (1.0f / 3.0f);
    return v.filter.processLow(osc + v.noise.nextBipolar() * noiseGain);
}

void DroneGenerator::process(float* left, float* right, int numSamples, float timeScale) noexcept
{
    // Sized so the default patch (3 voices, -3 dB pan law) sits near -20 dBFS RMS with
    // peaks around -8 dBFS: healthy level into the master without leaning on the limiter.
    constexpr float kVoiceGain = 0.55f;
    int i = 0;
    while (i < numSamples)
    {
        if (samplesUntilControl <= 0)
        {
            const float dt = static_cast<float>(kControlInterval / spec.sampleRate) * std::max(timeScale, 0.0f);
            updateControl(dt);
            samplesUntilControl = kControlInterval;
        }

        const int chunk = std::min(numSamples - i, samplesUntilControl);
        const int chunkStart = kControlInterval - samplesUntilControl;

        for (int s = 0; s < chunk; ++s)
        {
            // Interpolate gains across the control interval to avoid steps.
            const float frac = static_cast<float>(chunkStart + s + 1) / static_cast<float>(kControlInterval);
            float outL = 0.0f;
            float outR = 0.0f;
            for (auto& v : voices)
            {
                if (v.gainL == 0.0f && v.gainR == 0.0f && v.prevGainL == 0.0f && v.prevGainR == 0.0f)
                    continue;
                const float y = renderVoiceSample(v);
                outL += y * lerp(v.prevGainL, v.gainL, frac);
                outR += y * lerp(v.prevGainR, v.gainR, frac);
            }
            left[i + s] = outL * kVoiceGain;
            right[i + s] = outR * kVoiceGain;
        }

        i += chunk;
        samplesUntilControl -= chunk;
    }
}

float DroneGenerator::getVoiceLevel(int voice) const noexcept
{
    return voice >= 0 && voice < kMaxVoices ? voices[static_cast<size_t>(voice)].level : 0.0f;
}

float DroneGenerator::getVoiceInterval(int voice) const noexcept
{
    return voice >= 0 && voice < kMaxVoices ? voices[static_cast<size_t>(voice)].interval : 0.0f;
}

float DroneGenerator::getVoiceNote(int voice) const noexcept
{
    return voice >= 0 && voice < kMaxVoices ? voices[static_cast<size_t>(voice)].note : 0.0f;
}

} // namespace tf::dsp
