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
    reset();
}

void MasterChain::reset() noexcept
{
    dcL.reset();
    dcR.reset();
    limiter.reset();
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

MasterChain::Events MasterChain::process(float* left, float* right, int n, float levelStart, float levelEnd) noexcept
{
    Events events;

    bool finite = true;
    for (int i = 0; i < n && finite; ++i)
        finite = std::isfinite(left[i]) && std::isfinite(right[i]);
    if (! finite)
    {
        std::fill(left, left + n, 0.0f);
        std::fill(right, right + n, 0.0f);
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
    }

    limiter.process(left, right, n);

    if (panicActive)
    {
        for (int i = 0; i < n; ++i)
        {
            panicGain = std::max(0.0f, panicGain - panicStep);
            left[i] *= panicGain;
            right[i] *= panicGain;
        }
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
