#pragma once

#include "control/ControlEvent.h"
#include "control/SnapshotChannel.h"
#include "control/SpscQueue.h"
#include "control/Telemetry.h"
#include "master/MasterChain.h"
#include "mix/ChannelStrip.h"
#include "mix/FxSlot.h"
#include "mix/Layout.h"
#include "params/ParamRegistry.h"
#include "params/ParamState.h"
#include "scene/SceneSet.h"
#include "scene/Wander.h"

#include <dsp/core/SampleBuffer.h>
#include <dsp/core/Smoother.h>
#include <dsp/fx/medium/Medium.h>
#include <dsp/harmony/HarmonicGravity.h>
#include <dsp/sources/drone/DroneGenerator.h>
#include <dsp/sources/granular/GranularCloud.h>
#include <dsp/sources/input/LiveInput.h>
#include <dsp/sources/resonator/ResonatorBank.h>

#include <array>
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

    /** Hands a sample to a granular cloud (0..3). The cloud fades out, swaps, fades
        back in. Returns false if too many swaps are queued (retry after collect). */
    bool loadCloudSample(int cloud, std::unique_ptr<dsp::SampleBuffer> buffer);

    /** FX slots are fed by FxManager; see there. */
    bool sendProcessor(int slot, dsp::ProcessorPtr processor);
    int collectProcessors(int slot);

    /** Frees retired scene sets and sample buffers. Call regularly (UI timer). */
    void collectGarbage();

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
        SnapshotChannel<dsp::SampleBuffer> buffers { 4 };
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
    void accumulateTelemetry(const float* l, const float* r, int n) noexcept;
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
    std::array<ChannelStrip, kNumStrips> strips;
    std::array<FxSlot, kNumFxSlots> fxSlots;
    dsp::Medium medium;
    MasterChain master;

    // Buffers (maxBlock samples each).
    std::array<std::vector<float>, kNumStrips> stripL, stripR;
    std::vector<float> busAL, busAR, busBL, busBR, masterL, masterR;
    std::vector<float> inputMono, excite, scratchDryL, scratchDryR, scratchAltL, scratchAltR;

    // Telemetry accumulation.
    int telemetryInterval = 800;
    int telemetryCountdown = 0;
    float accPeakL = 0.0f, accPeakR = 0.0f;
    double accSumL = 0.0, accSumR = 0.0;
    int accCount = 0;
    std::array<float, kNumStrips> accStripL {}, accStripR {};
};

} // namespace tf::engine
