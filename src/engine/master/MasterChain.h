#pragma once

#include "../control/Telemetry.h"

#include <dsp/core/ProcessSpec.h>
#include <dsp/filters/DcBlocker.h>
#include <dsp/fx/Limiter.h>

#include <array>
#include <cstdint>
#include <vector>

namespace tf::engine {
class MasterChain
{
public:
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

    Events process(float* left, float* right, int numSamples, float levelStart, float levelEnd, float* const* extra = nullptr,
                   int numExtra = 0) noexcept;

    FadeState getFadeState() const noexcept { return fadeState; }
    float getFadeGain() const noexcept { return fadeCurve(static_cast<float>(fadePosition)); }
    bool isPanicActive() const noexcept { return panicActive; }
    float getLimiterGain() const noexcept { return limiter.getCurrentGain(); }
    int getLatencySamples() const noexcept { return limiter.getLatencySamples(); }
    std::uint32_t getGuardTrips() const noexcept { return guardTrips; }

    static float fadeCurve(float position) noexcept { return position * position * position; }

private:
    void startFade(FadeState direction, float seconds) noexcept;

    double fs = 48000.0;
    dsp::DcBlocker dcL, dcR;
    dsp::Limiter limiter;

    float fadeSeconds = 8.0f;
    double fadePosition = 0.0;
    double fadeIncrement = 0.0;
    FadeState fadeState = FadeState::Silent;

    bool panicActive = false;
    bool panicSilentReported = false;
    float panicGain = 1.0f;
    float panicStep = 0.0f;

    std::uint32_t guardTrips = 0;

    static constexpr int kMaxExtra = 8;
    std::vector<float> gainTrace, panicTrace;
    std::array<std::vector<float>, kMaxExtra> extraDelay;
    std::array<dsp::DcBlocker, kMaxExtra> extraDc;
    int extraWrite = 0;
};
}
