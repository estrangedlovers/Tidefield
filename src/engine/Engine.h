#pragma once

#include "control/ControlEvent.h"
#include "control/SnapshotChannel.h"
#include "control/SpscQueue.h"
#include "control/Telemetry.h"
#include "master/MasterChain.h"
#include "mix/ChannelStrip.h"
#include "params/ParamRegistry.h"
#include "params/ParamState.h"
#include "scene/SceneSet.h"
#include "scene/Wander.h"

#include <dsp/core/Smoother.h>
#include <dsp/sources/drone/DroneGenerator.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace tf::engine {

/** Tidefield's audio engine. Host-agnostic: the app's device callback, the offline
    render harness, the tests and (later) a plugin AudioProcessor all drive it the
    same way.

    Threading:
      - prepare()/release(): message thread, audio stopped.
      - process(): audio thread only. Never allocates, locks or does I/O.
      - post(), publishScenes(), collectGarbage(): one producer thread (the message
        thread or the render harness).
      - popTelemetry()/popNotice(): one consumer thread (the message thread). */
class Engine
{
public:
    /** Control tick length. Sub-blocks are aligned to absolute sample positions so the
        output does not depend on the host block size. */
    static constexpr int kControlInterval = 32;

    /** The terrain is evaluated every this many control ticks (~2.7 ms at 48 kHz).
        Parameter smoothing hides the steps, and it keeps large scene sets cheap. */
    static constexpr int kTerrainDecimation = 4;

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
        convention), so inputs are always copied to scratch before anything is written.
        numSamples may exceed the prepared block size; the engine chunks internally. */
    void process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept;

    // --- Producer side (one thread) ---------------------------------------------------
    bool post(const ControlEvent& event) noexcept;
    bool setParam(P p, float value) noexcept { return post(ControlEvent::setParam(idx(p), value)); }
    bool command(Command c) noexcept { return post(ControlEvent::makeCommand(c)); }

    /** Hands a new terrain to the audio thread. Returns false if the previous few
        snapshots have not been retired yet (call collectGarbage() and retry). */
    bool publishScenes(std::unique_ptr<SceneSet> scenes) { return sceneChannel.publish(std::move(scenes)); }

    /** Frees snapshots the audio thread has retired. Call regularly (e.g. UI timer). */
    void collectGarbage() { sceneChannel.collectGarbage(); }

    // --- Consumer side (one thread) ---------------------------------------------------
    bool popTelemetry(TelemetryFrame& out) noexcept { return telemetryQueue.pop(out); }
    bool popNotice(EngineNotice& out) noexcept { return noticeQueue.pop(out); }

    // --- Read-only info -----------------------------------------------------------------
    const ParamRegistry& getRegistry() const noexcept { return registry; }
    int getLatencySamples() const noexcept { return master.getLatencySamples(); }
    double getSampleRate() const noexcept { return sampleRate; }
    std::uint64_t getSampleTime() const noexcept { return sampleTime; }

private:
    void drainControl() noexcept;
    void applyEvent(const ControlEvent& e) noexcept;
    void applyCommand(Command c) noexcept;
    void controlTick() noexcept;
    void updateTerrain(float dtSeconds) noexcept;
    void resetFeedback() noexcept;
    void processChunk(int offset, int numSamples) noexcept;
    void notify(EngineNotice::Type type) noexcept;
    void accumulateTelemetry(const float* l, const float* r, int n) noexcept;
    bool terrainActive() const noexcept;

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

    // Terrain state (audio thread).
    dsp::OnePoleSmoother cursorX, cursorY;
    Wander wander;
    Point2 cursor {}, position {};
    std::array<float, kMaxScenes> weights {};
    std::vector<std::uint8_t> live; // per parameter: held in the live layer

    dsp::DroneGenerator drone;
    ChannelStrip droneStrip;
    MasterChain master;

    std::vector<float> sourceL, sourceR, masterL, masterR;

    // Telemetry accumulation.
    int telemetryInterval = 800;
    int telemetryCountdown = 0;
    float accPeakL = 0.0f, accPeakR = 0.0f;
    double accSumL = 0.0, accSumR = 0.0;
    int accCount = 0;
};

} // namespace tf::engine
