#include "MasterChain.h"

#include <algorithm>
#include <cmath>

namespace tf::engine {
void MasterChain::prepare(const dsp::ProcessSpec& spec)
{
    fs = spec.sampleRate;
    dcL.prepare(fs);
    dcR.prepare(fs);
    limiter.prepare(spec);
    panicStep = 1.0f / static_cast<float>(kPanicSeconds * fs);
    gainTrace.assign(static_cast<std::size_t>(std::max(1, spec.maxBlockSize)), 1.0f);
    panicTrace.assign(gainTrace.size(), 1.0f);
    for (auto& d : extraDelay)
        d.assign(static_cast<std::size_t>(std::max(1, limiter.getLatencySamples())), 0.0f);
    for (auto& dc : extraDc)
        dc.prepare(fs);
    reset();
}

void MasterChain::reset() noexcept
{
    dcL.reset();
    dcR.reset();
    limiter.reset();
    for (auto& d : extraDelay)
        std::fill(d.begin(), d.end(), 0.0f);
    for (auto& dc : extraDc)
        dc.reset();
    extraWrite = 0;
}

void MasterChain::setFadeSeconds(float seconds) noexcept
{
    fadeSeconds = std::max(0.01f, seconds);
}

void MasterChain::startFade(FadeState direction, float seconds) noexcept
{
    const double inc = 1.0 / (static_cast<double>(std::max(0.01f, seconds)) * fs);
    fadeState = direction;
    fadeIncrement = direction == FadeState::FadingIn ? inc : -inc;
}

void MasterChain::fadeIn() noexcept
{
    if (panicActive || fadeState == FadeState::Open)
        return;
    startFade(FadeState::FadingIn, fadeSeconds);
}

void MasterChain::fadeOut() noexcept
{
    if (fadeState == FadeState::Silent)
        return;
    startFade(FadeState::FadingOut, fadeSeconds);
}

void MasterChain::panic() noexcept
{
    panicActive = true;
    panicSilentReported = false;
}

void MasterChain::resumeFromPanic() noexcept
{
    if (! panicActive)
        return;
    panicActive = false;
    panicGain = 1.0f;
    fadePosition = 0.0;
    fadeState = FadeState::Silent;
    startFade(FadeState::FadingIn, fadeSeconds);
}

MasterChain::Events MasterChain::process(float* left, float* right, int n, float levelStart, float levelEnd, float* const* extra,
                                         int numExtra) noexcept
{
    Events events;
    numExtra = extra != nullptr ? std::clamp(numExtra, 0, kMaxExtra) : 0;
    if (numExtra > 0)
        n = std::min(n, static_cast<int>(gainTrace.size()));

    bool finite = true;
    for (int i = 0; i < n && finite; ++i)
        finite = std::isfinite(left[i]) && std::isfinite(right[i]);
    for (int c = 0; c < numExtra && finite; ++c)
        for (int i = 0; i < n && finite; ++i)
            finite = std::isfinite(extra[c][i]);
    if (! finite)
    {
        std::fill(left, left + n, 0.0f);
        std::fill(right, right + n, 0.0f);
        for (int c = 0; c < numExtra; ++c)
            std::fill(extra[c], extra[c] + n, 0.0f);
        reset();
        ++guardTrips;
        events.guardTripped = true;
        if (! panicActive && fadeState != FadeState::Silent && fadeState != FadeState::FadingOut)
        {
            fadePosition = 0.0;
            startFade(FadeState::FadingIn, kGuardRecoverySeconds);
        }
    }

    const float levelStep = n > 0 ? (levelEnd - levelStart) / static_cast<float>(n) : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (fadeState == FadeState::FadingIn || fadeState == FadeState::FadingOut)
        {
            fadePosition += fadeIncrement;
            if (fadePosition >= 1.0 - 1.0e-9)
            {
                fadePosition = 1.0;
                fadeState = FadeState::Open;
                events.fadeInCompleted = true;
            }
            else if (fadePosition <= 1.0e-9)
            {
                fadePosition = 0.0;
                fadeState = FadeState::Silent;
                events.fadeOutCompleted = true;
            }
        }
        const float g = (levelStart + levelStep * static_cast<float>(i + 1)) * fadeCurve(static_cast<float>(fadePosition));
        left[i] = dcL.process(left[i] * g);
        right[i] = dcR.process(right[i] * g);
        for (int c = 0; c < numExtra; ++c)
            extra[c][i] = extraDc[static_cast<std::size_t>(c)].process(extra[c][i] * g);
    }

    limiter.process(left, right, n, numExtra > 0 ? gainTrace.data() : nullptr);

    if (panicActive)
    {
        for (int i = 0; i < n; ++i)
        {
            panicGain = std::max(0.0f, panicGain - panicStep);
            left[i] *= panicGain;
            right[i] *= panicGain;
            panicTrace[static_cast<std::size_t>(i)] = panicGain;
        }
    }
    else if (numExtra > 0)
        std::fill_n(panicTrace.data(), n, 1.0f);

    if (numExtra > 0)
    {
        const int length = static_cast<int>(extraDelay[0].size());
        const float ceiling = limiter.getCeiling();
        int w = extraWrite;
        for (int i = 0; i < n; ++i)
        {
            const float gain = gainTrace[static_cast<std::size_t>(i)] * panicTrace[static_cast<std::size_t>(i)];
            for (int c = 0; c < numExtra; ++c)
            {
                auto& line = extraDelay[static_cast<std::size_t>(c)];
                const float delayed = line[static_cast<std::size_t>(w)];
                line[static_cast<std::size_t>(w)] = extra[c][i];
                extra[c][i] = std::clamp(delayed * gain, -ceiling, ceiling);
            }
            w = (w + 1) % length;
        }
        extraWrite = w;
    }

    if (panicActive)
    {
        if (panicGain <= 0.0f && ! panicSilentReported)
        {
            panicSilentReported = true;
            events.panicReachedSilence = true;
            fadePosition = 0.0;
            fadeState = FadeState::Silent;
            reset();
        }
    }

    return events;
}
}
