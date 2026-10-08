#include "ResonatorBank.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
constexpr int kControlInterval = 32;
constexpr std::array<float, 8> kBellRatios { 1.0f, 2.0f, 2.4f, 3.0f, 4.08f, 5.43f, 6.8f, 8.2f };
}

void ResonatorBank::prepare(const ProcessSpec& newSpec, std::uint64_t seed)
{
    spec = newSpec;
    rng.setSeed(seed);
    tuningDrift.setSeed(seed + 5);
    tuningDrift.setRate(0.02f);
    followAttack = onePoleCoefficient(0.005f, spec.sampleRate);
    followRelease = onePoleCoefficient(0.3f, spec.sampleRate);
    for (int i = 0; i < kMaxModes; ++i)
    {
        auto& m = modes[static_cast<size_t>(i)];
        m.seed = rng.nextFloat();
        m.amp = 0.6f + 0.4f * rng.nextFloat();
        const float pan = rng.nextBipolar();
        const auto g = equalPowerPan(pan);
        m.gainL = g.left;
        m.gainR = g.right;
    }
    reset();
    snapNextUpdate = true;
}

void ResonatorBank::reset() noexcept
{
    for (auto& m : modes)
        m.y1 = m.y2 = m.x1 = m.x2 = m.level = 0.0f;
    strikePos = strikeLength = 0;
    strikeAmp = 0.0f;
    outputLevel = 0.0f;
    samplesToBurst = 0.0;
    samplesUntilControl = 0;
    ringingModes = 0;
    pendingStrike = 0.0f;
    snapNextUpdate = true;
}

float ResonatorBank::modeTargetNote(int i) const noexcept
{
    const float root = params.rootNote;
    const float fi = static_cast<float>(i);

    const float harmonic = root + 12.0f * std::log2(fi + 1.0f);

    float chordal = root + fi;
    if (harmony != nullptr)
    {
        const auto& scale = harmony->scaleFor(modes[static_cast<size_t>(i)].seed);
        const int base = static_cast<int>(std::lround(root));
        const int rootNote = base - (((base - scale.root) % 12 + 12) % 12);
        chordal = scale.degreeToNote(rootNote - 12, i * 2 - 2);
    }

    const float ratio = kBellRatios[static_cast<size_t>(i % 8)] * std::exp2(static_cast<float>(i / 8));
    const float bell = root + 12.0f * std::log2(ratio);

    const float s = std::clamp(params.structure, 0.0f, 1.0f);
    float note = s < 0.5f ? lerp(harmonic, chordal, s * 2.0f) : lerp(chordal, bell, (s - 0.5f) * 2.0f);
    const float pull = s <= 0.5f ? 1.0f : 1.0f - (s - 0.5f) * 2.0f;
    if (harmony != nullptr && params.gravity > 0.0f && pull > 0.0f)
        note = harmony->quantize(note, modes[static_cast<size_t>(i)].seed, params.gravity * pull);
    return note;
}

void ResonatorBank::updateModes(float dt) noexcept
{
    const float fs = static_cast<float>(spec.sampleRate);
    const float nyquistNote = 12.0f * std::log2(fs * 0.45f / 440.0f) + 69.0f;
    const float glide = snapNextUpdate ? 1.0f : std::min(1.0f, dt / 1.5f);
    snapNextUpdate = false;
    const float detune = 0.03f * tuningDrift.advance(dt);
    const float baseDecay = std::clamp(params.decaySeconds, 0.05f, 60.0f);

    const int active = std::min(params.modes, modeLimit);
    ringingModes = std::max(ringingModes, active);
    const float dyingR = std::exp(-6.9078f / (0.08f * fs));
    for (int i = 0; i < kMaxModes; ++i)
    {
        auto& m = modes[static_cast<size_t>(i)];
        m.targetNote = modeTargetNote(i);
        m.note += (m.targetNote - m.note) * glide;
        if (i >= active)
        {
            if (i < ringingModes)
            {
                const float w = kTwoPi * midiToHz(std::min(m.note, nyquistNote)) / fs;
                m.b1 = 2.0f * dyingR * std::cos(w);
                m.b2 = dyingR * dyingR;
                m.inGain = 0.0f;
            }
            continue;
        }

        const float note = std::min(m.note + detune, nyquistNote);
        const float w = kTwoPi * midiToHz(note) / fs;
        const float ratio = midiToHz(note) / midiToHz(params.rootNote);
        const float t60 = baseDecay / (1.0f + (1.0f - params.brightness) * std::max(0.0f, ratio - 1.0f) * 0.5f);
        const float r = std::exp(-6.9078f / (t60 * fs));
        m.b1 = 2.0f * r * std::cos(w);
        m.b2 = r * r;
        constexpr float kExcite = 2.4f;
        m.inGain = std::sin(w) * kExcite * m.amp / std::sqrt(static_cast<float>(active));
    }
}

