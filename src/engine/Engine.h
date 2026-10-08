#pragma once

#include "control/ControlEvent.h"
#include "control/SnapshotChannel.h"
#include "control/SpscQueue.h"
#include "control/Telemetry.h"
#include "guard/DegradationPolicy.h"
#include "master/MasterChain.h"
#include "mix/ChannelStrip.h"
#include "mix/FxSlot.h"
#include "midi/MidiTypes.h"
#include "mix/Layout.h"
#include "mod/ModMatrix.h"
#include "mod/Seasons.h"
#include "params/ParamRegistry.h"
#include "params/ParamState.h"
#include "record/RecordTap.h"
#include "SampleHandle.h"
#include "scene/SceneSet.h"
#include "perform/Gesture.h"
#include "scene/TerrainPath.h"
#include "scene/Wander.h"

#include <dsp/core/SampleBuffer.h>
#include <dsp/core/Smoother.h>
#include <dsp/fx/medium/Medium.h>
#include <dsp/master/AutoMaster.h>
#include <dsp/harmony/HarmonicGravity.h>
#include <dsp/mod/Drift.h>
#include <dsp/sources/bloom/BloomSampler.h>
#include <dsp/sources/drone/DroneGenerator.h>
#include <dsp/sources/granular/GranularCloud.h>
#include <dsp/sources/input/LiveInput.h>
#include <dsp/sources/looper/Disintegrator.h>
#include <dsp/sources/weather/WeatherBed.h>
#include <dsp/spectral/SpectralFreeze.h>
#include <dsp/sources/resonator/ResonatorBank.h>

#include <array>
#include <bitset>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace tf::engine {
class Engine
{
public:
    static constexpr int kControlInterval = 32;
    static constexpr int kTerrainDecimation = 4;
    static constexpr float kCloudSwapSeconds = 0.03f;

    struct Config
    {
        std::uint64_t seed = 1;
        std::size_t controlQueueSize = 4096;
        std::size_t telemetryQueueSize = 16;
        std::size_t noticeQueueSize = 64;
        double telemetryRateHz = 60.0;
    };

    Engine();
    explicit Engine(const Config& config);

    void prepare(double sampleRate, int maxBlockSize);
    void release();

    void process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept;

    bool post(const ControlEvent& event) noexcept;
    bool setParam(P p, float value) noexcept { return post(ControlEvent::setParam(idx(p), value)); }
    bool command(Command c) noexcept { return post(ControlEvent::makeCommand(c)); }

    bool publishScenes(std::unique_ptr<SceneSet> scenes) { return sceneChannel.publish(std::move(scenes)); }
    bool publishSeasons(std::unique_ptr<SeasonSet> set) { return seasonChannel.publish(std::move(set)); }
    bool publishPath(std::unique_ptr<TerrainPath> path) { return pathChannel.publish(std::move(path)); }
    bool publishModRoutes(std::unique_ptr<ModRouteSet> routes) { return modChannel.publish(std::move(routes)); }

    bool loadCloudSample(int cloud, std::shared_ptr<const dsp::SampleBuffer> buffer);

    std::shared_ptr<const dsp::SampleBuffer> getCloudSample(int cloud) const;

    bool loadBloomSample(std::shared_ptr<const dsp::SampleBuffer> buffer);
    bool previewSample(std::shared_ptr<const dsp::SampleBuffer> buffer);
    std::shared_ptr<const dsp::SampleBuffer> getBloomSample() const { return bloomMirror; }

    void setHostTransport(double tempoBpm, double ppqPosition, bool playing) noexcept
    {
        if (! std::isfinite(tempoBpm) || ! std::isfinite(ppqPosition))
            tempoBpm = ppqPosition = 0.0, playing = false;
        hostBpm = tempoBpm;
        hostPpq = ppqPosition;
        hostPlaying = playing && tempoBpm > 0.0;
        hostSampleTime = sampleTime;
    }

    bool noteOn(int note, float velocity) noexcept { return post(ControlEvent::note(note, velocity)); }
    bool noteOff(int note) noexcept { return post(ControlEvent::note(note, 0.0f)); }

    bool copyCatch(const EngineNotice& notice, dsp::SampleBuffer& out) const;

    bool postMidi(int port, const RawMidi& message) noexcept;
    bool publishMidiMap(std::unique_ptr<MidiMap> map) { return midiMapChannel.publish(std::move(map)); }
    bool popMidiMonitor(RawMidi& out) noexcept { return midiMonitor.pop(out); }

    bool popGesture(GestureEvent& out) noexcept { return gestureOut.pop(out); }
    bool publishGesture(std::unique_ptr<GestureTake> take) { return gestureChannel.publish(std::move(take)); }

    bool sendProcessor(int slot, dsp::ProcessorPtr processor);
    int collectProcessors(int slot);

    void collectGarbage();

    RecordTap& getRecordTap() noexcept { return recordTap; }

