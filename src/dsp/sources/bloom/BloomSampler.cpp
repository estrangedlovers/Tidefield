#include "BloomSampler.h"

#include "../../core/Denormal.h"
#include "../../core/Interpolation.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {
namespace {
constexpr float kMinEnv = 1.0e-4f;
constexpr std::array<int, 7> kConstellationDegrees { -3, 2, 4, 5, 7, 9, 11 };
}

const char* BloomSampler::transformName(Transform t) noexcept
{
    switch (t)
    {
        case Transform::Swell: return "Swell";
        case Transform::Smear: return "Smear";
        case Transform::Freeze: return "Freeze";
        case Transform::Ghost: return "Ghost";
        case Transform::Constellation: return "Constellation";
        case Transform::Tape: return "Tape";
    }
    return "";
}

void BloomSampler::prepare(const ProcessSpec& newSpec, std::uint64_t seed)
{
    spec = newSpec;
    rng.setSeed(seed);
    hann.resize(kWindowSize + 1);
    for (int i = 0; i <= kWindowSize; ++i)
        hann[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / kWindowSize);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];
        v.rng.setSeed(seed * 7u + static_cast<std::uint64_t>(i) + 1u);
        v.lpL.prepare(spec.sampleRate);
        v.lpR.prepare(spec.sampleRate);
    }
    reset();
}

void BloomSampler::reset() noexcept
{
    for (auto& v : voices)
    {
        v.active = false;
        v.lpL.reset();
        v.lpR.reset();
        v.level = 0.0f;
    }
}

void BloomSampler::setBuffer(const SampleBuffer* b) noexcept
{
    if (b == buffer)
        return;
    reset();
    buffer = b;
}

float BloomSampler::window(float phase) const noexcept
{
    const float pos = std::clamp(phase, 0.0f, 1.0f) * kWindowSize;
    const auto i = std::min(static_cast<int>(pos), kWindowSize - 1);
    const float t = pos - static_cast<float>(i);
    return hann[static_cast<size_t>(i)] + t * (hann[static_cast<size_t>(i) + 1] - hann[static_cast<size_t>(i)]);
}

float BloomSampler::quantizedNote(float note, float seed) const noexcept
{
    if (harmony == nullptr || params.gravity <= 0.0f)
        return note;
    return harmony->quantize(note, seed, params.gravity);
}

float BloomSampler::toneCutoff(float tone) noexcept { return 400.0f * std::pow(45.0f, std::clamp(tone, 0.0f, 1.0f)); }

void BloomSampler::applyTone(Voice& v, float cut) noexcept
{
    const float c = std::min(cut * v.toneScale, v.toneCap);
    if (c == v.appliedCut)
        return;
    v.lpL.setCutoff(c);
    v.lpR.setCutoff(c);
    v.appliedCut = c;
}

void BloomSampler::noteOn(int note, float velocity) noexcept
{
    if (buffer == nullptr || buffer->size() < 256 || velocity <= 0.0f)
        return;

    const auto pool = static_cast<std::size_t>(voiceLimit);
    Voice* target = nullptr;
    for (std::size_t i = 0; i < pool; ++i)
        if (! voices[i].active)
        {
            target = &voices[i];
            break;
        }
    if (target == nullptr)
    {
        float best = 1.0e9f;
        for (std::size_t i = 0; i < pool; ++i)
        {
            auto& v = voices[i];
            const float score = v.env + (v.stage == Stage::Release ? 0.0f : 1.0f);
            if (score < best)
            {
                best = score;
                target = &v;
            }
        }
    }
    startVoice(*target, note, velocity);
}

void BloomSampler::noteOff(int note) noexcept
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.held)
        {
            if (sustainPedal)
                v.sustained = true;
            else
                v.held = false;
        }
}

void BloomSampler::setSustain(bool down) noexcept
{
    sustainPedal = down;
    if (down)
        return;
    for (auto& v : voices)
        if (v.sustained)
        {
            v.sustained = false;
            v.held = false;
        }
}

void BloomSampler::releaseAll(float seconds) noexcept
{
    const float coeff = std::exp(-1.0f / (std::max(0.001f, seconds) * static_cast<float>(spec.sampleRate)));
    for (auto& v : voices)
        if (v.active)
        {
            v.held = false;
            v.stage = Stage::Release;
            v.releaseCoeff = coeff;
        }
}

