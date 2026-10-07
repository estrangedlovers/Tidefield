#include "GranularCloud.h"

#include "../../core/Interpolation.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

void GranularCloud::prepare(const ProcessSpec& newSpec, std::uint64_t seed)
{
    spec = newSpec;
    rng.setSeed(seed);
    positionDrift.setSeed(seed + 17);
    positionDrift.setRate(0.03f);

    hann.resize(kWindowSize + 1);
    perc.resize(kWindowSize + 1);
    tukey.resize(kWindowSize + 1);
    for (int i = 0; i <= kWindowSize; ++i)
    {
        const float t = static_cast<float>(i) / kWindowSize;
        hann[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(kTwoPi * t);
        // Percussive: 5% raised-cosine attack, exponential-ish decay to zero.
        const float attack = 0.05f;
        perc[static_cast<size_t>(i)] = t < attack ? 0.5f - 0.5f * std::cos(kPi * t / attack)
                                                  : std::pow(1.0f - (t - attack) / (1.0f - attack), 2.5f);
        // Tukey (25% tapers), for smooth, sustained textures.
        const float taper = 0.25f;
        float w = 1.0f;
        if (t < taper * 0.5f)
            w = 0.5f - 0.5f * std::cos(kTwoPi * t / taper);
        else if (t > 1.0f - taper * 0.5f)
            w = 0.5f - 0.5f * std::cos(kTwoPi * (1.0f - t) / taper);
        tukey[static_cast<size_t>(i)] = w;
    }
    reset();
}

void GranularCloud::reset() noexcept
{
    for (auto& g : grains)
        g.active = false;
    activeCount = 0;
    samplesToNextGrain = 0.0;
}

void GranularCloud::setBuffer(const SampleBuffer* newBuffer) noexcept
{
    if (newBuffer == buffer)
        return;
    // Grains index into the old buffer, so they must stop. The engine crossfades the
    // strip when it swaps buffers, so the cut is not heard.
    reset();
    buffer = newBuffer;
}

float GranularCloud::windowAt(float phase, float mix) const noexcept
{
    const float pos = std::clamp(phase, 0.0f, 1.0f) * kWindowSize;
    const auto i = std::min(static_cast<int>(pos), kWindowSize - 1);
    const float t = pos - static_cast<float>(i);
    const auto ui = static_cast<size_t>(i);
    auto sample = [&](const std::vector<float>& w) { return w[ui] + t * (w[ui + 1] - w[ui]); };
    if (mix <= 0.5f)
        return lerp(sample(perc), sample(hann), mix * 2.0f);
    return lerp(sample(hann), sample(tukey), (mix - 0.5f) * 2.0f);
}

void GranularCloud::spawnGrain() noexcept
{
    if (buffer == nullptr || buffer->size() < 64 || activeCount >= grainLimit)
        return;

    Grain* g = nullptr;
    for (auto& candidate : grains)
        if (! candidate.active)
        {
            g = &candidate;
            break;
        }
    if (g == nullptr)
        return;

    const auto& p = params;
    const double bufferSize = static_cast<double>(buffer->size());
    const double rateRatio = buffer->sampleRate / spec.sampleRate;

    // Pitch: base + random detune, optionally a scale interval, optionally pulled to
    // the scale by gravity (relative to the root the sample is assumed to sit on).
    float semis = p.pitch + p.pitchSpread * rng.nextBipolar();
    if (harmony != nullptr && p.harmonize > 0.0f && rng.chance(p.harmonize))
    {
        const int degree = rng.nextInt(9) - 2; // a little below to well above
        const auto& scale = harmony->scaleFor(rng.nextFloat());
        const int rootPc = scale.root;
        const int base = static_cast<int>(std::lround(p.rootNote));
        const int rootNote = base - (((base - rootPc) % 12 + 12) % 12);
        semis += scale.degreeToNote(rootNote, degree) - static_cast<float>(base);
    }
    if (harmony != nullptr && p.gravity > 0.0f)
        semis = harmony->quantize(p.rootNote + semis, rng.nextFloat(), p.gravity) - p.rootNote;

    const double ratio = std::exp2(static_cast<double>(semis) / 12.0) * rateRatio;
    const int length = std::max(16, static_cast<int>(p.grainMs * 0.001f * static_cast<float>(spec.sampleRate)));
    const double span = ratio * length; // buffer samples the grain will cover

    float pos = p.position + scanPosition + p.spray * 0.5f * rng.nextBipolar() + 0.02f * positionDrift.getValue();
    pos -= std::floor(pos); // wrap
    double start = static_cast<double>(pos) * bufferSize;
    const bool reversed = rng.chance(p.reverse);
    if (! reversed)
        start = std::min(start, bufferSize - span - 4.0);
    else
        start = std::max(start, span + 4.0);
    if (start < 1.0 || start > bufferSize - 2.0)
        start = std::clamp(start, 1.0, bufferSize - 2.0);

    g->active = true;
    g->readPos = start;
    g->increment = reversed ? -ratio : ratio;
    g->mip = mipLevelFor(std::fabs(g->increment), buffer->mipLevels());
    g->length = length;
    g->age = 0;
    g->windowMix = std::clamp(p.shape, 0.0f, 1.0f);
    g->pan = std::clamp(p.stereo * rng.nextBipolar(), -1.0f, 1.0f);
    const auto gains = equalPowerPan(g->pan);
    g->gainL = gains.left;
    g->gainR = gains.right;
    ++activeCount;
}

void GranularCloud::process(float* left, float* right, int n, float timeScale) noexcept
{
    std::fill_n(left, n, 0.0f);
    std::fill_n(right, n, 0.0f);
    if (buffer == nullptr || buffer->size() < 64)
        return;

    const float dt = static_cast<float>(n / spec.sampleRate) * timeScale;
    positionDrift.advance(dt);
    scanPosition += params.scan * dt / 60.0f;
    scanPosition -= std::floor(scanPosition);

    // Overlap normalisation: expected simultaneous grains = density * length.
    const float overlap = std::max(1.0f, params.density * params.grainMs * 0.001f);
    const float norm = 1.0f / std::sqrt(overlap);

    const bool stereoSource = buffer->isStereo();
    const double meanInterval = spec.sampleRate / std::max(0.05, static_cast<double>(params.density));

    // Onsets are resolved per block (the engine calls with <= 32 samples, < 1 ms).
    samplesToNextGrain -= static_cast<double>(n);
    {
        while (samplesToNextGrain <= 0.0)
        {
            spawnGrain();
            // Half regular, half Poisson: even enough to feel continuous at high
            // density, irregular enough to never sound like a pulse.
            const double u = std::max(1.0e-6, static_cast<double>(rng.nextFloat()));
            samplesToNextGrain += meanInterval * (0.5 + 0.5 * -std::log(u));
        }
    }

    for (auto& g : grains)
    {
        if (! g.active)
            continue;
        // Read the band-limited level chosen for this grain's speed (positions stay in
        // level-0 samples; a level-k sample covers 2^k of them).
        const float* srcL = buffer->mipChannel(0, g.mip);
        const float* srcR = buffer->mipChannel(1, g.mip);
        const auto size = buffer->mipSize(g.mip);
        const bool stereo = stereoSource;
        const double scale = 1.0 / static_cast<double>(1 << g.mip);
        const float invLength = 1.0f / static_cast<float>(g.length);
        // The window changes slowly against a chunk (<= 32 samples; grains are >= 10 ms),
        // so it is evaluated at the chunk's ends and interpolated: two table blends per
        // chunk instead of one per sample (error below -50 dB).
        const int run = std::min(n, g.length - g.age);
        const float w0 = windowAt(static_cast<float>(g.age) * invLength, g.windowMix) * norm;
        const float w1 = windowAt(static_cast<float>(g.age + run) * invLength, g.windowMix) * norm;
        const float wStep = run > 0 ? (w1 - w0) / static_cast<float>(run) : 0.0f;
        float w = w0;
        const float gl = g.gainL, gr = g.gainR;
        double pos = g.readPos * scale;
        const double inc = g.increment * scale;
        for (int i = 0; i < run; ++i)
        {
            float sl, sr;
            const auto k = static_cast<std::size_t>(pos);
            if (pos >= 1.0 && k + 2 < size)
            {
                // Interior: one index for both channels, no bounds checks.
                const float t = static_cast<float>(pos - static_cast<double>(k));
                sl = hermite(srcL[k - 1], srcL[k], srcL[k + 1], srcL[k + 2], t);
                sr = stereo ? hermite(srcR[k - 1], srcR[k], srcR[k + 1], srcR[k + 2], t) : sl;
            }
            else
            {
                sl = readHermite(srcL, size, pos);
                sr = stereo ? readHermite(srcR, size, pos) : sl;
            }
            // Pan a mono read in full; for stereo sources the pan narrows to a balance.
            left[i] += sl * w * gl;
            right[i] += sr * w * gr;
            pos += inc;
            w += wStep;
        }
        g.readPos = pos / scale;
        g.age += run;
        if (g.age >= g.length)
        {
            g.active = false;
            --activeCount;
        }
    }
}

int GranularCloud::getGrainViews(GrainView* out) const noexcept
{
    if (buffer == nullptr || buffer->size() == 0)
        return 0;
    int count = 0;
    const double size = static_cast<double>(buffer->size());
    for (const auto& g : grains)
    {
        if (! g.active || count >= kTelemetryGrains)
            continue;
        const float phase = static_cast<float>(g.age) / static_cast<float>(g.length);
        out[count++] = { static_cast<float>(g.readPos / size), windowAt(phase, g.windowMix), g.pan };
    }
    return count;
}

} // namespace tf::dsp
