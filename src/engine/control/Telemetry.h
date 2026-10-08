#pragma once

#include "../mix/Layout.h"
#include "../mod/ModMatrix.h"
#include "../params/ParamDefs.h"
#include "../perform/Gesture.h"
#include "../scene/SceneSet.h"

#include <dsp/sources/bloom/BloomSampler.h>
#include <dsp/sources/granular/GranularCloud.h>
#include <dsp/sources/resonator/ResonatorBank.h>

#include <array>
#include <cstdint>

namespace tf::engine {
enum class FadeState : std::uint8_t { Silent, FadingIn, Open, FadingOut };

struct TelemetryFrame
{
    std::uint64_t sampleTime = 0;
    float peakL = 0.0f, peakR = 0.0f;
    float rmsL = 0.0f, rmsR = 0.0f;
    float limiterGain = 1.0f;
    float fadeGain = 0.0f;
    FadeState fadeState = FadeState::Silent;
    bool panicActive = false;
    std::uint32_t guardTrips = 0;

    float dspLoad = 0.0f;
    int guardLevel = 0;

    float tide = 1.0f;
    int harmonyRoot = 2;
    int harmonyScale = 1;
    float harmonyMorph = 1.0f;
    int mediumType = 0;

    std::array<float, kNumStrips> stripPeakL {}, stripPeakR {};

    std::array<float, 6> droneVoiceLevel {};
    std::array<float, 6> droneVoiceInterval {};
    std::array<float, 6> droneVoiceNote {};

    std::array<bool, kNumClouds> cloudLoaded {};
    std::array<int, kNumClouds> cloudGrainCount {};
    std::array<int, kNumClouds> cloudGrainViews {};
    std::array<std::array<dsp::GranularCloud::GrainView, dsp::GranularCloud::kTelemetryGrains>, kNumClouds> cloudGrains {};

    std::array<float, dsp::ResonatorBank::kMaxModes> modeLevel {};
    std::array<float, dsp::ResonatorBank::kMaxModes> modeNote {};

    bool bloomLoaded = false;
    std::array<dsp::BloomSampler::VoiceView, dsp::BloomSampler::kMaxVoices> bloomVoices {};

    float inputLevel = 0.0f;
    bool inputGateOpen = false;
    float inputFreeze = 0.0f;

    int loopState = 0;
    float loopPosition = 0.0f;
    float loopSeconds = 0.0f;
    int loopPasses = 0;
    float weatherGust = 0.0f, weatherWave = 0.0f;
    float freezeGain = 0.0f;

    float bpm = 90.0f;
    float beatPhase = 0.0f;
    bool syncOn = false;
    bool hostTempo = false;

    GestureState gestureState = GestureState::Idle;
    std::uint8_t performanceState = 0;
    std::uint8_t spaceMode = 0, spaceChannels = 0, outputChannels = 2;
    float spaceRotation = 0.0f;
    float performanceSeconds = 0.0f;
    float gestureSeconds = 0.0f, gestureLength = 0.0f;

    std::array<float, 8> autoMaster {};

    float swell = 0.0f;
    float hush = 0.0f, slow = 0.0f;
    std::array<float, 8> seasonValue {};
    std::array<float, 8> loopPhase {};
    std::array<float, 8> loopNote {};
    std::array<float, 8> loopFlash {};

    Point2 cursor {};
    Point2 position {};
    int numScenes = 0;
    std::uint64_t sceneSetVersion = 0;
    std::array<float, kMaxScenes> sceneWeights {};

    std::array<float, kNumParams> paramTargets {};
    std::array<float, kNumParams> paramMod {};
    std::array<float, kNumModSources> modValue {};
    std::array<std::uint8_t, kNumParams> live {};

    std::array<std::int8_t, kNumParams> midiPickup {};
    bool sustainPedal = false;
};

struct EngineNotice
{
    enum class Type : std::uint8_t { FadeInComplete, FadeOutComplete, PanicSilent, GuardTripped, ControlQueueOverflow, CatchReady,
                                     CaptureSceneRequest, RecordToggleRequest };
    Type type = Type::FadeInComplete;
    std::uint64_t sampleTime = 0;

    std::uint64_t start = 0;
    std::uint32_t length = 0;
    std::uint8_t source = 0;
    std::uint8_t target = 0;
};
}
