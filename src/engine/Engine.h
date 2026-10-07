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
#include "params/ParamRegistry.h"
#include "params/ParamState.h"
#include "record/RecordTap.h"
#include "SampleHandle.h"
#include "scene/SceneSet.h"
#include "scene/Wander.h"

#include <dsp/core/SampleBuffer.h>
#include <dsp/core/Smoother.h>
#include <dsp/fx/medium/Medium.h>
#include <dsp/harmony/HarmonicGravity.h>
#include <dsp/sources/bloom/BloomSampler.h>
#include <dsp/sources/drone/DroneGenerator.h>
#include <dsp/sources/granular/GranularCloud.h>
#include <dsp/sources/input/LiveInput.h>
#include <dsp/sources/resonator/ResonatorBank.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace tf::engine {

/** Tidefield's audio engine. Host-agnostic: the app's device callback, the offline
    render harness, the tests and (later) a plugin AudioProcessor all drive it the
    same way.

    Signal flow per control tick (32 samples):
      live input -> drone -> clouds 1-4 -> resonator (excited by input/drone/clouds)
      each source -> 2 insert slots -> strip (level, pan, width, sends A/B)
    then per block:
      bus A (2 slots, reverb by default) and bus B (2 slots, delay) return to master
      master -> 2 insert slots -> Medium -> level/fade/DC/limiter/panic

    Threading:
      - prepare()/release(): message thread, audio stopped.
      - process(): audio thread only. Never allocates, locks or does I/O.
      - post(), publishScenes(), loadCloudSample(), sendProcessor(), collect*():
        one producer thread (the message thread or the render harness).
      - popTelemetry()/popNotice(): one consumer thread (the message thread). */
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

    /** Renders numSamples. `inputs` may alias `outputs` (AudioProcessor in-place
        convention): each block's inputs are read before that block's outputs are
        written. numSamples may exceed the prepared block size. */
    void process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept;

    // --- Producer side (one thread) ---------------------------------------------------
    bool post(const ControlEvent& event) noexcept;
    bool setParam(P p, float value) noexcept { return post(ControlEvent::setParam(idx(p), value)); }
    bool command(Command c) noexcept { return post(ControlEvent::makeCommand(c)); }

    bool publishScenes(std::unique_ptr<SceneSet> scenes) { return sceneChannel.publish(std::move(scenes)); }

    /** Hands a sample to a granular cloud (0..3); nullptr unloads it. The cloud fades
        out, swaps, fades back in. Returns false if too many swaps are queued (retry
        after collectGarbage). */
    bool loadCloudSample(int cloud, std::shared_ptr<const dsp::SampleBuffer> buffer);

    /** The sample most recently sent to a cloud (message thread; for saving). */
    std::shared_ptr<const dsp::SampleBuffer> getCloudSample(int cloud) const;

    /** The one-shot Bloom plays (nullptr unloads). Sounding voices fade quickly first. */
    bool loadBloomSample(std::shared_ptr<const dsp::SampleBuffer> buffer);
    std::shared_ptr<const dsp::SampleBuffer> getBloomSample() const { return bloomMirror; }

    bool noteOn(int note, float velocity) noexcept { return post(ControlEvent::note(note, velocity)); }
    bool noteOff(int note) noexcept { return post(ControlEvent::note(note, 0.0f)); }

    /** Copies a caught region (from an EngineNotice::CatchReady) out of the capture
        ring. Message thread. Returns false if the region has already been
        overwritten (the notice was handled more than ~10 s late). */
    bool copyCatch(const EngineNotice& notice, dsp::SampleBuffer& out) const;

    /** MIDI from a device. Each port has its own queue (one producer per queue: the
        thread that delivers that device's messages). */
    bool postMidi(int port, const RawMidi& message) noexcept;
    bool publishMidiMap(std::unique_ptr<MidiMap> map) { return midiMapChannel.publish(std::move(map)); }
    /** Message thread: every MIDI message the engine received, for learn and activity. */
    bool popMidiMonitor(RawMidi& out) noexcept { return midiMonitor.pop(out); }

    /** FX slots are fed by FxManager; see there. */
    bool sendProcessor(int slot, dsp::ProcessorPtr processor);
    int collectProcessors(int slot);

    /** Frees retired scene sets and sample buffers. Call regularly (UI timer). */
    void collectGarbage();

    /** Recording: the disk writer is the tap's consumer (see io::Recorder). */
    RecordTap& getRecordTap() noexcept { return recordTap; }

    /** CPU guardrails: measure each block's DSP time and trim grains, modes and voices
        when the load stays high. Off by default so offline renders stay deterministic;
        the app turns it on. Any thread. */
    void setGuardrailsEnabled(bool enabled) noexcept { guardEnabled.store(enabled, std::memory_order_relaxed); }
    /** Tests: use this load instead of measuring (negative = measure). */
    void forceLoadForTesting(float load) noexcept { forcedLoad.store(load, std::memory_order_relaxed); }

    // --- Consumer side (one thread) ---------------------------------------------------
    bool popTelemetry(TelemetryFrame& out) noexcept { return telemetryQueue.pop(out); }
    bool popNotice(EngineNotice& out) noexcept { return noticeQueue.pop(out); }

    // --- Read-only info -----------------------------------------------------------------
    const ParamRegistry& getRegistry() const noexcept { return registry; }
    /** Limiter lookahead plus the Medium's base delay. */
    int getLatencySamples() const noexcept { return master.getLatencySamples() + medium.getLatencySamples(); }
    double getSampleRate() const noexcept { return sampleRate; }
    dsp::ProcessSpec getProcessSpec() const noexcept { return { sampleRate, maxBlock }; }
    std::uint64_t getSampleTime() const noexcept { return sampleTime; }

