#include "DroneGenerator.h"

#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
struct ChordSet
{
    const char* name;
    std::array<float, 6> initial;
    std::array<float, 9> pool;
    int poolSize;
};

constexpr std::array<ChordSet, DroneGenerator::kNumChords> kChords { {
    { "Open", { 0.0f, 12.0f, 7.0f, -12.0f, 19.0f, 24.0f }, { -12.0f, 0.0f, 5.0f, 7.0f, 12.0f, 14.0f, 17.0f, 19.0f, 24.0f }, 9 },
    { "Fifths", { 0.0f, 7.0f, 12.0f, -12.0f, 19.0f, 24.0f }, { -12.0f, -5.0f, 0.0f, 7.0f, 12.0f, 19.0f, 24.0f }, 7 },
    { "Octaves", { 0.0f, 12.0f, -12.0f, 24.0f, 0.0f, 12.0f }, { -12.0f, 0.0f, 12.0f, 24.0f }, 4 },
    { "Minor", { 0.0f, 7.0f, 15.0f, -12.0f, 12.0f, 22.0f }, { -12.0f, 0.0f, 3.0f, 7.0f, 10.0f, 12.0f, 15.0f, 19.0f, 22.0f }, 9 },
    { "Major", { 0.0f, 7.0f, 16.0f, -12.0f, 12.0f, 23.0f }, { -12.0f, 0.0f, 4.0f, 7.0f, 11.0f, 12.0f, 16.0f, 19.0f, 23.0f }, 9 },
    { "Suspended", { 0.0f, 7.0f, 14.0f, -12.0f, 12.0f, 17.0f }, { -12.0f, 0.0f, 2.0f, 5.0f, 7.0f, 12.0f, 14.0f, 17.0f, 19.0f }, 9 },
    { "Cluster", { 0.0f, 2.0f, 3.0f, -12.0f, 5.0f, 7.0f }, { -12.0f, 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 8.0f }, 8 },
    { "Harmonics", { 0.0f, 12.0f, 19.02f, -12.0f, 24.0f, 27.86f }, { -12.0f, 0.0f, 12.0f, 19.02f, 24.0f, 27.86f, 31.02f, 33.69f, 36.0f }, 9 },
} };

constexpr std::array<const char*, DroneGenerator::kNumWaves> kWaveNames { "Classic", "Pulse", "Fold", "Organ", "FM" };

constexpr float kDensityFadeSeconds = 3.0f;
constexpr float kWaveFadeSeconds = 0.08f;

inline double wrap01(double x) noexcept { return x - std::floor(x); }
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
}

const char* DroneGenerator::waveName(int w) noexcept { return kWaveNames[static_cast<std::size_t>(std::clamp(w, 0, kNumWaves - 1))]; }

const char* DroneGenerator::chordName(int c) noexcept { return kChords[static_cast<std::size_t>(std::clamp(c, 0, kNumChords - 1))].name; }

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
        v.interval = v.pendingInterval = kChords[0].initial[static_cast<size_t>(i)];
        v.basePan = (i % 2 == 0 ? -1.0f : 1.0f) * (0.2f + 0.15f * static_cast<float>(i / 2));
        v.seed = (static_cast<float>(i) + 0.5f) / kMaxVoices;
        for (auto& ph : v.phase)
            ph = static_cast<double>(rng.nextFloat());
        v.modPhase = v.phase;
    }
    chord = 0;
    wave = previousWave = 0;
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
        v.breath = 0.0f;
    }
    waveFade = 1.0f;
    filterWeight = { 1.0f, 0.0f, 0.0f };
    filterType = 0;
    subGain = prevSubGain = 0.0f;
    driveL.reset();
    driveR.reset();
    samplesUntilControl = 0;
    snapPitch = true;
}