void ResonatorBank::process(const float* excite, float* left, float* right, int n, float timeScale) noexcept
{
    std::fill_n(left, n, 0.0f);
    std::fill_n(right, n, 0.0f);

    const float rainRate = 8.0f * std::clamp(params.rain, 0.0f, 1.0f) * std::max(timeScale, 0.0f);
    const int strikeWidth = std::max(4, static_cast<int>(lerp(0.003f, 0.0003f, std::clamp(params.rainColour, 0.0f, 1.0f))
                                                         * static_cast<float>(spec.sampleRate)));
    constexpr float kDuckThreshold = 0.3f;

    int i = 0;
    while (i < n)
    {
        if (samplesUntilControl <= 0)
        {
            updateModes(static_cast<float>(kControlInterval / spec.sampleRate) * timeScale);
            samplesUntilControl = kControlInterval;
        }
        const int ringing = ringingModes;
        const int chunk = std::min(n - i, samplesUntilControl);

        for (int s = 0; s < chunk; ++s)
        {
            if (rainRate > 0.0f)
            {
                samplesToBurst -= 1.0;
                if (samplesToBurst <= 0.0)
                {
                    strikeAmp = 0.25f + 0.75f * rng.nextFloat();
                    strikeLength = strikeWidth;
                    strikePos = 0;
                    const double u = std::max(1.0e-6, static_cast<double>(rng.nextFloat()));
                    samplesToBurst = -std::log(u) * spec.sampleRate / rainRate;
                }
            }
            if (pendingStrike > 0.0f)
            {
                strikeAmp = std::min(1.0f, pendingStrike);
                strikeLength = strikeWidth;
                strikePos = 0;
                pendingStrike = 0.0f;
            }
            float strike = 0.0f;
            if (strikePos < strikeLength)
            {
                const float phase = static_cast<float>(strikePos) / static_cast<float>(strikeLength);
                strike = strikeAmp * (2.0f / static_cast<float>(strikeLength)) * (0.5f - 0.5f * std::cos(kTwoPi * phase));
                ++strikePos;
            }

            const float ratio = outputLevel / kDuckThreshold;
            const float duck = 1.0f / (1.0f + ratio * ratio);
            const float x = (strike + (excite != nullptr ? excite[i + s] * 0.05f : 0.0f)) * duck;
            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < ringing; ++k)
            {
                auto& m = modes[static_cast<size_t>(k)];
                const float y = m.inGain * (x - m.x2) + m.b1 * m.y1 - m.b2 * m.y2;
                m.x2 = m.x1;
                m.x1 = x;
                m.y2 = m.y1;
                m.y1 = flushDenormal(y);
                outL += y * m.gainL;
                outR += y * m.gainR;
            }
            left[i + s] = outL;
            right[i + s] = outR;
            const float level = 0.5f * (std::fabs(outL) + std::fabs(outR));
            outputLevel = flushDenormal(outputLevel + (level > outputLevel ? followAttack : followRelease) * (level - outputLevel));
        }

        for (int k = 0; k < ringing; ++k)
        {
            auto& m = modes[static_cast<size_t>(k)];
            const float mag = std::fabs(m.y1) + std::fabs(m.y2);
            if (mag < 1.0e-10f && std::fabs(m.x1) + std::fabs(m.x2) < 1.0e-10f)
                m.y1 = m.y2 = 0.0f;
            m.level = std::max(mag, m.level * 0.95f);
        }

        const int active = std::min(params.modes, modeLimit);
        while (ringingModes > active)
        {
            const auto& m = modes[static_cast<size_t>(ringingModes - 1)];
            if (std::fabs(m.y1) + std::fabs(m.y2) > 1.0e-6f)
                break;
            --ringingModes;
        }

        i += chunk;
        samplesUntilControl -= chunk;
    }

    for (int k = ringingModes; k < kMaxModes; ++k)
    {
        auto& m = modes[static_cast<size_t>(k)];
        m.y1 = m.y2 = m.x1 = m.x2 = m.level = 0.0f;
    }

    const float spread = std::clamp(params.spread, 0.0f, 1.0f);
    if (spread < 1.0f)
        for (int s = 0; s < n; ++s)
        {
            const float mid = 0.5f * (left[s] + right[s]);
            left[s] = lerp(mid, left[s], spread);
            right[s] = lerp(mid, right[s], spread);
        }
}

float ResonatorBank::getModeLevel(int mode) const noexcept
{
    return mode >= 0 && mode < kMaxModes ? modes[static_cast<size_t>(mode)].level : 0.0f;
}

float ResonatorBank::getModeNote(int mode) const noexcept
{
    return mode >= 0 && mode < kMaxModes ? modes[static_cast<size_t>(mode)].note : 0.0f;
}
}
