#pragma once

#include "../mix/Layout.h"
#include "../params/ParamDefs.h"
#include "../scene/SceneSet.h"

#include <dsp/sources/bloom/BloomSampler.h>
#include <dsp/sources/granular/GranularCloud.h>
#include <dsp/sources/resonator/ResonatorBank.h>

#include <array>
#include <cstdint>

namespace tf::engine {

enum class FadeState : std::uint8_t { Silent, FadingIn, Open, FadingOut };

/** Engine state snapshot sent to the UI roughly 60 times a second. Trivially
    copyable; everything the visuals and controls need to follow the sound. */
struct TelemetryFrame
{
    std::uint64_t sampleTime = 0;
    float peakL = 0.0f, peakR = 0.0f;   // master, linear, since previous frame
    float rmsL = 0.0f, rmsR = 0.0f;
    float limiterGain = 1.0f;
    float fadeGain = 0.0f;
    FadeState fadeState = FadeState::Silent;
    bool panicActive = false;
    std::uint32_t guardTrips = 0;

    // CPU guardrails: smoothed DSP load (fraction of the real-time budget, 0 when not
    // measured) and the degradation level (0 = full quality).
    float dspLoad = 0.0f;
    int guardLevel = 0;

    // Global state.
    float tide = 1.0f;
    int harmonyRoot = 2;
    int harmonyScale = 1;
    float harmonyMorph = 1.0f;   // 1 = settled, < 1 while a key change migrates
    int mediumType = 0;

    // Mixer meters (post-fader peaks).
    std::array<float, kNumStrips> stripPeakL {}, stripPeakR {};

    // Drone.
    std::array<float, 6> droneVoiceLevel {};
    std::array<float, 6> droneVoiceInterval {};
    std::array<float, 6> droneVoiceNote {};

    // Clouds.
    std::array<bool, kNumClouds> cloudLoaded {};
    std::array<int, kNumClouds> cloudGrainCount {};
    std::array<int, kNumClouds> cloudGrainViews {};
    std::array<std::array<dsp::GranularCloud::GrainView, dsp::GranularCloud::kTelemetryGrains>, kNumClouds> cloudGrains {};

    // Resonator.
    std::array<float, dsp::ResonatorBank::kMaxModes> modeLevel {};
    std::array<float, dsp::ResonatorBank::kMaxModes> modeNote {};

    // Bloom.
    bool bloomLoaded = false;
    std::array<dsp::BloomSampler::VoiceView, dsp::BloomSampler::kMaxVoices> bloomVoices {};

    // Live input.
    float inputLevel = 0.0f;
    bool inputGateOpen = false;
    float inputFreeze = 0.0f;  // spectral pad gain 0..1

    // Looper, weather, freeze all.
    int loopState = 0;         // dsp::Disintegrator::State
    float loopPosition = 0.0f; // 0..1
    float loopSeconds = 0.0f;
    int loopPasses = 0;
    float weatherGust = 0.0f, weatherWave = 0.0f;
    float freezeGain = 0.0f;

    // Auto master: loudness (LUFS), make-up dB, low/mud/high EQ dB, width, glue dB, mix.
    std::array<float, 8> autoMaster {};

    // Performance layer.
    float swell = 0.0f;                         // envelope 0..1
    float hush = 0.0f, slow = 0.0f;             // envelopes 0..1
    std::array<float, 8> seasonValue {};        // each season's curve, -1..1
    std::array<float, 8> loopPhase {};          // incommensurate loops, 0..1
    std::array<float, 8> loopNote {};
    std::array<float, 8> loopFlash {};          // 1 when a loop just fired, decaying

    // Terrain.
    Point2 cursor {};          // performer's cursor after glide
    Point2 position {};        // effective position after wander
    int numScenes = 0;
    std::uint64_t sceneSetVersion = 0;
    std::array<float, kMaxScenes> sceneWeights {};

    // Every parameter's current target, so controls can follow the terrain, and
    // which parameters are held in the live layer.
    std::array<float, kNumParams> paramTargets {};
    std::array<std::uint8_t, kNumParams> live {};

    /** Soft takeover: for parameters with a MIDI binding that has not caught up yet,
        +1 if the controller is above the value, -1 if below; 0 otherwise. */
    std::array<std::int8_t, kNumParams> midiPickup {};
    bool sustainPedal = false;
};

/** Discrete things the UI should hear about once. */
struct EngineNotice
{
    enum class Type : std::uint8_t { FadeInComplete, FadeOutComplete, PanicSilent, GuardTripped, ControlQueueOverflow, CatchReady,
                                     CaptureSceneRequest, RecordToggleRequest };
    Type type = Type::FadeInComplete;
    std::uint64_t sampleTime = 0;

    // CatchReady: absolute ring frame where the region starts, its length in frames,
    // the source (0 = master output, 1 = live input) and the requested cloud
    // (0 = auto, 1-4), all as they were when the command ran.
    std::uint64_t start = 0;
    std::uint32_t length = 0;
    std::uint8_t source = 0;
    std::uint8_t target = 0;
};

} // namespace tf::engine