void DroneGenerator::updateControl(float dt, float real) noexcept
{
    const auto& p = params;
    const float density = std::clamp(p.density, 0.0f, static_cast<float>(kMaxVoices));
    const float densityStep = dt / kDensityFadeSeconds;
    const float revoiceStep = dt / std::clamp(p.revoiceSeconds, 0.1f, 60.0f);
    const float revoiceChance = std::clamp(p.evolve, 0.0f, 1.0f) * dt / kRevoiceMeanSeconds;
    const float depth = std::clamp(p.driftDepth, 0.0f, 1.0f);

    sineMix = std::clamp(p.shape, 0.0f, 1.0f);
    noiseGain = std::clamp(p.noise, 0.0f, 1.0f) * 0.35f;
    const float tone = std::clamp(p.breathTone, 0.0f, 1.0f);
    breathCoef = tone >= 0.999f ? 1.0f
                                : 1.0f - std::exp(-6.28318530718f * 150.0f * std::pow(120.0f, tone) / static_cast<float>(spec.sampleRate));
    breathMakeup = std::min(4.0f, 1.0f / std::sqrt(breathCoef / (2.0f - breathCoef)));
    fmRatio = std::clamp(p.fmRatio, 0.25f, 16.0f);
    filterType = std::clamp(p.filterType, 0, 2);
    filterStep = 1.0f / (0.03f * static_cast<float>(spec.sampleRate));

    const int wantedWave = std::clamp(p.wave, 0, kNumWaves - 1);
    if (wantedWave != wave && waveFade >= 1.0f)
    {
        previousWave = wave;
        wave = wantedWave;
        waveFade = 0.0f;
    }
    waveFadeStep = 1.0f / (kWaveFadeSeconds * static_cast<float>(spec.sampleRate));

    const int wantedChord = std::clamp(p.chord, 0, kNumChords - 1);
    const bool chordChanged = wantedChord != chord;
    chord = wantedChord;
    const auto& set = kChords[static_cast<std::size_t>(chord)];

    vibratoPhase = wrap01(vibratoPhase + static_cast<double>(std::clamp(p.vibratoRate, 0.0f, 20.0f) * real));
    tremoloPhase = wrap01(tremoloPhase + static_cast<double>(std::clamp(p.tremoloRate, 0.0f, 20.0f) * real));
    const float vibrato = std::clamp(p.vibrato, 0.0f, 1.0f) * 0.5f;
    const float tremolo = std::clamp(p.tremolo, 0.0f, 1.0f);
    const float tilt = std::clamp(p.tilt, 0.0f, 1.0f);
    const float glide = std::clamp(p.glideSeconds, 0.01f, 60.0f);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];

        for (auto* d : { &v.pitchDrift, &v.cutoffDrift, &v.panDrift, &v.ampDrift })
        {
            d->setRate(p.driftRate);
            d->advance(dt);
        }

        const float densityTarget = std::clamp(density - static_cast<float>(i), 0.0f, 1.0f);
        if (v.densityGain < densityTarget)
            v.densityGain = std::min(densityTarget, v.densityGain + densityStep);
        else
            v.densityGain = std::max(densityTarget, v.densityGain - densityStep);

        const bool wasRevoicing = v.revoicing;
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
        if (chordChanged && i > 0)
        {
            v.pendingInterval = set.initial[static_cast<size_t>(i)];
            if (v.pendingInterval != v.interval)
            {
                v.revoicing = true;
                v.fadingIn = false;
            }
        }
        else if (! wasRevoicing && i > 0 && v.densityGain > 0.0f && rng.chance(revoiceChance))
        {
            v.pendingInterval = set.pool[static_cast<size_t>(rng.nextInt(set.poolSize))];
            v.revoicing = v.pendingInterval != v.interval;
        }

        float target = p.rootNote + v.interval;
        if (harmony != nullptr && p.gravity > 0.0f)
            target = harmony->quantize(target, v.seed, p.gravity);
        v.note = snapPitch ? target : v.note + (target - v.note) * std::min(1.0f, dt / glide);
        const float vib = vibrato > 0.0f ? vibrato * fastSin01(static_cast<float>(wrap01(vibratoPhase + static_cast<double>(v.seed)))) : 0.0f;
        const float note = v.note + depth * 0.12f * v.pitchDrift.getValue() + vib;
        const double baseHz = static_cast<double>(midiToHz(note));
        const double detune = static_cast<double>(p.detuneCents) / 1200.0;
        constexpr std::array<double, 3> spreadFactor { -1.0, 0.0, 1.0 };
        for (size_t o = 0; o < 3; ++o)
        {
            const double hz = baseHz * std::exp2(detune * spreadFactor[o]);
            v.increment[o] = std::min(hz / spec.sampleRate, 0.45);
        }

        const double modStep = v.increment[1] * static_cast<double>(fmRatio);
        const double fullIndex = 0.75 * static_cast<double>(sineMix);
        const double maxIndex = modStep > 0.0 ? std::max(0.0, 0.45 / modStep - 1.0) / 6.283185307179586 : fullIndex;
        v.fmIndex = std::min(fullIndex, maxIndex);

        const float track = std::clamp(p.keyTrack, 0.0f, 1.0f) * (v.note - p.rootNote) / 12.0f;
        const float cutoff = p.cutoffHz * std::exp2(1.5f * depth * v.cutoffDrift.getValue() + track);
        v.filter.setCutoff(cutoff, p.resonance);

        const float ampDrift = dbToGain(3.0f * depth * v.ampDrift.getValue());
        const float above = std::max(0.0f, v.interval) / 12.0f;
        const float tiltGain = tilt > 0.0f ? std::pow(1.0f - 0.65f * tilt, above) : 1.0f;
        const float trem = tremolo > 0.0f
                               ? 1.0f - tremolo * 0.5f * (1.0f + fastSin01(static_cast<float>(wrap01(tremoloPhase + 0.06 * static_cast<double>(i)))))
                               : 1.0f;
        const float amp = v.densityGain * smoothstep(v.revoiceGain) * ampDrift * tiltGain * trem;
        const float pan = std::clamp(p.spread * (v.basePan + 0.4f * depth * v.panDrift.getValue()), -1.0f, 1.0f);
        const auto gains = equalPowerPan(pan);

        v.prevGainL = v.gainL;
        v.prevGainR = v.gainR;
        v.gainL = amp * gains.left;
        v.gainR = amp * gains.right;
        v.level = amp;
    }

    const auto& root = voices[0];
    subIncrement = std::min(static_cast<double>(midiToHz(root.note - 12.0f)) / spec.sampleRate, 0.45);
    prevSubGain = subGain;
    subGain = std::clamp(p.sub, 0.0f, 1.0f) * 0.9f * root.densityGain;
    prevDriveGain = driveGain;
    prevDriveAmount = driveAmount;
    const float drive = std::clamp(p.drive, 0.0f, 1.0f);
    driveGain = 1.0f + 40.0f * drive * drive;
    driveAmount = std::min(1.0f, drive * 5.0f);
    snapPitch = false;
}

