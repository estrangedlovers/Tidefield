#include "Disintegrator.h"

#include "../../core/Denormal.h"
#include "../../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

namespace {

constexpr float kMinLoopSeconds = 0.25f;
constexpr float kClearSeconds = 0.5f;
constexpr float kPassLoss = 0.035f;     // level lost per pass at full erosion
constexpr float kHissPerPass = 2.0e-4f; // noise settling in per pass at full erosion

} // namespace

void Disintegrator::prepare(const ProcessSpec& s, std::uint64_t seed)
{
    spec = s;
    rng.setSeed(seed);
    capacity = static_cast<int>(kMaxSeconds * spec.sampleRate);
    bufL.assign(static_cast<std::size_t>(capacity), 0.0f);
    bufR.assign(static_cast<std::size_t>(capacity), 0.0f);
    edge = std::max(1, static_cast<int>(kEdgeSeconds * spec.sampleRate));
    for (int ch = 0; ch < 2; ++ch)
    {
        lowPass[ch].prepare(spec.sampleRate);
        lowPass[ch].setCutoff(9000.0f);
        highPass[ch].prepare(spec.sampleRate);
        highPass[ch].setCutoff(70.0f);
        dc[ch].prepare(spec.sampleRate);
    }
    reset();
}

void Disintegrator::reset() noexcept
{
    state = State::Empty;
    length = pos = passes = 0;
    clearGain = 1.0f;
    flakeRemaining = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        lowPass[ch].reset();
        highPass[ch].reset();
        dc[ch].reset();
    }
}

void Disintegrator::record() noexcept
{
    switch (state)
    {
        case State::Empty:
        case State::Clearing:
            state = State::Recording;
            pos = 0;
            passes = 0;
            clearGain = 1.0f;
            break;
        case State::Recording: closeLoop(); break;
        case State::Playing: state = State::Overdubbing; break;
        case State::Overdubbing: state = State::Playing; break;
    }
}

void Disintegrator::clear() noexcept
{
    if (state == State::Recording)
    {
        state = State::Empty; // nothing audible yet: drop the take
        length = pos = 0;
    }
    else if (state == State::Playing || state == State::Overdubbing)
    {
        state = State::Clearing;
    }
}

void Disintegrator::closeLoop() noexcept
{
    if (pos < static_cast<int>(kMinLoopSeconds * spec.sampleRate))
    {
        state = State::Empty;
        length = pos = 0;
        return;
    }
    length = pos;
    // Short fades at both ends so the seam never clicks.
    const int e = std::min(edge, length / 4);
    for (int i = 0; i < e; ++i)
    {
        const float g = static_cast<float>(i) / static_cast<float>(e);
        const auto a = static_cast<std::size_t>(i);
        const auto b = static_cast<std::size_t>(length - 1 - i);
        bufL[a] *= g;
        bufR[a] *= g;
        bufL[b] *= g;
        bufR[b] *= g;
    }
    pos = 0;
    passes = 0;
    state = State::Playing;
}

float Disintegrator::erode(float x, int ch) noexcept
{
    const float e = params.erosion;
    if (e <= 0.0f)
        return x;
    float y = highPass[ch].processHigh(lowPass[ch].processLow(x));
    y = dc[ch].process(y);
    y = std::tanh(1.5f * y) * (1.0f / 1.5f) * (1.0f - kPassLoss);
    y += kHissPerPass * rng.nextBipolar();
    return flushDenormal(x + e * (y - x));
}

void Disintegrator::process(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    const float fs = static_cast<float>(spec.sampleRate);
    if (state == State::Empty)
    {
        std::fill_n(outL, n, 0.0f);
        std::fill_n(outR, n, 0.0f);
        return;
    }

    const float flakeChance = params.flakes * params.erosion * 0.8f / fs; // per sample
    const float clearStep = 1.0f / (kClearSeconds * fs);

    for (int i = 0; i < n; ++i)
    {
        const float xl = inL != nullptr ? inL[i] : 0.0f;
        const float xr = inR != nullptr ? inR[i] : (inL != nullptr ? inL[i] : 0.0f);

        if (state == State::Recording)
        {
            bufL[static_cast<std::size_t>(pos)] = xl;
            bufR[static_cast<std::size_t>(pos)] = xr;
            outL[i] = outR[i] = 0.0f;
            if (++pos >= capacity)
                closeLoop();
            continue;
        }
        if (state == State::Empty)
        {
            outL[i] = outR[i] = 0.0f;
            continue;
        }

        const auto p = static_cast<std::size_t>(pos);
        const float l = bufL[p];
        const float r = bufR[p];
        outL[i] = l * clearGain;
        outR[i] = r * clearGain;

        // Oxide flaking: a raised-cosine dent, written into the tape for good.
        float flake = 1.0f;
        if (flakeRemaining > 0)
        {
            const float phase = 1.0f - static_cast<float>(flakeRemaining) / static_cast<float>(flakeLength);
            flake = 1.0f - flakeDepth * (0.5f - 0.5f * std::cos(kTwoPi * phase));
            --flakeRemaining;
        }
        else if (flakeChance > 0.0f && rng.chance(flakeChance))
        {
            flakeLength = flakeRemaining = static_cast<int>(rng.nextRange(0.03f, 0.4f) * fs);
            flakeDepth = rng.nextRange(0.15f, 0.7f);
        }

        float wl = erode(l, 0) * flake;
        float wr = erode(r, 1) * flake;
        if (state == State::Overdubbing)
        {
            // Layers pile up pass after pass: a soft ceiling keeps the tape bounded.
            wl = 1.5f * std::tanh((wl + xl * params.overdub) * (1.0f / 1.5f));
            wr = 1.5f * std::tanh((wr + xr * params.overdub) * (1.0f / 1.5f));
        }
        bufL[p] = wl;
        bufR[p] = wr;

        if (++pos >= length)
        {
            pos = 0;
            ++passes;
        }

        if (state == State::Clearing)
        {
            clearGain -= clearStep;
            if (clearGain <= 0.0f)
            {
                state = State::Empty;
                length = pos = 0;
                clearGain = 1.0f;
            }
        }
    }
}

float Disintegrator::getPosition() const noexcept
{
    if (state == State::Recording)
        return capacity > 0 ? static_cast<float>(pos) / static_cast<float>(capacity) : 0.0f;
    return length > 0 ? static_cast<float>(pos) / static_cast<float>(length) : 0.0f;
}

float Disintegrator::getLengthSeconds() const noexcept
{
    const int n = state == State::Recording ? pos : length;
    return static_cast<float>(n / spec.sampleRate);
}

} // namespace tf::dsp