void BloomSampler::setVoiceLimit(int limit) noexcept
{
    voiceLimit = std::clamp(limit, 1, kMaxVoices);
    const float coeff = std::exp(-1.0f / (0.5f * static_cast<float>(spec.sampleRate)));
    for (auto i = static_cast<std::size_t>(voiceLimit); i < voices.size(); ++i)
    {
        auto& v = voices[i];
        if (v.active && v.stage != Stage::Release)
        {
            v.held = v.sustained = false;
            v.stage = Stage::Release;
            v.releaseCoeff = coeff;
        }
    }
}

void BloomSampler::startVoice(Voice& v, int note, float velocity) noexcept
{
    const auto& p = params;
    const float fs = static_cast<float>(spec.sampleRate);
    const double size = static_cast<double>(buffer->size());
    const float rnd = std::clamp(p.random, 0.0f, 1.0f);
    const float amount = std::clamp(p.amount, 0.0f, 1.0f);

    v = Voice { .rng = v.rng, .lpL = v.lpL, .lpR = v.lpR };
    v.lpL.reset();
    v.lpR.reset();
    v.active = true;
    v.held = true;
    v.note = note;
    v.transform = p.transform;
    v.velocityGain = std::pow(std::clamp(velocity, 0.0f, 1.0f), 1.5f);

    const float seed = v.rng.nextFloat();
    v.playedNote = quantizedNote(static_cast<float>(note) + p.pitch, seed) + rnd * 0.08f * v.rng.nextBipolar();
    v.ratio = std::exp2((static_cast<double>(v.playedNote) - static_cast<double>(p.rootNote)) / 12.0) * buffer->sampleRate / spec.sampleRate;
    v.pan = std::clamp(p.spread * v.rng.nextBipolar(), -1.0f, 1.0f);

    const float attack = std::max(0.002f, p.attackSeconds * (1.0f + rnd * 0.5f * v.rng.nextBipolar()));
    v.attackStep = 1.0f / (attack * fs);
    v.releaseCoeff = std::exp(-6.9f / (std::max(0.05f, p.releaseSeconds) * fs));
    v.minLength = static_cast<int>(std::max(0.2f, p.lengthSeconds * (1.0f + rnd * 0.3f * v.rng.nextBipolar())) * fs);
    v.stage = Stage::Attack;

    const auto pans = equalPowerPan(v.pan);
    const float cut = toneCutoff(p.tone);

    auto grains = [&](double centre, double centreInc, float grainSeconds, float perSecond, float jitter, float pitchJitter) {
        v.grainsOn = true;
        v.centre = centre;
        v.centreInc = centreInc;
        v.grainLength = static_cast<double>(grainSeconds * fs);
        v.grainInterval = static_cast<double>(fs / perSecond);
        v.nextGrain = 0.0;
        v.grainJitter = jitter;
        v.pitchJitter = pitchJitter;
    };

    switch (p.transform)
    {
        case Transform::Swell:
        {
            const double longest = std::min(size - 2.0, 4.0 * static_cast<double>(fs) * v.ratio);
            const double shortest = std::min(0.6 * static_cast<double>(fs) * v.ratio, 0.5 * longest);
            const double swellSamples = shortest + (longest - shortest) * static_cast<double>(amount);
            auto& t = v.taps[0];
            t.active = true;
            t.mip = -1;
            t.pos = swellSamples;
            t.inc = -v.ratio;
            t.gainL = pans.left;
            t.gainR = pans.right;
            v.attackStep = 1.0f / static_cast<float>(swellSamples / v.ratio);
            grains(0.01 * size, 0.12 * size / (static_cast<double>(p.lengthSeconds) * spec.sampleRate), 0.22f, 18.0f, 0.02f, 0.03f);
            v.grainsStartAt = static_cast<int>(swellSamples / v.ratio * 0.85);
            break;
        }
        case Transform::Smear:
        {
            const double stretch = static_cast<double>(lerp(10.0f, 100.0f, amount * amount));
            grains(0.0, v.ratio / stretch, 0.18f, 26.0f, 0.04f, 0.04f);
            break;
        }
        case Transform::Freeze:
        {
            const double at = std::clamp(static_cast<double>(p.position + rnd * 0.05f * v.rng.nextBipolar()), 0.0, 0.95) * size;
            grains(at, 0.0, 0.09f, 45.0f, 0.004f + 0.02f * amount, 0.03f + 0.12f * amount);
            break;
        }
        case Transform::Ghost:
        {
            const double start = std::clamp(static_cast<double>(std::max(p.position, 0.12f)), 0.0, 0.9) * size;
            const double stretch = static_cast<double>(lerp(2.0f, 8.0f, amount));
            grains(start, v.ratio / stretch, 0.3f, 14.0f, 0.03f, 0.05f);
            v.attackStep = std::min(v.attackStep, 1.0f / (1.5f * fs));
            v.toneScale = 0.4f;
            break;
        }
        case Transform::Constellation:
        {
            const int count = std::clamp(2 + static_cast<int>(std::lround(amount * 4.0f)), 1, kMaxTaps);
            for (int i = 0; i < count; ++i)
            {
                auto& t = v.taps[static_cast<size_t>(i)];
                float tapNote = v.playedNote;
                if (i > 0)
                {
                    const int degree = kConstellationDegrees[static_cast<size_t>(v.rng.nextInt(static_cast<int>(kConstellationDegrees.size())))];
                    tapNote = harmony != nullptr ? harmony->scaleFor(seed).nearest(v.playedNote + static_cast<float>(degree))
                                                 : v.playedNote + static_cast<float>(degree);
                }
                t.active = true;
                t.mip = -1;
                t.pos = 0.0;
                t.inc = std::exp2((static_cast<double>(tapNote) - static_cast<double>(p.rootNote)) / 12.0) * buffer->sampleRate / spec.sampleRate;
                t.delay = i == 0 ? 0 : static_cast<int>(v.rng.nextFloat() * (0.4f + 2.6f * amount) * fs);
                const float g = i == 0 ? 1.0f : 0.45f + 0.4f * v.rng.nextFloat();
                const auto tp = equalPowerPan(std::clamp(p.spread * v.rng.nextBipolar(), -1.0f, 1.0f));
                t.gainL = tp.left * g;
                t.gainR = tp.right * g;
            }
            break;
        }
        case Transform::Tape:
        {
            auto& t = v.taps[0];
            t.active = true;
            t.mip = -1;
            t.pos = 0.0;
            t.inc = v.ratio;
            t.gainL = pans.left;
            t.gainR = pans.right;
            v.wowPhase = v.rng.nextFloat();
            v.toneCap = 9000.0f - 4000.0f * amount;
            break;
        }
    }
    applyTone(v, cut);
}

