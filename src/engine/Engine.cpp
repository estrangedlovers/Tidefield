#include "Engine.h"

#include <dsp/core/Denormal.h>
#include <dsp/core/MathUtil.h>

#include <algorithm>
#include <cmath>

namespace tf::engine {

Engine::Engine() : Engine(Config {}) {}

Engine::Engine(const Config& c)
    : config(c),
      controlQueue(c.controlQueueSize),
      telemetryQueue(c.telemetryQueueSize),
      noticeQueue(c.noticeQueueSize)
{
}

void Engine::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max(maxBlockSize, kControlInterval);
    sampleTime = 0;

    const dsp::ProcessSpec spec { sampleRate, maxBlock };
    params.prepare(registry, sampleRate);
    drone.prepare(spec, config.seed);
    master.prepare(spec);

    const auto n = static_cast<std::size_t>(maxBlock);
    sourceL.assign(n, 0.0f);
    sourceR.assign(n, 0.0f);
    masterL.assign(n, 0.0f);
    masterR.assign(n, 0.0f);

    telemetryInterval = std::max(1, static_cast<int>(sampleRate / config.telemetryRateHz));
    telemetryCountdown = telemetryInterval;

    // Prime the first tick so ramps start from settled values rather than zero.
    controlTick();
    droneStrip.settle();
}

void Engine::release()
{
    sourceL.clear();
    sourceR.clear();
    masterL.clear();
    masterR.clear();
}

bool Engine::post(const ControlEvent& event) noexcept { return controlQueue.push(event); }

void Engine::notify(EngineNotice::Type type) noexcept
{
    noticeQueue.push(EngineNotice { type, sampleTime }); // dropped if the UI is not reading
}

void Engine::drainControl() noexcept
{
    ControlEvent e;
    while (controlQueue.pop(e))
    {
        if (e.type == ControlEvent::Type::SetParam)
            params.setTarget(e.param, e.value);
        else
            applyCommand(e.command);
    }
}

void Engine::applyCommand(Command c) noexcept
{
    switch (c)
    {
        // Fade length is a time setting, not a sound parameter: use the latest target so a
        // length posted in the same batch as the fade applies to it.
        case Command::FadeIn:
            master.setFadeSeconds(params.target(idx(P::MasterFadeSecs)));
            master.fadeIn();
            break;
        case Command::FadeOut:
            master.setFadeSeconds(params.target(idx(P::MasterFadeSecs)));
            master.fadeOut();
            break;
        case Command::Panic: master.panic(); break;
        case Command::ResumeFromPanic:
            master.setFadeSeconds(params.target(idx(P::MasterFadeSecs)));
            master.resumeFromPanic();
            break;
        case Command::ResetFeedback: resetFeedback(); break;
        case Command::None: break;
    }
}

void Engine::resetFeedback() noexcept
{
    drone.reset();
    master.reset();
}

void Engine::controlTick() noexcept
{
    params.advance(kControlInterval);

    dsp::DroneGenerator::Params d;
    d.rootNote = params.current(P::DroneRoot);
    d.detuneCents = params.current(P::DroneDetune);
    d.shape = params.current(P::DroneShape);
    d.cutoffHz = params.current(P::DroneCutoff);
    d.resonance = params.current(P::DroneResonance);
    d.noise = params.current(P::DroneNoise);
    d.driftDepth = params.current(P::DroneDriftDepth);
    d.driftRate = params.current(P::DroneDriftRate);
    d.density = params.current(P::DroneDensity);
    d.evolve = params.current(P::DroneEvolve);
    d.spread = params.current(P::DroneSpread);
    drone.setParams(d);

    droneStrip.update({ params.current(P::DroneLevel), params.current(P::DronePan), params.current(P::DroneWidth) });

    master.setFadeSeconds(params.current(P::MasterFadeSecs));
    master.setCeilingDb(params.current(P::MasterCeiling));
}

void Engine::processChunk(int offset, int n) noexcept
{
    // Sources render into scratch, strips sum into the master bus.
    constexpr float kTide = 1.0f; // Tide arrives in phase 3.
    float* sl = sourceL.data() + offset;
    float* sr = sourceR.data() + offset;
    drone.process(sl, sr, n, kTide);

    const int tickPos = static_cast<int>(sampleTime % kControlInterval);
    droneStrip.processAdd(sl, sr, masterL.data() + offset, masterR.data() + offset, n, tickPos, kControlInterval);
}