private:
    struct CloudSlot
    {
        dsp::GranularCloud cloud;
        SnapshotChannel<SampleHandle> buffers { 4 };
        std::shared_ptr<const dsp::SampleBuffer> mirror; // message thread only
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

    // MIDI.
    std::array<std::unique_ptr<SpscQueue<RawMidi>>, kMaxMidiPorts> midiQueues;
    SpscQueue<RawMidi> midiMonitor { 512 };
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

    double sampleRate = 48000.0;
    int maxBlock = 0;
    std::uint64_t sampleTime = 0;
    std::uint64_t tickCount = 0;
    float tide = 1.0f;

    // Terrain state.
    dsp::OnePoleSmoother cursorX, cursorY;
    Wander wander;
    Point2 cursor {}, position {};
    std::array<float, kMaxScenes> weights {};
    std::vector<std::uint8_t> live;

    // Sources and processing.
    dsp::HarmonicGravity harmony;
    dsp::LiveInput liveInput;
    dsp::DroneGenerator drone;
    std::array<CloudSlot, kNumClouds> clouds;
    dsp::ResonatorBank resonator;
    dsp::BloomSampler bloom;
    SnapshotChannel<SampleHandle> bloomBuffers { 4 };
    std::shared_ptr<const dsp::SampleBuffer> bloomMirror; // message thread only
    bool bloomSwapping = false;
    std::array<ChannelStrip, kNumStrips> strips;
    std::array<FxSlot, kNumFxSlots> fxSlots;
    dsp::Medium medium;
    MasterChain master;

    // Buffers (maxBlock samples each).
    std::array<std::vector<float>, kNumStrips> stripL, stripR;
    std::vector<float> busAL, busAR, busBL, busBR, masterL, masterR;
    std::vector<float> inputMono, excite, scratchDryL, scratchDryR, scratchAltL, scratchAltR;

    // Catch: rings of recent master output and live input. Written on the audio thread;
    // regions are read by copyCatch() after the CatchReady notice (whose queue release
    // orders those writes before the read) while the writer is >= 10 s away.
    static constexpr double kCatchRingSeconds = 40.0;
    std::vector<float> catchL, catchR, catchIn;
    std::size_t catchCapacity = 0;
    std::atomic<std::uint64_t> catchWritten { 0 }; // master frames ever written
    std::atomic<std::uint64_t> inputWritten { 0 };
    void writeCatch(const float* l, const float* r, int n) noexcept;
    void requestCatch() noexcept;

    // Recording: stems are post-fader strips then the two bus returns (maxBlock each),
    // filled only while a stem recording runs.
    RecordTap recordTap;
    std::array<std::vector<float>, RecordTap::kStemPairs> stemL, stemR;
    int recordStride = 0; // this block's
    void pushRecording(int numSamples) noexcept;

    // CPU guardrails.
    DegradationPolicy guard;
    std::atomic<bool> guardEnabled { false };
    std::atomic<float> forcedLoad { -1.0f };
    float droneVoiceCap = static_cast<float>(dsp::DroneGenerator::kMaxVoices);

    // Telemetry accumulation.
    int telemetryInterval = 800;
    int telemetryCountdown = 0;
    float accPeakL = 0.0f, accPeakR = 0.0f;
    double accSumL = 0.0, accSumR = 0.0;
    int accCount = 0;
    std::array<float, kNumStrips> accStripL {}, accStripR {};
};

} // namespace tf::engine