void BloomSampler::spawnGrain(Voice& v) noexcept
{
    Grain* g = nullptr;
    for (auto& candidate : v.grains)
        if (! candidate.active)
        {
            g = &candidate;
            break;
        }
    if (g == nullptr)
        return;

    const double size = static_cast<double>(buffer->size());
    const double span = v.grainLength * v.ratio;
    double pos = v.centre + static_cast<double>(v.grainJitter * v.rng.nextBipolar()) * size;
    pos = std::clamp(pos, 1.0, std::max(1.0, size - span - 4.0));

    g->active = true;
    g->pos = pos;
    g->inc = v.ratio * std::exp2(static_cast<double>(v.pitchJitter * v.rng.nextBipolar()) / 12.0);
    g->mip = mipLevelFor(std::fabs(g->inc), buffer->mipLevels());
    g->length = std::max(32, static_cast<int>(v.grainLength * (0.8 + 0.4 * static_cast<double>(v.rng.nextFloat()))));
    g->age = 0;
    const auto pans = equalPowerPan(std::clamp(v.pan + 0.3f * params.spread * v.rng.nextBipolar(), -1.0f, 1.0f));
    g->gainL = pans.left;
    g->gainR = pans.right;
}

void BloomSampler::renderVoice(Voice& v, float* left, float* right, int n, float timeScale) noexcept
{
    const bool stereo = buffer->isStereo();
    const double sizeD = static_cast<double>(buffer->size());
    auto readAt = [&](int mip, double pos, float& sl, float& sr) {
        const double p = pos / static_cast<double>(1 << mip);
        const auto sz = buffer->mipSize(mip);
        sl = readHermite(buffer->mipChannel(0, mip), sz, p);
        sr = stereo ? readHermite(buffer->mipChannel(1, mip), sz, p) : sl;
    };
    const float overlap = v.grainsOn ? std::max(1.0f, static_cast<float>(v.grainLength / std::max(1.0, v.grainInterval))) : 1.0f;
    const float grainNorm = 0.8f / std::sqrt(overlap);
    const float amount = std::clamp(params.amount, 0.0f, 1.0f);
    const float dt = 1.0f / static_cast<float>(spec.sampleRate);
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        switch (v.stage)
        {
            case Stage::Attack:
                v.env += v.attackStep;
                if (v.env >= 1.0f)
                {
                    v.env = 1.0f;
                    v.stage = Stage::Hold;
                }
                break;
            case Stage::Hold:
                if (! v.held && v.age >= v.minLength)
                    v.stage = Stage::Release;
                break;
            case Stage::Release:
                v.env = flushDenormal(v.env * v.releaseCoeff);
                break;
        }

        float l = 0.0f, r = 0.0f;
        bool anyTap = false;

        for (auto& t : v.taps)
        {
            if (! t.active)
                continue;
            anyTap = true;
            if (t.delay > 0)
            {
                --t.delay;
                continue;
            }
            double inc = t.inc;
            if (v.transform == Transform::Tape)
            {
                v.wowPhase += 0.45f * timeScale * dt;
                v.wowPhase -= std::floor(v.wowPhase);
                v.flutterPhase += 8.5f * timeScale * dt;
                v.flutterPhase -= std::floor(v.flutterPhase);
                const float wobble = 0.012f * amount * fastSin01(v.wowPhase) + 0.002f * amount * fastSin01(v.flutterPhase);
                if (v.stage == Stage::Release)
                    v.tapeSpeed = std::max(0.0f, v.tapeSpeed - dt / 1.2f);
                inc *= static_cast<double>((1.0f + wobble) * v.tapeSpeed);
            }
            if (t.mip < 0)
                t.mip = mipLevelFor(std::fabs(t.inc), buffer->mipLevels());
            float sl, sr;
            readAt(t.mip, t.pos, sl, sr);
            l += sl * t.gainL;
            r += sr * t.gainR;
            t.pos += inc;
            if (inc < 0.0 ? t.pos < 1.0 : t.pos >= sizeD - 2.0)
                t.active = false;
        }

        if (v.grainsOn && v.age >= v.grainsStartAt)
        {
            v.nextGrain -= 1.0;
            if (v.nextGrain <= 0.0)
            {
                spawnGrain(v);
                v.nextGrain += v.grainInterval * (0.6 + 0.8 * static_cast<double>(v.rng.nextFloat()));
            }
            v.centre = std::clamp(v.centre + v.centreInc * static_cast<double>(timeScale), 0.0, sizeD - 4.0);
            for (auto& g : v.grains)
            {
                if (! g.active)
                    continue;
                const float w = window(static_cast<float>(g.age) / static_cast<float>(g.length)) * grainNorm;
                float sl, sr;
                readAt(g.mip, g.pos, sl, sr);
                l += sl * w * g.gainL;
                r += sr * w * g.gainR;
                g.pos += g.inc;
                if (++g.age >= g.length)
                    g.active = false;
            }
        }

        if (v.transform == Transform::Tape && v.tapeSpeed <= 0.0f)
            for (auto& t : v.taps)
                t.active = false;
        if (v.transform == Transform::Tape)
        {
            l = std::tanh(l * (1.0f + 2.0f * amount)) / (1.0f + amount);
            r = std::tanh(r * (1.0f + 2.0f * amount)) / (1.0f + amount);
        }

        const float g = v.env * v.velocityGain;
        l = v.lpL.processLow(l) * g;
        r = v.lpR.processLow(r) * g;
        left[i] += l;
        right[i] += r;
        peak = std::max(peak, std::fabs(l) + std::fabs(r));
        ++v.age;

        if (! v.grainsOn && ! anyTap && v.stage != Stage::Release)
            v.stage = Stage::Release;
    }

    v.level = std::max(peak, v.level * 0.9f);
    if (v.stage == Stage::Release && v.env < kMinEnv)
    {
        v.active = false;
        v.level = 0.0f;
    }
}

void BloomSampler::process(float* left, float* right, int n, float timeScale) noexcept
{
    std::fill_n(left, n, 0.0f);
    std::fill_n(right, n, 0.0f);
    if (buffer == nullptr || buffer->size() < 256)
        return;
    if (params.tone != toneFor)
    {
        toneFor = params.tone;
        toneCut = toneCutoff(params.tone);
    }
    for (auto& v : voices)
        if (v.active)
        {
            applyTone(v, toneCut);
            renderVoice(v, left, right, n, timeScale);
        }
}

int BloomSampler::getActiveVoices() const noexcept
{
    int count = 0;
    for (const auto& v : voices)
        count += v.active ? 1 : 0;
    return count;
}

BloomSampler::VoiceView BloomSampler::getVoice(int i) const noexcept
{
    if (i < 0 || i >= kMaxVoices)
        return {};
    const auto& v = voices[static_cast<size_t>(i)];
    return { v.active, v.playedNote, v.active ? v.env * v.velocityGain : 0.0f };
}
}