float DroneGenerator::waveSample(int w, double t, double dt, double tm, double fmIndex) const noexcept
{
    const float shape = sineMix;
    switch (static_cast<Wave>(w))
    {
        case Wave::Classic:
        {
            const float saw = static_cast<float>(1.0 - 2.0 * t + polyBlep(t, dt));
            return lerp(saw, fastSin01(static_cast<float>(t)), shape);
        }
        case Wave::Pulse:
        {
            const double width = 0.5 - 0.45 * static_cast<double>(shape);
            double y = t < width ? 1.0 : -1.0;
            y += polyBlep(t, dt);
            y -= polyBlep(wrap01(t - width + 1.0), dt);
            return static_cast<float>(y - (2.0 * width - 1.0));
        }
        case Wave::Fold:
        {
            const float sine = fastSin01(static_cast<float>(t));
            const float gain = 1.0f + 5.0f * shape;
            return fastSin01(static_cast<float>(wrap01(0.25 * static_cast<double>(gain * sine))));
        }
        case Wave::Organ:
        {
            const float slope = 2.2f - 2.0f * shape;
            float sum = 0.0f, weights = 0.0f;
            for (int k = 1; k <= 6; ++k)
            {
                if (static_cast<double>(k) * dt > 0.45)
                    break;
                const float w8 = std::pow(static_cast<float>(k), -slope);
                sum += w8 * fastSin01(static_cast<float>(wrap01(t * static_cast<double>(k))));
                weights += w8;
            }
            return weights > 0.0f ? 1.2f * sum / weights : 0.0f;
        }
        case Wave::Fm:
        {
            return fastSin01(static_cast<float>(wrap01(t + fmIndex * static_cast<double>(fastSin01(static_cast<float>(tm))))));
        }
        default: return 0.0f;
    }
}