void Engine::process(const float* const* /*inputs*/, int /*numInputs*/, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const dsp::ScopedFlushDenormals noDenormals;

    drainControl();

    int done = 0;
    while (done < numSamples)
    {
        const int block = std::min(numSamples - done, maxBlock);
        std::fill_n(masterL.data(), block, 0.0f);
        std::fill_n(masterR.data(), block, 0.0f);

        // Master level ramps across the whole block, sampled at its edges.
        const float levelStart = dsp::dbToGain(params.current(P::MasterLevel));

        int offset = 0;
        while (offset < block)
        {
            const int tickPos = static_cast<int>(sampleTime % kControlInterval);
            if (tickPos == 0)
                controlTick();
            const int chunk = std::min(block - offset, kControlInterval - tickPos);
            processChunk(offset, chunk);
            offset += chunk;
            sampleTime += static_cast<std::uint64_t>(chunk);
        }

        const float levelEnd = dsp::dbToGain(params.current(P::MasterLevel));
        const auto ev = master.process(masterL.data(), masterR.data(), block, levelStart, levelEnd);

        if (ev.guardTripped)
        {
            drone.reset();
            notify(EngineNotice::Type::GuardTripped);
        }
        if (ev.panicReachedSilence)
        {
            resetFeedback();
            notify(EngineNotice::Type::PanicSilent);
        }
        if (ev.fadeInCompleted)
            notify(EngineNotice::Type::FadeInComplete);
        if (ev.fadeOutCompleted)
            notify(EngineNotice::Type::FadeOutComplete);

        // Write to the device. Mono outputs get the sum; extra channels are silent
        // until multichannel output is designed in.
        if (numOutputs == 1)
        {
            for (int i = 0; i < block; ++i)
                outputs[0][done + i] = 0.5f * (masterL[static_cast<std::size_t>(i)] + masterR[static_cast<std::size_t>(i)]);
        }
        else if (numOutputs >= 2)
        {
            std::copy_n(masterL.data(), block, outputs[0] + done);
            std::copy_n(masterR.data(), block, outputs[1] + done);
            for (int ch = 2; ch < numOutputs; ++ch)
                std::fill_n(outputs[ch] + done, block, 0.0f);
        }

        accumulateTelemetry(masterL.data(), masterR.data(), block);
        done += block;
    }
}

void Engine::accumulateTelemetry(const float* l, const float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        accPeakL = std::max(accPeakL, std::fabs(l[i]));
        accPeakR = std::max(accPeakR, std::fabs(r[i]));
        accSumL += static_cast<double>(l[i]) * static_cast<double>(l[i]);
        accSumR += static_cast<double>(r[i]) * static_cast<double>(r[i]);
    }
    accCount += n;
    telemetryCountdown -= n;
    if (telemetryCountdown > 0)
        return;

    TelemetryFrame f;
    f.sampleTime = sampleTime;
    f.peakL = accPeakL;
    f.peakR = accPeakR;
    f.rmsL = static_cast<float>(std::sqrt(accSumL / std::max(1, accCount)));
    f.rmsR = static_cast<float>(std::sqrt(accSumR / std::max(1, accCount)));
    f.limiterGain = master.getLimiterGain();
    f.fadeGain = master.getFadeGain();
    f.fadeState = master.getFadeState();
    f.panicActive = master.isPanicActive();
    f.guardTrips = master.getGuardTrips();
    for (int v = 0; v < dsp::DroneGenerator::kMaxVoices; ++v)
    {
        f.droneVoiceLevel[static_cast<std::size_t>(v)] = drone.getVoiceLevel(v);
        f.droneVoiceInterval[static_cast<std::size_t>(v)] = drone.getVoiceInterval(v);
    }
    telemetryQueue.push(f); // dropped if the UI is behind; the next frame supersedes it

    accPeakL = accPeakR = 0.0f;
    accSumL = accSumR = 0.0;
    accCount = 0;
    telemetryCountdown += telemetryInterval;
}

} // namespace tf::engine
