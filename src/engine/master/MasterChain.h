#pragma once

#include "../control/Telemetry.h"

#include <dsp/core/ProcessSpec.h>
#include <dsp/filters/DcBlocker.h>
#include <dsp/fx/Limiter.h>

#include <cstdint>

namespace tf::engine {

/** Master safety chain:
    non-finite guard -> master level -> master fade -> DC blocker -> limiter -> panic gain.

    The guard sits first so a NaN never reaches the limiter's state. Panic sits last so
    it acts within one sample of the limiter output, without waiting on the lookahead. */
class MasterChain
{
public:
    /** Things the engine needs to react to after a block. */
    struct Events
    {
        bool guardTripped = false;
        bool panicReachedSilence = false;
        bool fadeInCompleted = false;
        bool fadeOutCompleted = false;
    };

    static constexpr float kPanicSeconds = 0.05f;
    static constexpr float kGuardRecoverySeconds = 1.0f;

    void prepare(const dsp::ProcessSpec& spec);
    void reset() noexcept;

    void setCeilingDb(float db) noexcept { limiter.setCeilingDb(db); }
    void setFadeSeconds(float seconds) noexcept;

    void fadeIn() noexcept;
    void fadeOut() noexcept;
    void panic() noexcept;
    void resumeFromPanic() noexcept;

    /** In place. levelStart/levelEnd are linear gains to ramp across the block. */
    Events process(float* left, float* right, int numSamples, float levelStart, float levelEnd) noexcept;

    FadeState getFadeState() const noexcept { return fadeState; }
    float getFadeGain() const noexcept { return fadeCurve(fadePosition); }
    bool isPanicActive() const noexcept { return panicActive; }
    float getLimiterGain() const noexcept { return limiter.getCurrentGain(); }
    int getLatencySamples() const noexcept { return limiter.getLatencySamples(); }
    std::uint32_t getGuardTrips() const noexcept { return guardTrips; }

    /** Cubic curve: close to linear in dB over the range that matters, so long fades
        feel even instead of jumping at the start. */
    static float fadeCurve(float position) noexcept { return position * position * position; }

private:
    void startFade(FadeState direction, float seconds) noexcept;

    double fs = 48000.0;
    dsp::DcBlocker dcL, dcR;
    dsp::Limiter limiter;

    float fadeSeconds = 8.0f;
    float fadePosition = 0.0f;     // 0 = silent, 1 = open
    float fadeIncrement = 0.0f;    // per sample, signed
    FadeState fadeState = FadeState::Silent;

    bool panicActive = false;
    bool panicSilentReported = false;
    float panicGain = 1.0f;
    float panicStep = 0.0f;

    std::uint32_t guardTrips = 0;
};

} // namespace tf::engine