float DroneGenerator::renderVoiceSample(Voice& v) noexcept
{
    float osc = 0.0f;
    const bool fading = waveFade < 1.0f;
    const double modRatio = static_cast<double>(fmRatio);
    for (size_t o = 0; o < 3; ++o)
    {
        const double t = v.phase[o];
        const double dt = v.increment[o];
        const double tm = v.modPhase[o];
        float y = waveSample(wave, t, dt, tm, v.fmIndex);
        if (fading)
            y = lerp(waveSample(previousWave, t, dt, tm, v.fmIndex), y, waveFade);
        osc += y;
        double next = t + dt;
        if (next >= 1.0)
            next -= 1.0;
        v.phase[o] = next;
        v.modPhase[o] = wrap01(tm + std::min(dt * modRatio, 0.45));
    }
    osc *= 1.0f / 3.0f;
    float breath = v.noise.nextBipolar();
    if (breathCoef < 1.0f)
    {
        v.breath += breathCoef * (breath - v.breath);
        breath = v.breath * breathMakeup;
    }
    const auto out = v.filter.process(osc + breath * noiseGain);
    return filterWeight[0] * out.low + filterWeight[1] * 1.6f * out.band + filterWeight[2] * out.high;
}

void DroneGenerator::process(float* left, float* right, int numSamples, float timeScale) noexcept
{
    constexpr float kVoiceGain = 0.3f;
    constexpr float kDriveReference = 0.12f;
    int i = 0;
    while (i < numSamples)
    {
        if (samplesUntilControl <= 0)
        {
            const float real = static_cast<float>(kControlInterval / spec.sampleRate);
            updateControl(real * std::max(timeScale, 0.0f), real);
            samplesUntilControl = kControlInterval;
        }

        const int chunk = std::min(numSamples - i, samplesUntilControl);
        const int chunkStart = kControlInterval - samplesUntilControl;

        for (int s = 0; s < chunk; ++s)
        {
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
            if (waveFade < 1.0f)
                waveFade = std::min(1.0f, waveFade + waveFadeStep);
            for (int f = 0; f < 3; ++f)
            {
                auto& w = filterWeight[static_cast<std::size_t>(f)];
                w = f == filterType ? std::min(1.0f, w + filterStep) : std::max(0.0f, w - filterStep);
            }
            outL *= kVoiceGain;
            outR *= kVoiceGain;
            const float sg = lerp(prevSubGain, subGain, frac);
            if (sg > 0.0f || prevSubGain > 0.0f)
            {
                const float sub = fastSin01(static_cast<float>(subPhase)) * sg * kVoiceGain;
                outL += sub;
                outR += sub;
            }
            subPhase = wrap01(subPhase + subIncrement);
            const float amount = lerp(prevDriveAmount, driveAmount, frac);
            if (amount > 0.0f)
            {
                const float g = lerp(prevDriveGain, driveGain, frac);
                const float makeup = kDriveReference / std::tanh(kDriveReference * g) / (1.0f + 0.03f * (g - 1.0f));
                outL += amount * (driveL.process(outL * g) * makeup - outL);
                outR += amount * (driveR.process(outR * g) * makeup - outR);
            }
            left[i + s] = outL;
            right[i + s] = outR;
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
}
