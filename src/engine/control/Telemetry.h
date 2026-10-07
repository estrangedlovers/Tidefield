#pragma once

#include <array>
#include <cstdint>

namespace tf::engine {

enum class FadeState : std::uint8_t { Silent, FadingIn, Open, FadingOut };

/** Engine state snapshot sent to the UI roughly 60 times a second. */
struct TelemetryFrame
{
    std::uint64_t sampleTime = 0;
    float peakL = 0.0f, peakR = 0.0f;   // linear, since previous frame
    float rmsL = 0.0f, rmsR = 0.0f;
    float limiterGain = 1.0f;
    float fadeGain = 0.0f;
    FadeState fadeState = FadeState::Silent;
    bool panicActive = false;
    std::uint32_t guardTrips = 0;
    std::array<float, 6> droneVoiceLevel {};
    std::array<float, 6> droneVoiceInterval {};
};

/** Discrete things the UI should hear about once. */
struct EngineNotice
{
    enum class Type : std::uint8_t { FadeInComplete, FadeOutComplete, PanicSilent, GuardTripped, ControlQueueOverflow };
    Type type = Type::FadeInComplete;
    std::uint64_t sampleTime = 0;
};

} // namespace tf::engine