    void setGuardrailsEnabled(bool enabled) noexcept { guardEnabled.store(enabled, std::memory_order_relaxed); }
    void forceLoadForTesting(float load) noexcept { forcedLoad.store(load, std::memory_order_relaxed); }

    bool popTelemetry(TelemetryFrame& out) noexcept { return telemetryQueue.pop(out); }
    bool popNotice(EngineNotice& out) noexcept { return noticeQueue.pop(out); }

    const ParamRegistry& getRegistry() const noexcept { return registry; }
    int getLatencySamples() const noexcept { return master.getLatencySamples() + medium.getLatencySamples(); }
    double getSampleRate() const noexcept { return sampleRate; }
    dsp::ProcessSpec getProcessSpec() const noexcept { return { sampleRate, maxBlock }; }
    std::uint64_t getSampleTime() const noexcept { return sampleTime; }

private:
    struct CloudSlot
    {
        dsp::GranularCloud cloud;
        SnapshotChannel<SampleHandle> buffers { 4 };
        std::shared_ptr<const dsp::SampleBuffer> mirror;
        enum class Swap { Idle, FadingOut, FadingIn } swap = Swap::Idle;
        float swapGain = 1.0f;
    };

    void drainControl() noexcept;
    void applyEvent(const ControlEvent& e) noexcept;
    void applyCommand(Command c) noexcept;
    void controlTick() noexcept;
    void updateTerrain(float dtSeconds) noexcept;
    void updateSources(float tide) noexcept;
    void updateFx(float tide) noexcept;
    void updateModulation(float dtSeconds) noexcept;
    void updateTempo(float dtSeconds) noexcept;
    void updateLoops(float dtSeconds) noexcept;
    void updateCloudSwaps(int numSamples) noexcept;
    void resetFeedback() noexcept;
    void processChunk(const float* const* inputs, int numInputs, int inputOffset, int offset, int numSamples) noexcept;
    void notify(EngineNotice::Type type) noexcept;
    void handleMidi(const RawMidi& m) noexcept;
    void applyMidiBinding(std::size_t index, const MidiBinding& b, int value) noexcept;
    void fireMidiAction(MidiAction action) noexcept;
    void accumulateTelemetry(const float* l, const float* r, int n) noexcept;
    void updateGuardrails(double elapsedSeconds, int numSamples) noexcept;
    void applyGuardLimits(const GuardLimits& limits) noexcept;
    bool terrainActive() const noexcept;
    float mixRamp(P mixParam, int tickPos) const noexcept;
    std::array<float, 6> slotControls(int slot) const noexcept;

    Config config;
    ParamRegistry registry;
    ParamState params;
    std::vector<unsigned> paramFlags;

    SpscQueue<ControlEvent> controlQueue;
    SpscQueue<TelemetryFrame> telemetryQueue;
    SpscQueue<EngineNotice> noticeQueue;
    SnapshotChannel<SceneSet> sceneChannel;
    SnapshotChannel<SeasonSet> seasonChannel { 4 };
    SnapshotChannel<TerrainPath> pathChannel { 4 };
    SnapshotChannel<ModRouteSet> modChannel { 4 };
    std::array<float, kNumModSources> modValue {};
    std::array<float, kNumLfos> lfoPhase {}, lfoStep {};
    std::array<float, kNumRandoms> randomValue {}, randomTarget {}, randomClock {};
    dsp::Random modRng;
    double inputEnergy = 0.0, inputDiffEnergy = 0.0, mixEnergy = 0.0;
    int inputEnergyCount = 0, mixEnergyCount = 0;
    float inputPrev = 0.0f, inputFollow = 0.0f, brightFollow = 0.0f, mixFollow = 0.0f;
    float lastVelocity = 0.0f, lastNote = 60.0f, modWheel = 0.0f, pressure = 0.0f;
    void updateModSources(float dt) noexcept;
    void trackNote(int note, float velocity) noexcept;

    std::array<std::unique_ptr<SpscQueue<RawMidi>>, kMaxMidiPorts> midiQueues;
    SpscQueue<RawMidi> midiMonitor { 512 };
    SpscQueue<GestureEvent> gestureOut { 16384 };
    SnapshotChannel<GestureTake> gestureChannel { 4 };
    GestureState gestureState = GestureState::Idle;
    std::uint64_t gestureStart = 0;
    std::size_t gestureIndex = 0;
    std::uint64_t gesturePlayedVersion = 0;
    float pendingPlayVersion = 0.0f;
    std::uint16_t recordGeneration = 0;
    GestureEvent endMarker;
    bool endPending = false;
    std::bitset<128> recordHeld, playHeld;
    void recordGesture(const ControlEvent& e) noexcept;
    void stopGesture(bool onAudioThread) noexcept;
    void flushGestureEnd() noexcept;
    void releasePlayedNotes() noexcept;
    void updateGesture() noexcept;
    SnapshotChannel<MidiMap> midiMapChannel { 4 };
    struct Pickup
    {
        bool caught = false;
        bool hasLast = false;
        float lastController = 0.0f;
        float lastSent = -1.0f;
        bool buttonDown = false;
    };
    std::array<Pickup, kMaxMidiBindings> pickups {};
    std::vector<std::int8_t> midiPickup;
    bool sustainPedal = false;
    std::array<float, 16> mpeBend {}, mpePressure {};
    std::array<float, 16> mpeTimbre { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    std::int64_t clockTicks = 0;
    double lastClockTime = 0.0, clockInterval = 0.0;
    bool clockRunning = false, clockDriven = false;
    void handleClock(const RawMidi& m) noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    std::uint64_t sampleTime = 0;
    std::uint64_t tickCount = 0;
    float tide = 1.0f;

    dsp::OnePoleSmoother cursorX, cursorY;
    Wander wander;
    Point2 cursor {}, position {};
    std::array<float, kMaxScenes> weights {};
    std::vector<std::uint8_t> live;

    dsp::HarmonicGravity harmony;
    dsp::LiveInput liveInput;
    dsp::DroneGenerator drone;
    std::array<CloudSlot, kNumClouds> clouds;
    dsp::ResonatorBank resonator;
    dsp::BloomSampler bloom;
    SnapshotChannel<SampleHandle> bloomBuffers { 4 };
    std::shared_ptr<const dsp::SampleBuffer> bloomMirror;
    bool bloomSwapping = false;
    SnapshotChannel<SampleHandle> previewBuffers { 4 };
    const dsp::SampleBuffer* preview = nullptr;
    double previewPos = 0.0;
    void mixPreview(int numSamples) noexcept;
    dsp::Disintegrator looper;
    dsp::WeatherBed weather;
    dsp::SpectralFreeze inputFreeze;

    static constexpr double kFreezeRingSeconds = 3.0;
    static constexpr double kFreezeSeconds = 2.0;
    dsp::GranularCloud freezeCloud;
    dsp::SampleBuffer freezeBuffer;
    std::vector<float> preRingL, preRingR, freezeGainBuf;
    std::size_t preCapacity = 0;
    std::uint64_t preWritten = 0;
    bool freezeLoaded = false;
    float freezeGain = 0.0f;
    void captureFreeze() noexcept;
    void processFreeze(int offset, int numSamples) noexcept;

    float swellEnv = 0.0f, hushEnv = 0.0f, slowEnv = 0.0f;
    std::array<float, kMaxSeasons> seasonPhase {}, seasonValue {};
    std::array<dsp::Drift, kMaxSeasons> seasonDrift;
    std::uint64_t seasonVersion = 0;
    static constexpr int kMaxLoops = 8;
    std::array<float, kMaxLoops> loopPhase {}, loopNote {}, loopFlash {};
    std::array<float, kMaxLoops> loopOffset {};
    int loopPattern = -1;
    double hostBpm = 0.0, hostPpq = 0.0;
    bool hostPlaying = false;
    std::uint64_t hostSampleTime = 0;
    double beatPos = 0.0;
    float bpm = 90.0f;
    bool syncOn = false;
    std::array<std::int64_t, kMaxLoops> loopCycle {};
    std::array<double, kMaxLoops> loopSyncPeriod {};
    dsp::Random loopRng;

    std::array<ChannelStrip, kNumStrips> strips;
    std::array<FxSlot, kNumFxSlots> fxSlots;
    dsp::Medium medium;
    dsp::AutoMaster autoMaster;
    MasterChain master;

    std::array<std::vector<float>, kNumStrips> stripL, stripR;
    std::vector<float> busAL, busAR, busBL, busBR, masterL, masterR;
    std::vector<float> inputMono, excite, scratchDryL, scratchDryR, scratchAltL, scratchAltR, loopInL, loopInR, padL, padR;

    static constexpr double kCatchRingSeconds = 40.0;
    std::vector<float> catchL, catchR, catchIn;
    std::size_t catchCapacity = 0;
    std::atomic<std::uint64_t> catchWritten { 0 };
    std::atomic<std::uint64_t> inputWritten { 0 };
    void writeCatch(const float* l, const float* r, int n) noexcept;
    void requestCatch() noexcept;

    RecordTap recordTap;
    std::array<std::vector<float>, RecordTap::kStemPairs> stemL, stemR;
    int recordStride = 0;
    void pushRecording(int numSamples) noexcept;

    DegradationPolicy guard;
    std::atomic<bool> guardEnabled { false };
    std::atomic<float> forcedLoad { -1.0f };
    float droneVoiceCap = static_cast<float>(dsp::DroneGenerator::kMaxVoices);

    int telemetryInterval = 800;
    int telemetryCountdown = 0;
    float accPeakL = 0.0f, accPeakR = 0.0f;
    double accSumL = 0.0, accSumR = 0.0;
    int accCount = 0;
    std::array<float, kNumStrips> accStripL {}, accStripR {};
};
}
