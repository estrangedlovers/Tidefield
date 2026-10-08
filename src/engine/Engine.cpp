#include "Engine.h"

#include "scene/TerrainMath.h"

#include <dsp/core/Denormal.h>
#include <dsp/core/MathUtil.h>

#include <algorithm>
#include <limits>
#include <chrono>
#include <cmath>

namespace tf::engine {
namespace {
enum CloudOffset : int { kDensity, kGrainMs, kPosition, kSpray, kScan, kPitch, kPitchSpread, kHarmonize, kReverse, kShape, kStereo, kGravity };

P offsetParam(P first, int offset) noexcept { return static_cast<P>(idx(first) + offset); }

int toInt(float v) noexcept { return static_cast<int>(std::lround(v)); }

dsp::Scale scaleFrom(float root, float scaleIndex) noexcept
{
    const int s = std::clamp(toInt(scaleIndex), 0, static_cast<int>(dsp::kScaleTypes.size()) - 1);
    return { dsp::kScaleTypes[static_cast<std::size_t>(s)].mask, std::clamp(toInt(root), 0, 11) };
}
}

Engine::Engine() : Engine(Config {}) {}

Engine::Engine(const Config& c)
    : config(c),
      controlQueue(c.controlQueueSize),
      telemetryQueue(c.telemetryQueueSize),
      noticeQueue(c.noticeQueueSize)
{
    paramFlags.reserve(registry.size());
    for (const auto& s : registry.all())
        paramFlags.push_back(s.flags);
    live.assign(registry.size(), 0);
    midiPickup.assign(registry.size(), 0);
    for (auto& q : midiQueues)
        q = std::make_unique<SpscQueue<RawMidi>>(1024);
}

void Engine::prepare(double newSampleRate, int maxBlockSize)
{
    stopGesture(false);
    hostSampleTime = 0;
    hostPlaying = false;
    sampleRate = newSampleRate;
    maxBlock = std::max(maxBlockSize, kControlInterval);
    sampleTime = 0;
    tickCount = 0;

    const dsp::ProcessSpec spec { sampleRate, maxBlock };
    params.prepare(registry, sampleRate);

    harmony.snapTo(scaleFrom(params.current(P::HarmonyRoot), params.current(P::HarmonyScale)));
    liveInput.prepare(spec);
    drone.prepare(spec, config.seed);
    drone.setHarmony(&harmony);
    for (int k = 0; k < kNumClouds; ++k)
    {
        auto& slot = clouds[static_cast<std::size_t>(k)];
        slot.cloud.prepare(spec, config.seed + 100u + static_cast<std::uint64_t>(k));
        slot.cloud.setHarmony(&harmony);
        slot.cloud.setBuffer(rawBuffer(slot.buffers.current()));
        slot.swap = CloudSlot::Swap::Idle;
        slot.swapGain = 1.0f;
    }
    resonator.prepare(spec, config.seed + 200u);
    resonator.setHarmony(&harmony);
    bloom.prepare(spec, config.seed + 400u);
    bloom.setHarmony(&harmony);
    bloom.setBuffer(rawBuffer(bloomBuffers.current()));
    bloomSwapping = false;
    looper.prepare(spec, config.seed + 500u);
    weather.prepare(spec, config.seed + 600u);
    inputFreeze.prepare(spec, config.seed + 700u);
    freezeCloud.prepare(spec, config.seed + 800u);
    freezeCloud.setHarmony(&harmony);
    freezeBuffer.sampleRate = sampleRate;
    freezeBuffer.name = "freeze";
    freezeBuffer.left.assign(static_cast<std::size_t>(kFreezeSeconds * sampleRate), 0.0f);
    freezeBuffer.right.assign(freezeBuffer.left.size(), 0.0f);
    freezeCloud.setBuffer(nullptr);
    freezeLoaded = false;
    freezeGain = 0.0f;
    preCapacity = static_cast<std::size_t>(kFreezeRingSeconds * sampleRate) + static_cast<std::size_t>(maxBlock);
    preRingL.assign(preCapacity, 0.0f);
    preRingR.assign(preCapacity, 0.0f);
    preWritten = 0;
    medium.prepare(spec, config.seed + 300u);
    autoMaster.prepare(spec);
    master.prepare(spec);
    for (auto& slot : fxSlots)
        slot.prepareAll(spec);

    const double terrainRate = sampleRate / (kControlInterval * kTerrainDecimation);
    cursorX.prepare(terrainRate, 1.0f, false);
    cursorY.prepare(terrainRate, 1.0f, false);
    cursorX.reset(params.current(P::TerrainX));
    cursorY.reset(params.current(P::TerrainY));
    wander.setSeed(config.seed ^ 0x77616e64ull);
    loopRng.setSeed(config.seed ^ 0x6c6f6f70ull);
    modRng.setSeed(config.seed ^ 0x6d6f6475ull);
    lfoPhase = {};
    lfoStep = {};
    randomValue = randomTarget = randomClock = {};
    modValue = {};
    inputEnergy = inputDiffEnergy = mixEnergy = 0.0;
    inputEnergyCount = mixEnergyCount = 0;
    inputPrev = inputFollow = brightFollow = mixFollow = 0.0f;
    lastVelocity = modWheel = pressure = 0.0f;
    lastNote = 60.0f;
    loopPattern = -1;
    loopCycle.fill(std::numeric_limits<std::int64_t>::min());
    beatPos = 0.0;
    swellEnv = hushEnv = slowEnv = 0.0f;
    seasonVersion = 0;

    const auto n = static_cast<std::size_t>(maxBlock);
    for (int s = 0; s < kNumStrips; ++s)
    {
        stripL[static_cast<std::size_t>(s)].assign(n, 0.0f);
        stripR[static_cast<std::size_t>(s)].assign(n, 0.0f);
    }
    for (auto* v : { &busAL, &busAR, &busBL, &busBR, &masterL, &masterR, &inputMono, &excite, &scratchDryL, &scratchDryR,
                     &scratchAltL, &scratchAltR, &loopInL, &loopInR, &padL, &padR, &freezeGainBuf })
        v->assign(n, 0.0f);

    for (std::size_t k = 0; k < stemL.size(); ++k)
    {
        stemL[k].assign(n, 0.0f);
        stemR[k].assign(n, 0.0f);
    }
    recordTap.prepare(sampleRate);
    recordStride = 0;
    guard.reset();
    applyGuardLimits(guard.getLimits());

    catchCapacity = static_cast<std::size_t>(kCatchRingSeconds * sampleRate);
    catchL.assign(catchCapacity, 0.0f);
    catchR.assign(catchCapacity, 0.0f);
    catchIn.assign(catchCapacity, 0.0f);
    catchWritten.store(0);
    inputWritten.store(0);

    telemetryInterval = std::max(1, static_cast<int>(sampleRate / config.telemetryRateHz));
    telemetryCountdown = telemetryInterval;

    controlTick();
    for (auto& strip : strips)
        strip.settle();
}

void Engine::release()
{
    for (std::size_t k = 0; k < stemL.size(); ++k)
    {
        stemL[k].clear();
        stemR[k].clear();
    }
    for (int s = 0; s < kNumStrips; ++s)
    {
        stripL[static_cast<std::size_t>(s)].clear();
        stripR[static_cast<std::size_t>(s)].clear();
    }
    for (auto* v : { &busAL, &busAR, &busBL, &busBR, &masterL, &masterR, &inputMono, &excite, &scratchDryL, &scratchDryR,
                     &scratchAltL, &scratchAltR, &loopInL, &loopInR, &padL, &padR, &freezeGainBuf })
        v->clear();
}

bool Engine::post(const ControlEvent& event) noexcept { return controlQueue.push(event); }

bool Engine::loadCloudSample(int cloud, std::shared_ptr<const dsp::SampleBuffer> buffer)
{
    if (cloud < 0 || cloud >= kNumClouds)
        return false;
    auto& slot = clouds[static_cast<std::size_t>(cloud)];
    auto handle = std::make_unique<SampleHandle>();
    handle->buffer = buffer;
    if (! slot.buffers.publish(std::move(handle)))
        return false;
    slot.mirror = std::move(buffer);
    return true;
}

bool Engine::loadBloomSample(std::shared_ptr<const dsp::SampleBuffer> buffer)
{
    auto handle = std::make_unique<SampleHandle>();
    handle->buffer = buffer;
    if (! bloomBuffers.publish(std::move(handle)))
        return false;
    bloomMirror = std::move(buffer);
    return true;
}

bool Engine::previewSample(std::shared_ptr<const dsp::SampleBuffer> buffer)
{
    auto handle = std::make_unique<SampleHandle>();
    handle->buffer = std::move(buffer);
    return previewBuffers.publish(std::move(handle));
}

void Engine::mixPreview(int n) noexcept
{
    if (previewBuffers.acquire())
    {
        preview = rawBuffer(previewBuffers.current());
        previewPos = 0.0;
    }
    if (preview == nullptr || preview->size() < 2)
        return;
    const double step = preview->sampleRate / sampleRate;
    const double size = static_cast<double>(preview->size());
    const double fadeLength = 0.01 * preview->sampleRate;
    const bool stereo = preview->isStereo();
    for (int i = 0; i < n; ++i)
    {
        if (previewPos >= size - 1.0)
        {
            preview = nullptr;
            return;
        }
        const auto k = static_cast<std::size_t>(previewPos);
        const float frac = static_cast<float>(previewPos - static_cast<double>(k));
        const float l = dsp::lerp(preview->left[k], preview->left[k + 1], frac);
        const float r = stereo ? dsp::lerp(preview->right[k], preview->right[k + 1], frac) : l;
        const float edge = static_cast<float>(std::min({ 1.0, previewPos / fadeLength, (size - previewPos) / fadeLength }));
        masterL[static_cast<std::size_t>(i)] += 0.5f * edge * l;
        masterR[static_cast<std::size_t>(i)] += 0.5f * edge * r;
        previewPos += step;
    }
}

std::shared_ptr<const dsp::SampleBuffer> Engine::getCloudSample(int cloud) const
{
    if (cloud < 0 || cloud >= kNumClouds)
        return nullptr;
    return clouds[static_cast<std::size_t>(cloud)].mirror;
}

bool Engine::copyCatch(const EngineNotice& notice, dsp::SampleBuffer& out) const
{
    if (catchCapacity == 0 || notice.type != EngineNotice::Type::CatchReady)
        return false;
    const bool fromInput = notice.source == 1;
    const auto written = (fromInput ? inputWritten : catchWritten).load(std::memory_order_acquire);
    if (written > notice.start + catchCapacity)
        return false;

    out.sampleRate = sampleRate;
    out.left.resize(notice.length);
    out.right.clear();
    if (! fromInput)
        out.right.resize(notice.length);
    for (std::uint32_t i = 0; i < notice.length; ++i)
    {
        const auto idx = static_cast<std::size_t>((notice.start + i) % catchCapacity);
        if (fromInput)
        {
            out.left[i] = catchIn[idx];
        }
        else
        {
            out.left[i] = catchL[idx];
            out.right[i] = catchR[idx];
        }
    }
    const auto after = (fromInput ? inputWritten : catchWritten).load(std::memory_order_acquire);
    return after <= notice.start + catchCapacity;
}

void Engine::writeCatch(const float* l, const float* r, int n) noexcept
{
    auto pos = catchWritten.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++pos)
    {
        const auto idx = static_cast<std::size_t>(pos % catchCapacity);
        catchL[idx] = l[i];
        catchR[idx] = r[i];
    }
    catchWritten.store(pos, std::memory_order_release);
}

void Engine::requestCatch() noexcept
{
    const bool fromInput = toInt(params.target(idx(P::CatchSource))) == 1;
    const auto written = (fromInput ? inputWritten : catchWritten).load(std::memory_order_relaxed);
    const auto wanted = static_cast<std::uint64_t>(params.target(idx(P::CatchSeconds)) * sampleRate);
    const auto length = std::min(wanted, written);
    if (length == 0)
        return;
    EngineNotice n;
    n.type = EngineNotice::Type::CatchReady;
    n.sampleTime = sampleTime;
    n.start = written - length;
    n.length = static_cast<std::uint32_t>(length);
    n.source = fromInput ? 1 : 0;
    n.target = static_cast<std::uint8_t>(std::clamp(toInt(params.target(idx(P::CatchTarget))), 0, kNumClouds));
    noticeQueue.push(n);
}

bool Engine::sendProcessor(int slot, dsp::ProcessorPtr processor)
{
    if (slot < 0 || slot >= kNumFxSlots)
        return false;
    return fxSlots[static_cast<std::size_t>(slot)].send(std::move(processor));
}

int Engine::collectProcessors(int slot)
{
    if (slot < 0 || slot >= kNumFxSlots)
        return 0;
    return fxSlots[static_cast<std::size_t>(slot)].collect();
}

void Engine::collectGarbage()
{
    sceneChannel.collectGarbage();
    seasonChannel.collectGarbage();
    pathChannel.collectGarbage();
    modChannel.collectGarbage();
    gestureChannel.collectGarbage();
    midiMapChannel.collectGarbage();
    bloomBuffers.collectGarbage();
    previewBuffers.collectGarbage();
    for (auto& c : clouds)
        c.buffers.collectGarbage();
}

void Engine::notify(EngineNotice::Type type) noexcept
{
    noticeQueue.push(EngineNotice { type, sampleTime });
}

bool Engine::terrainActive() const noexcept
{
    const auto* set = sceneChannel.current();
    return set != nullptr && set->numScenes > 0;
}

bool Engine::postMidi(int port, const RawMidi& message) noexcept
{
    if (port < 0 || port >= kMaxMidiPorts)
        return false;
    auto m = message;
    m.port = static_cast<std::uint8_t>(port);
    return midiQueues[static_cast<std::size_t>(port)]->push(m);
}

void Engine::drainControl() noexcept
{
    sceneChannel.acquire();
    seasonChannel.acquire();
    pathChannel.acquire();
    modChannel.acquire();
    gestureChannel.acquire();
    if (midiMapChannel.acquire())
    {
        pickups.fill({});
        std::fill(midiPickup.begin(), midiPickup.end(), std::int8_t { 0 });
    }
    for (auto& slot : fxSlots)
        slot.acquire();
    ControlEvent e;
    while (controlQueue.pop(e))
        applyEvent(e);
    RawMidi m;
    for (auto& q : midiQueues)
        while (q->pop(m))
            handleMidi(m);
}

void Engine::fireMidiAction(MidiAction action) noexcept
{
    switch (action)
    {
        case MidiAction::Catch: requestCatch(); break;
        case MidiAction::Panic: master.isPanicActive() ? applyCommand(Command::ResumeFromPanic) : applyCommand(Command::Panic); break;
        case MidiAction::ReleaseLive: applyCommand(Command::ReleaseLiveLayer); break;
        case MidiAction::FadeToggle:
        {
            const auto st = master.getFadeState();
            applyCommand(st == FadeState::Silent || st == FadeState::FadingOut ? Command::FadeIn : Command::FadeOut);
            break;
        }
        case MidiAction::CaptureScene: notify(EngineNotice::Type::CaptureSceneRequest); break;
        case MidiAction::RecordToggle: notify(EngineNotice::Type::RecordToggleRequest); break;
        case MidiAction::LoopRecord: looper.record(); break;
        case MidiAction::LoopClear: looper.clear(); break;
        case MidiAction::FreezeToggle:
            params.setTarget(idx(P::FreezeOn), params.target(idx(P::FreezeOn)) > 0.5f ? 0.0f : 1.0f);
            break;
        case MidiAction::InputFreezeToggle:
            params.setTarget(idx(P::InputFreeze), params.target(idx(P::InputFreeze)) > 0.5f ? 0.0f : 1.0f);
            break;
        case MidiAction::None: break;
    }
}

void Engine::applyMidiBinding(std::size_t index, const MidiBinding& b, int value) noexcept
{
    auto& st = pickups[index];

    if (b.action != MidiAction::None)
    {
        const bool down = value >= 64;
        if (down && ! st.buttonDown)
            fireMidiAction(b.action);
        st.buttonDown = down;
        return;
    }
    if (b.param >= registry.size())
        return;

    const float v = static_cast<float>(value) / 127.0f;
    float shaped = v;
    if (b.curve > 0.0f)
        shaped = std::pow(v, 1.0f + 3.0f * b.curve);
    else if (b.curve < 0.0f)
        shaped = 1.0f - std::pow(1.0f - v, 1.0f - 3.0f * b.curve);
    const float wanted = b.low + (b.high - b.low) * shaped;

    const auto& spec = registry.spec(b.param);
    const float current = spec.toNormalised(params.target(b.param));
    constexpr float kTolerance = 0.025f;

    if (b.pickup)
    {
        if (st.caught && std::fabs(current - st.lastSent) > kTolerance)
            st.caught = false;
        if (! st.caught)
        {
            const bool near = std::fabs(wanted - current) < kTolerance;
            const bool crossed = st.hasLast && (st.lastController - current) * (wanted - current) <= 0.0f;
            st.caught = near || crossed;
        }
    }
    st.lastController = wanted;
    st.hasLast = true;

    if (! b.pickup || st.caught)
    {
        applyEvent(ControlEvent::setParam(b.param, spec.fromNormalised(wanted), ControlSource::Midi));
        st.lastSent = wanted;
        midiPickup[b.param] = 0;
    }
    else
    {
        midiPickup[b.param] = wanted > current ? 1 : -1;
    }
}

void Engine::handleClock(const RawMidi& m) noexcept
{
    if (params.current(P::SyncSource) < 0.5f)
        return;
    switch (m.status)
    {
        case 0xf8:
        {
            if (lastClockTime > 0.0 && m.time > lastClockTime)
            {
                const double interval = m.time - lastClockTime;
                if (interval > 60.0 / (400.0 * 24.0) && interval < 60.0 / (20.0 * 24.0))
                    clockInterval = clockInterval <= 0.0 ? interval : clockInterval + (interval - clockInterval) * 0.08;
            }
            lastClockTime = m.time;
            if (clockRunning)
                ++clockTicks;
            if (clockInterval > 0.0)
            {
                hostBpm = std::clamp(60.0 / (24.0 * clockInterval), 20.0, 400.0);
                hostPpq = static_cast<double>(clockTicks) / 24.0;
                hostSampleTime = sampleTime;
                hostPlaying = clockRunning;
                clockDriven = true;
            }
            break;
        }
        case 0xfa:
            clockTicks = 0;
            clockRunning = true;
            break;
        case 0xfb: clockRunning = true; break;
        case 0xfc:
            clockRunning = false;
            hostPlaying = false;
            break;
        case 0xf2: clockTicks = static_cast<std::int64_t>((m.data1 & 0x7f) | ((m.data2 & 0x7f) << 7)) * 6; break;
        default: break;
    }
}

void Engine::handleMidi(const RawMidi& m) noexcept
{
    if (m.status >= 0xf0)
        return handleClock(m);
    midiMonitor.push(m);

    const auto* map = midiMapChannel.current();
    const int ch = m.channel();

    if (m.isCc() || m.isNoteOn() || m.isNoteOff())
    {
        const int src = m.isCc() ? 0 : 1;
        if (map != nullptr)
        {
            const auto s = static_cast<std::size_t>(src), c = static_cast<std::size_t>(ch), n = static_cast<std::size_t>(m.data1 & 127);
            const int count = map->count[s][c][n];
            const int first = map->start[s][c][n];
            const int value = m.isCc() ? m.data2 : (m.isNoteOn() ? 127 : 0);
            for (int i = 0; i < count; ++i)
            {
                const auto bi = map->targets[static_cast<std::size_t>(first + i)];
                if (bi < kMaxMidiBindings)
                    applyMidiBinding(bi, map->bindings[bi], value);
            }
            if (src == 1 && count > 0)
                return;
        }
        if (m.isCc())
        {
            if (m.data1 == 1)
                modWheel = static_cast<float>(m.data2) / 127.0f;
            if (m.data1 == 74 && map != nullptr && map->mpe && ch != 0)
            {
                const auto c = static_cast<std::size_t>(ch);
                mpeTimbre[c] = static_cast<float>(m.data2) / 127.0f;
                bloom.setChannelExpression(ch, mpeBend[c], mpePressure[c], mpeTimbre[c]);
            }
            if (m.data1 == 64)
            {
                sustainPedal = m.data2 >= 64;
                bloom.setSustain(sustainPedal);
            }
            return;
        }
    }

    const bool mpe = map != nullptr && map->mpe;
    const bool member = mpe && ch != 0;
    const auto uc = static_cast<std::size_t>(ch);
    const bool channelOk = mpe || map == nullptr || map->noteChannel < 0 || map->noteChannel == ch;
    if (! channelOk)
        return;
    if (m.type() == 0xe0)
    {
        const float bend = static_cast<float>(((m.data1 & 0x7f) | ((m.data2 & 0x7f) << 7)) - 8192) / 8192.0f;
        if (member)
        {
            mpeBend[uc] = bend * 48.0f;
            bloom.setChannelExpression(ch, mpeBend[uc], mpePressure[uc], mpeTimbre[uc]);
        }
        else
            bloom.setGlobalBend(bend * 2.0f);
        return;
    }
    if (member && m.type() == 0xd0)
    {
        mpePressure[uc] = static_cast<float>(m.data1) / 127.0f;
        bloom.setChannelExpression(ch, mpeBend[uc], mpePressure[uc], mpeTimbre[uc]);
    }
    if (m.type() == 0xd0)
        pressure = static_cast<float>(m.data1) / 127.0f;
    else if (m.type() == 0xa0)
        pressure = std::max(pressure * 0.98f, static_cast<float>(m.data2) / 127.0f);
    if (m.isNoteOn())
    {
        trackNote(m.data1, static_cast<float>(m.data2) / 127.0f);
        if (member)
            bloom.setChannelExpression(ch, mpeBend[uc], mpePressure[uc], mpeTimbre[uc]);
        bloom.noteOn(m.data1, static_cast<float>(m.data2) / 127.0f, member ? ch : -1);
        if (map != nullptr && map->notesToDrone)
        {
            int root = m.data1;
            while (root > 60)
                root -= 12;
            while (root < 24)
                root += 12;
            applyEvent(ControlEvent::setParam(idx(P::DroneRoot), static_cast<float>(root), ControlSource::Midi));
        }
    }
    else if (m.isNoteOff())
    {
        bloom.noteOff(m.data1);
    }
}

void Engine::recordGesture(const ControlEvent& e) noexcept
{
    if (gestureState != GestureState::Recording || e.source == ControlSource::Terrain || e.source == ControlSource::Score)
        return;
    if (e.type == ControlEvent::Type::SnapParam)
        return;
    if (e.type == ControlEvent::Type::Command && e.command != Command::Catch && e.command != Command::LoopRecord && e.command != Command::LoopClear)
        return;
    if (e.type == ControlEvent::Type::Note)
        recordHeld.set(e.param & 127u, e.value > 0.0f);
    gestureOut.push({ sampleTime - gestureStart, e, recordGeneration });
}

void Engine::releasePlayedNotes() noexcept
{
    for (int n = 0; n < 128; ++n)
        if (playHeld.test(static_cast<std::size_t>(n)))
            bloom.noteOff(n);
    playHeld.reset();
}

void Engine::stopGesture(bool onAudioThread) noexcept
{
    if (gestureState == GestureState::Recording)
    {
        const auto end = sampleTime - gestureStart;
        if (onAudioThread)
            for (int n = 0; n < 128; ++n)
                if (recordHeld.test(static_cast<std::size_t>(n)))
                    gestureOut.push({ end, ControlEvent::note(n, 0.0f), recordGeneration });
        recordHeld.reset();
        endMarker = { end, ControlEvent::makeCommand(Command::None), recordGeneration };
        endMarker.event.value = static_cast<float>(sampleRate);
        endPending = true;
        if (onAudioThread)
            flushGestureEnd();
    }
    if (onAudioThread)
        releasePlayedNotes();
    else
        playHeld.reset();
    gestureState = GestureState::Idle;
    pendingPlayVersion = 0.0f;
}

void Engine::flushGestureEnd() noexcept
{
    if (endPending && gestureOut.push(endMarker))
        endPending = false;
}

void Engine::updateGesture() noexcept
{
    flushGestureEnd();
    const auto* take = gestureChannel.current();

    if (pendingPlayVersion > 0.0f && take != nullptr && static_cast<float>(take->version) >= pendingPlayVersion)
    {
        pendingPlayVersion = 0.0f;
        if (take->length > 0)
        {
            gestureState = GestureState::Playing;
            gestureStart = sampleTime;
            gestureIndex = 0;
            gesturePlayedVersion = take->version;
        }
    }
    if (gestureState != GestureState::Playing)
        return;
    if (take == nullptr || take->length == 0 || take->version != gesturePlayedVersion)
    {
        releasePlayedNotes();
        gestureState = GestureState::Idle;
        return;
    }
    const double scale = take->sampleRate > 0.0 ? sampleRate / take->sampleRate : 1.0;
    const auto length = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(static_cast<double>(take->length) * scale));
    auto pos = sampleTime - gestureStart;
    while (true)
    {
        while (gestureIndex < take->events.size()
               && static_cast<std::uint64_t>(static_cast<double>(take->events[gestureIndex].time) * scale) <= pos)
        {
            auto e = take->events[gestureIndex++].event;
            e.source = ControlSource::Score;
            if (e.type == ControlEvent::Type::Note)
                playHeld.set(e.param & 127u, e.value > 0.0f);
            applyEvent(e);
        }
        if (pos < length)
            break;
        releasePlayedNotes();
        if (! take->loop)
        {
            gestureState = GestureState::Idle;
            break;
        }
        gestureStart += length;
        gestureIndex = 0;
        pos = sampleTime - gestureStart;
    }
}

void Engine::applyEvent(const ControlEvent& e) noexcept
{
    recordGesture(e);
    switch (e.type)
    {
        case ControlEvent::Type::SetParam:
            if (e.param >= registry.size())
                return;
            if ((paramFlags[e.param] & ParamFlag::kTerrainBound) != 0 && terrainActive() && e.source != ControlSource::Terrain)
                live[e.param] = 1;
            params.setTarget(e.param, e.value);
            break;

        case ControlEvent::Type::SnapParam:
            if (e.param < registry.size())
                params.snap(e.param, e.value);
            break;

        case ControlEvent::Type::ReleaseParam:
            if (e.param < live.size())
                live[e.param] = 0;
            break;

        case ControlEvent::Type::Command:
            if (e.command == Command::GestureRecord)
            {
                stopGesture(true);
                gestureState = GestureState::Recording;
                gestureStart = sampleTime;
                recordGeneration = e.param;
                recordHeld.reset();
            }
            else if (e.command == Command::GesturePlay)
            {
                stopGesture(true);
                pendingPlayVersion = std::max(1.0f, e.value);
            }
            else
                applyCommand(e.command);
            break;

        case ControlEvent::Type::Note:
            trackNote(static_cast<int>(e.param), e.value);
            if (e.value > 0.0f)
                bloom.noteOn(static_cast<int>(e.param), e.value);
            else
                bloom.noteOff(static_cast<int>(e.param));
            break;
    }
}

void Engine::applyCommand(Command c) noexcept
{
    switch (c)
    {
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
        case Command::ReleaseLiveLayer: std::fill(live.begin(), live.end(), std::uint8_t { 0 }); break;
        case Command::Catch: requestCatch(); break;
        case Command::LoopRecord: looper.record(); break;
        case Command::LoopClear: looper.clear(); break;
        case Command::GestureRecord:
        case Command::GesturePlay: break;
        case Command::GestureStop: stopGesture(true); break;
        case Command::None: break;
    }
}

void Engine::resetFeedback() noexcept
{
    liveInput.reset();
    drone.reset();
    for (auto& c : clouds)
        c.cloud.reset();
    resonator.reset();
    bloom.reset();
    looper.reset();
    weather.reset();
    swellEnv = hushEnv = slowEnv = 0.0f;
    inputFreeze.reset();
    freezeCloud.reset();
    freezeGain = 0.0f;
    freezeLoaded = false;
    for (auto& slot : fxSlots)
        slot.reset();
    medium.reset();
    autoMaster.reset();
    master.reset();
}

void Engine::updateTerrain(float dt) noexcept
{
    const float glide = params.current(P::TerrainGlide);
    cursorX.setTimeConstant(glide);
    cursorY.setTimeConstant(glide);
    cursorX.setTarget(params.current(P::TerrainX));
    cursorY.setTarget(params.current(P::TerrainY));
    cursor = { cursorX.next(), cursorY.next() };

    const auto* set = sceneChannel.current();
    const auto style = static_cast<Wander::Style>(std::clamp(toInt(params.current(P::TerrainWanderStyle)), 0, 4));
    position = wander.update(cursor, params.current(P::TerrainWander), params.current(P::TerrainWanderRate), style, set, dt * tide,
                             pathChannel.current());

    if (set == nullptr || set->numScenes == 0)
        return;

    terrain::computeWeights(*set, position, params.current(P::TerrainFocus), weights.data());
    for (std::size_t c = 0; c < set->columns.size(); ++c)
    {
        const auto p = set->columns[c].param;
        if (live[p] == 0)
            params.setTarget(p, terrain::blendColumn(*set, c, weights.data()));
    }
}

void Engine::updateSources(float t) noexcept
{
    const float globalGravity = params.current(P::HarmonyGravity);

    dsp::DroneGenerator::Params d;
    d.rootNote = params.current(P::DroneRoot);
    d.detuneCents = params.current(P::DroneDetune);
    d.shape = params.current(P::DroneShape);
    d.cutoffHz = params.current(P::DroneCutoff);
    d.resonance = params.current(P::DroneResonance);
    d.noise = params.current(P::DroneNoise);
    d.driftDepth = params.current(P::DroneDriftDepth);
    d.driftRate = params.current(P::DroneDriftRate);
    d.density = std::min(params.current(P::DroneDensity), droneVoiceCap);
    d.evolve = params.current(P::DroneEvolve);
    d.spread = params.current(P::DroneSpread);
    d.gravity = params.current(P::DroneGravity) * globalGravity;
    drone.setParams(d);

    for (int k = 0; k < kNumClouds; ++k)
    {
        const P first = kCloudFirstParam[static_cast<std::size_t>(k)];
        auto get = [&](int offset) { return params.current(offsetParam(first, offset)); };
        dsp::GranularCloud::Params c;
        c.density = get(kDensity);
        c.grainMs = get(kGrainMs);
        c.position = get(kPosition);
        c.spray = get(kSpray);
        c.scan = get(kScan);
        c.pitch = get(kPitch);
        c.pitchSpread = get(kPitchSpread);
        c.harmonize = get(kHarmonize);
        c.reverse = get(kReverse);
        c.shape = get(kShape);
        c.stereo = get(kStereo);
        c.gravity = get(kGravity) * globalGravity;
        c.rootNote = params.current(P::DroneRoot) + 12.0f;
        clouds[static_cast<std::size_t>(k)].cloud.setParams(c);
    }

    dsp::ResonatorBank::Params r;
    r.rootNote = params.current(P::ResRoot);
    r.modes = std::clamp(toInt(params.current(P::ResModes)), 1, dsp::ResonatorBank::kMaxModes);
    r.structure = params.current(P::ResStructure);
    r.decaySeconds = params.current(P::ResDecay);
    r.brightness = params.current(P::ResBrightness);
    r.rain = params.current(P::ResRain);
    r.rainColour = params.current(P::ResRainColour);
    r.spread = params.current(P::ResSpread);
    r.gravity = params.current(P::ResGravity) * globalGravity;
    resonator.setParams(r);

    dsp::BloomSampler::Params b;
    b.transform = static_cast<dsp::BloomSampler::Transform>(std::clamp(toInt(params.current(P::BloomTransform)), 0, dsp::BloomSampler::kNumTransforms - 1));
    b.amount = params.current(P::BloomAmount);
    b.lengthSeconds = params.current(P::BloomLength);
    b.attackSeconds = params.current(P::BloomAttack);
    b.releaseSeconds = params.current(P::BloomRelease);
    b.rootNote = params.current(P::BloomRoot);
    b.pitch = params.current(P::BloomPitch);
    b.tone = params.current(P::BloomTone);
    b.spread = params.current(P::BloomSpread);
    b.random = params.current(P::BloomRandom);
    b.position = params.current(P::BloomPosition);
    b.gravity = params.current(P::BloomGravity) * globalGravity;
    bloom.setParams(b);

    dsp::LiveInput::Params in;
    in.channel = static_cast<dsp::LiveInput::Channel>(std::clamp(toInt(params.current(P::InputChannel)), 0, 2));
    in.gainDb = params.current(P::InputGain);
    in.highPassHz = params.current(P::InputHighPass);
    in.gateDb = params.current(P::InputGate);
    liveInput.setParams(in);

    inputFreeze.setDrift(params.current(P::InputFreezeDrift));

    dsp::Disintegrator::Params lp;
    lp.erosion = params.current(P::LoopErosion);
    lp.flakes = params.current(P::LoopFlakes);
    lp.overdub = params.current(P::LoopOverdub);
    looper.setParams(lp);

    dsp::WeatherBed::Params wp;
    wp.wind = params.current(P::WeatherWind);
    wp.rain = params.current(P::WeatherRain);
    wp.surf = params.current(P::WeatherSurf);
    wp.gust = params.current(P::WeatherGust);
    wp.tone = params.current(P::WeatherTone);
    wp.distance = params.current(P::WeatherDistance);
    weather.setParams(wp);

    const float tex = params.current(P::FreezeTexture);
    dsp::GranularCloud::Params fc;
    fc.density = 25.0f + 60.0f * tex;
    fc.grainMs = 150.0f + 650.0f * tex;
    fc.position = 0.5f;
    fc.spray = 1.0f;
    fc.pitchSpread = 0.04f + 0.1f * tex;
    fc.reverse = 0.3f * tex;
    fc.stereo = 0.9f;
    freezeCloud.setParams(fc);
    (void) t;
}

std::array<float, 6> Engine::slotControls(int slot) const noexcept
{
    const auto first = idx(kFxSlots[static_cast<std::size_t>(slot)].firstParam);
    std::array<float, 6> c {};
    for (int i = 0; i < 6; ++i)
        c[static_cast<std::size_t>(i)] = params.current(static_cast<ParamIndex>(first + i));
    return c;
}

void Engine::updateFx(float t) noexcept
{
    const dsp::ModContext ctx { t, &harmony, syncOn ? 60.0f / std::max(20.0f, bpm) : 0.0f };
    for (int s = 0; s < kNumFxSlots; ++s)
        if (fxSlots[static_cast<std::size_t>(s)].isActive())
            fxSlots[static_cast<std::size_t>(s)].setControls(slotControls(s), ctx);

    dsp::Medium::Params m;
    m.type = static_cast<dsp::Medium::Type>(std::clamp(toInt(params.current(P::MediumType)), 0, dsp::Medium::kNumTypes - 1));
    m.age = params.current(P::MediumAge);
    m.noise = params.current(P::MediumNoise);
    m.wobble = params.current(P::MediumWobble);
    m.drive = params.current(P::MediumDrive);
    m.mix = params.current(P::MediumMix);
    medium.setParams(m);
    medium.setTimeScale(t);

    static constexpr float kLoudnessTargets[] = { -23.0f, -16.0f, -14.0f };
    dsp::AutoMaster::Params am;
    am.enabled = params.current(P::MasterAuto) > 0.5f;
    am.targetLufs = kLoudnessTargets[std::clamp(toInt(params.current(P::MasterAutoTarget)), 0, 2)];
    am.amount = params.current(P::MasterAutoAmount);
    autoMaster.setParams(am);
}

void Engine::controlTick() noexcept
{
    const float tickSeconds = static_cast<float>(kControlInterval / sampleRate);
    if (tickCount++ % kTerrainDecimation == 0)
        updateTerrain(tickSeconds * kTerrainDecimation);

    updateModulation(tickSeconds);
    params.advance(kControlInterval);
    tide = params.current(P::TideRate);

    harmony.setTarget(scaleFrom(params.current(P::HarmonyRoot), params.current(P::HarmonyScale)));
    harmony.setMorphSeconds(params.current(P::HarmonyMorph));
    harmony.advance(tickSeconds * tide);

    updateSources(tide);
    updateGesture();
    updateTempo(tickSeconds);
    updateLoops(tickSeconds);

    for (int s = 0; s < kNumStrips; ++s)
    {
        const auto& info = kStrips[static_cast<std::size_t>(s)];
        ChannelStrip::Settings st;
        st.levelDb = params.current(info.level);
        st.pan = params.current(info.pan);
        st.width = params.current(info.width);
        st.sendADb = params.current(info.sendA);
        st.sendBDb = params.current(info.sendB);
        st.gate = 1.0f;
        strips[static_cast<std::size_t>(s)].update(st);
    }

    updateFx(tide);
    master.setFadeSeconds(params.current(P::MasterFadeSecs));
    master.setCeilingDb(params.current(P::MasterCeiling));
}

void Engine::updateModulation(float dt) noexcept
{
    params.clearModulation();

    const float hold = params.current(P::SwellHold);
    const float time = hold > swellEnv ? params.current(P::SwellAttack) : params.current(P::SwellRelease) / std::max(0.05f, tide);
    swellEnv += (hold - swellEnv) * (1.0f - std::exp(-3.0f * dt / std::max(0.01f, time)));
    swellEnv = dsp::flushDenormal(swellEnv);
    const float e = dsp::smoothstep(std::clamp(swellEnv, 0.0f, 1.0f)) * params.current(P::SwellDepth);
    if (e > 1.0e-4f)
    {
        for (const auto& info : kStrips)
        {
            params.addModulation(idx(info.sendA), 0.35f * e);
            params.addModulation(idx(info.sendB), 0.2f * e);
        }
        params.addModulation(idx(P::DroneCutoff), 0.3f * e);
        params.addModulation(idx(P::ResBrightness), 0.3f * e);
        params.addModulation(idx(P::BloomTone), 0.25f * e);
        params.addModulation(idx(P::BusALevel), 0.05f * e);
        for (auto first : kCloudFirstParam)
            params.addModulation(idx(first), 0.15f * e);
    }

    const float colour = params.current(P::PerformColour);
    if (colour != 0.0f)
    {
        params.addModulation(idx(P::DroneCutoff), 0.25f * colour);
        params.addModulation(idx(P::ResBrightness), 0.3f * colour);
        params.addModulation(idx(P::BloomTone), 0.3f * colour);
        params.addModulation(idx(P::WeatherTone), 0.3f * colour);
        params.addModulation(idx(P::MediumAge), -0.2f * colour);
    }
    const float space = params.current(P::PerformSpace);
    if (space != 0.0f)
    {
        for (const auto& info : kStrips)
        {
            params.addModulation(idx(info.sendA), 0.3f * space);
            params.addModulation(idx(info.width), 0.15f * space);
        }
        params.addModulation(idx(P::BusALevel), 0.06f * space);
        params.addModulation(idx(P::WeatherDistance), 0.4f * space);
    }

    {
        const float hushHold = params.current(P::HushHold);
        const float hushTime = hushHold > hushEnv ? 1.2f : 3.0f / std::max(0.05f, tide);
        hushEnv = dsp::flushDenormal(hushEnv + (hushHold - hushEnv) * (1.0f - std::exp(-3.0f * dt / hushTime)));
        const float h = dsp::smoothstep(std::clamp(hushEnv, 0.0f, 1.0f)) * params.current(P::HushDepth);
        if (h > 1.0e-4f)
            for (const auto& info : kStrips)
                params.addModulation(idx(info.level), -0.45f * h);
    }

    {
        const float slowHold = params.current(P::SlowHold);
        slowEnv = dsp::flushDenormal(slowEnv + (slowHold - slowEnv) * (1.0f - std::exp(-3.0f * dt / 2.0f)));
        if (slowEnv > 1.0e-4f)
            params.addModulation(idx(P::TideRate), -0.273f * dsp::smoothstep(std::clamp(slowEnv, 0.0f, 1.0f)));
    }

    updateModSources(dt);
    if (const auto* routes = modChannel.current())
        for (int k = 0; k < routes->count; ++k)
        {
            const auto& r = routes->routes[static_cast<std::size_t>(k)];
            const float depth = params.current(static_cast<ParamIndex>(idx(P::ModRoute1Depth) + routes->slot[static_cast<std::size_t>(k)]));
            if (depth != 0.0f)
                params.addModulation(r.param, depth * modValue[static_cast<std::size_t>(r.source)]);
        }

    const auto* set = seasonChannel.current();
    if (set == nullptr)
        return;
    if (set->version != seasonVersion)
    {
        seasonVersion = set->version;
        for (int k = 0; k < set->count; ++k)
        {
            seasonPhase[static_cast<std::size_t>(k)] = set->seasons[static_cast<std::size_t>(k)].phase;
            seasonDrift[static_cast<std::size_t>(k)].setSeed(config.seed * 31u + static_cast<std::uint64_t>(k));
        }
    }
    const float depthAll = params.current(P::SeasonsDepth);
    for (int k = 0; k < set->count; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        const auto& s = set->seasons[uk];
        auto& ph = seasonPhase[uk];
        ph += dt * tide / s.periodSeconds;
        ph -= std::floor(ph);
        float v = 0.0f;
        switch (s.shape)
        {
            case Season::Shape::Sine: v = std::sin(dsp::kTwoPi * ph); break;
            case Season::Shape::Triangle: v = 1.0f - 4.0f * std::fabs(ph - 0.5f); break;
            case Season::Shape::Drift:
                seasonDrift[uk].setRate(2.0f / s.periodSeconds);
                v = seasonDrift[uk].advance(dt * tide);
                break;
        }
        seasonValue[uk] = v;
        params.addModulation(s.param, v * s.depth * depthAll);
    }
}

void Engine::updateModSources(float dt) noexcept
{
    const float scaled = dt * tide;
    for (int k = 0; k < kNumLfos; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        const auto rateParam = static_cast<ParamIndex>(idx(P::ModLfo1Rate) + 2 * k);
        const float rate = params.current(rateParam);
        const int shape = static_cast<int>(std::lround(params.current(static_cast<ParamIndex>(rateParam + 1))));
        float& ph = lfoPhase[uk];
        ph += rate * scaled;
        if (ph >= 1.0f)
        {
            ph -= std::floor(ph);
            lfoStep[uk] = modRng.nextBipolar();
        }
        float v = 0.0f;
        switch (shape)
        {
            case 0: v = std::sin(dsp::kTwoPi * ph); break;
            case 1: v = 1.0f - 4.0f * std::fabs(ph - 0.5f); break;
            case 2: v = 2.0f * ph - 1.0f; break;
            case 3: v = ph < 0.5f ? 1.0f : -1.0f; break;
            default: v = lfoStep[uk]; break;
        }
        modValue[static_cast<std::size_t>(ModSource::Lfo1) + uk] = v;
    }
    for (int k = 0; k < kNumRandoms; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        const auto rateParam = static_cast<ParamIndex>(idx(P::ModRandom1Rate) + 2 * k);
        const float rate = params.current(rateParam);
        const float smooth = params.current(static_cast<ParamIndex>(rateParam + 1));
        randomClock[uk] += rate * scaled;
        if (randomClock[uk] >= 1.0f)
        {
            randomClock[uk] -= std::floor(randomClock[uk]);
            randomTarget[uk] = modRng.nextBipolar();
        }
        const float glideSeconds = smooth * 0.98f / std::max(0.01f, rate) + 1.0e-3f;
        randomValue[uk] += (randomTarget[uk] - randomValue[uk]) * (1.0f - std::exp(-scaled / glideSeconds));
        randomValue[uk] = dsp::flushDenormal(randomValue[uk]);
        modValue[static_cast<std::size_t>(ModSource::Random1) + uk] = randomValue[uk];
    }

    const float attack = params.current(P::ModFollowAttack);
    const float release = params.current(P::ModFollowRelease);
    const float sensitivity = dsp::dbToGain(params.current(P::ModFollowGain));
    auto follow = [&](float& state, float target) {
        const float t = target > state ? attack : release;
        state = dsp::flushDenormal(state + (target - state) * (1.0f - std::exp(-dt / t)));
        return std::clamp(state, 0.0f, 1.0f);
    };
    auto levelOf = [&](double energy, int count) {
        if (count <= 0)
            return 0.0f;
        const float rms = static_cast<float>(std::sqrt(energy / count)) * sensitivity;
        const float db = 20.0f * std::log10(std::max(rms, 1.0e-6f));
        return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    };
    const float inLevel = levelOf(inputEnergy, inputEnergyCount);
    const float bright = inputEnergy > 1.0e-9 ? std::clamp(static_cast<float>(std::sqrt(inputDiffEnergy / (4.0 * inputEnergy))) * 2.5f, 0.0f, 1.0f)
                                              : 0.0f;
    modValue[static_cast<std::size_t>(ModSource::InputLevel)] = follow(inputFollow, inLevel);
    modValue[static_cast<std::size_t>(ModSource::InputBrightness)] = follow(brightFollow, inLevel > 0.05f ? bright : brightFollow);
    modValue[static_cast<std::size_t>(ModSource::MixLevel)] = follow(mixFollow, levelOf(mixEnergy, mixEnergyCount));
    inputEnergy = inputDiffEnergy = mixEnergy = 0.0;
    inputEnergyCount = mixEnergyCount = 0;

    modValue[static_cast<std::size_t>(ModSource::Velocity)] = lastVelocity;
    modValue[static_cast<std::size_t>(ModSource::NotePitch)] = std::clamp((lastNote - 24.0f) / 72.0f, 0.0f, 1.0f);
    modValue[static_cast<std::size_t>(ModSource::ModWheel)] = modWheel;
    modValue[static_cast<std::size_t>(ModSource::Pressure)] = pressure;
    modValue[static_cast<std::size_t>(ModSource::TerrainX)] = position.x;
    modValue[static_cast<std::size_t>(ModSource::TerrainY)] = position.y;
}

void Engine::trackNote(int note, float velocity) noexcept
{
    if (velocity <= 0.0f)
        return;
    lastVelocity = std::clamp(velocity, 0.0f, 1.0f);
    lastNote = static_cast<float>(note);
}

void Engine::updateTempo(float dt) noexcept
{
    if (clockDriven && params.current(P::SyncSource) < 0.5f)
    {
        clockDriven = clockRunning = false;
        hostBpm = 0.0;
        hostPlaying = false;
        clockInterval = lastClockTime = 0.0;
    }
    syncOn = params.current(P::SyncOn) > 0.5f;
    bpm = hostBpm > 0.0 ? static_cast<float>(std::clamp(hostBpm, 20.0, 400.0)) : params.current(P::SyncBpm);
    if (hostPlaying)
        beatPos = hostPpq + static_cast<double>(sampleTime - hostSampleTime) / sampleRate * static_cast<double>(bpm) / 60.0;
    else
        beatPos += static_cast<double>(dt) * static_cast<double>(bpm) / 60.0;
}

void Engine::updateLoops(float dt) noexcept
{
    static constexpr std::array<float, kMaxLoops> kPeriods { 17.0f, 19.7f, 23.3f, 26.3f, 29.9f, 31.7f, 37.1f, 41.3f };

    const int pattern = toInt(params.current(P::LoopsPattern));
    if (pattern != loopPattern)
    {
        loopPattern = pattern;
        dsp::Random r(0x100957ull + static_cast<std::uint64_t>(pattern) * 7919u);
        for (int k = 0; k < kMaxLoops; ++k)
        {
            loopOffset[static_cast<std::size_t>(k)] = r.nextFloat();
            loopPhase[static_cast<std::size_t>(k)] = r.nextFloat();
        }
    }

    const bool on = params.current(P::LoopsOn) > 0.5f;
    const int count = std::clamp(toInt(params.current(P::LoopsCount)), 1, kMaxLoops);
    const float pace = params.current(P::LoopsRate) * tide;
    const float density = params.current(P::LoopsDensity);
    const float reg = params.current(P::LoopsRegister);
    const float spread = params.current(P::LoopsSpread) * 12.0f;
    const float velocity = params.current(P::LoopsVelocity);
    const int target = std::clamp(toInt(params.current(P::LoopsTarget)), 0, 2);
    const float flashDecay = std::exp(-dt / 0.4f);

    static constexpr std::array<int, 40> kPrimes { 2,  3,  5,  7,  11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
                                                   73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173 };
    const double paceSteps = std::clamp(std::round(std::log2(static_cast<double>(std::max(0.01f, params.current(P::LoopsRate))))), -2.0, 2.0);
    const int startBeats = static_cast<int>(std::round(23.0 * std::pow(2.0, -paceSteps)));
    std::size_t firstPrime = 0;
    while (firstPrime + kMaxLoops < kPrimes.size() && kPrimes[firstPrime] < startBeats)
        ++firstPrime;

    for (int k = 0; k < kMaxLoops; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        loopFlash[uk] *= flashDecay;
        loopNote[uk] = harmony.quantize(reg + loopOffset[uk] * spread, loopOffset[uk], 1.0f);
        if (k >= count)
            continue;
        if (syncOn)
        {
            const double period = kPrimes[firstPrime + uk];
            const double offsetBeats = std::round(static_cast<double>(loopOffset[uk]) * period);
            const double t = (beatPos + offsetBeats) / period;
            const auto cycle = static_cast<std::int64_t>(std::floor(t));
            loopPhase[uk] = static_cast<float>(t - std::floor(t));
            const auto previous = loopCycle[uk];
            loopCycle[uk] = cycle;
            const bool periodChanged = loopSyncPeriod[uk] != period;
            loopSyncPeriod[uk] = period;
            if (periodChanged || cycle != previous + 1)
                continue;
        }
        else
        {
            loopPhase[uk] += dt * pace / kPeriods[uk];
            if (loopPhase[uk] < 1.0f)
                continue;
            loopPhase[uk] -= std::floor(loopPhase[uk]);
            loopCycle[uk] = std::numeric_limits<std::int64_t>::min();
        }
        if (! on || ! loopRng.chance(density))
            continue;
        const float vel = velocity * (0.75f + 0.25f * loopRng.nextFloat());
        const int note = std::clamp(toInt(loopNote[uk]), 0, 127);
        if (target != 1)
        {
            bloom.noteOn(note, vel);
            bloom.noteOff(note);
        }
        if (target != 0)
            resonator.strike(vel);
        loopFlash[uk] = 1.0f;
    }
}

float Engine::mixRamp(P mixParam, int tickPos) const noexcept
{
    const float prev = params.previous(mixParam);
    const float cur = params.current(mixParam);
    return dsp::lerp(prev, cur, static_cast<float>(tickPos) / kControlInterval);
}

void Engine::processChunk(const float* const* inputs, int numInputs, int inputOffset, int offset, int n) noexcept
{
    const auto o = static_cast<std::size_t>(offset);
    const int tickPos = static_cast<int>(sampleTime % kControlInterval);
    auto L = [&](StripId s) { return stripL[static_cast<std::size_t>(s)].data() + o; };
    auto R = [&](StripId s) { return stripR[static_cast<std::size_t>(s)].data() + o; };

    float* in = inputMono.data() + o;
    liveInput.process(inputs, numInputs, inputOffset, in, n);
    for (int i = 0; i < n; ++i)
    {
        const float d = in[i] - inputPrev;
        inputPrev = in[i];
        inputEnergy += static_cast<double>(in[i]) * in[i];
        inputDiffEnergy += static_cast<double>(d) * d;
    }
    inputEnergyCount += n;
    inputPrev = dsp::flushDenormal(inputPrev);
    {
        auto pos = inputWritten.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++pos)
            catchIn[static_cast<std::size_t>(pos % catchCapacity)] = in[i];
        inputWritten.store(pos, std::memory_order_release);
    }

    const bool droneHeard = ! strips[static_cast<std::size_t>(StripId::Drone)].isSilent() || params.current(P::ResExciteDrone) > 0.0f;
    if (droneHeard)
        drone.process(L(StripId::Drone), R(StripId::Drone), n, tide);
    else
    {
        std::fill_n(L(StripId::Drone), n, 0.0f);
        std::fill_n(R(StripId::Drone), n, 0.0f);
    }

    const float swapStep = 1.0f / static_cast<float>(kCloudSwapSeconds * sampleRate);
    for (int k = 0; k < kNumClouds; ++k)
    {
        auto& slot = clouds[static_cast<std::size_t>(k)];
        const auto id = static_cast<StripId>(static_cast<int>(StripId::Cloud1) + k);

        if (slot.swap == CloudSlot::Swap::Idle && slot.buffers.hasPending())
        {
            if (rawBuffer(slot.buffers.current()) == nullptr)
            {
                slot.buffers.acquire();
                slot.cloud.setBuffer(rawBuffer(slot.buffers.current()));
                slot.swap = CloudSlot::Swap::FadingIn;
                slot.swapGain = 0.0f;
            }
            else
            {
                slot.swap = CloudSlot::Swap::FadingOut;
            }
        }

        slot.cloud.process(L(id), R(id), n, tide);

        if (slot.swap != CloudSlot::Swap::Idle)
        {
            const float dir = slot.swap == CloudSlot::Swap::FadingOut ? -1.0f : 1.0f;
            for (int i = 0; i < n; ++i)
            {
                slot.swapGain = std::clamp(slot.swapGain + dir * swapStep, 0.0f, 1.0f);
                L(id)[i] *= slot.swapGain;
                R(id)[i] *= slot.swapGain;
            }
            if (slot.swap == CloudSlot::Swap::FadingOut && slot.swapGain <= 0.0f)
            {
                slot.buffers.acquire();
                slot.cloud.setBuffer(rawBuffer(slot.buffers.current()));
                slot.swap = CloudSlot::Swap::FadingIn;
            }
            else if (slot.swap == CloudSlot::Swap::FadingIn && slot.swapGain >= 1.0f)
            {
                slot.swap = CloudSlot::Swap::Idle;
            }
        }
    }

    if (bloomBuffers.hasPending())
    {
        if (! bloomSwapping)
        {
            bloom.releaseAll(0.02f);
            bloomSwapping = true;
        }
        if (bloom.isSilent())
        {
            bloomBuffers.acquire();
            bloom.setBuffer(rawBuffer(bloomBuffers.current()));
            bloomSwapping = false;
        }
    }
    bloom.process(L(StripId::Bloom), R(StripId::Bloom), n, tide);

    const float exBloom = params.current(P::ResExciteBloom);
    const float exIn = params.current(P::ResExciteInput);
    const float exDrone = params.current(P::ResExciteDrone);
    const float exClouds = params.current(P::ResExciteClouds);
    float* ex = excite.data() + o;
    for (int i = 0; i < n; ++i)
    {
        float cloudsMono = 0.0f;
        for (int k = 0; k < kNumClouds; ++k)
        {
            const auto id = static_cast<StripId>(static_cast<int>(StripId::Cloud1) + k);
            cloudsMono += L(id)[i] + R(id)[i];
        }
        ex[i] = in[i] * exIn + 0.5f * (L(StripId::Drone)[i] + R(StripId::Drone)[i]) * exDrone + 0.25f * cloudsMono * exClouds
                + 0.5f * (L(StripId::Bloom)[i] + R(StripId::Bloom)[i]) * exBloom;
    }
    if (! strips[static_cast<std::size_t>(StripId::Resonator)].isSilent())
        resonator.process(ex, L(StripId::Resonator), R(StripId::Resonator), n, tide);
    else
    {
        std::fill_n(L(StripId::Resonator), n, 0.0f);
        std::fill_n(R(StripId::Resonator), n, 0.0f);
    }

    {
        const bool frozen = params.current(P::InputFreeze) > 0.5f;
        inputFreeze.setFrozen(frozen);
        float* pl = padL.data() + o;
        float* pr = padR.data() + o;
        if (frozen || inputFreeze.getGain() > 0.0f || liveInput.getLevel() > 1.0e-5f)
            inputFreeze.process(in, pl, pr, n);
        else
        {
            std::fill_n(pl, n, 0.0f);
            std::fill_n(pr, n, 0.0f);
        }
        const float padGain = dsp::dbToGain(params.current(P::InputFreezeLevel));
        const float m0 = params.previous(P::InputArmed);
        const float m1 = params.current(P::InputArmed);
        for (int i = 0; i < n; ++i)
        {
            const float m = dsp::lerp(m0, m1, static_cast<float>(tickPos + i + 1) / kControlInterval);
            L(StripId::Input)[i] = in[i] * m + pl[i] * padGain;
            R(StripId::Input)[i] = in[i] * m + pr[i] * padGain;
        }
    }

    {
        const float* li = in;
        const float* ri = in;
        if (params.current(P::LoopSource) > 0.5f)
        {
            const auto lag = static_cast<std::uint64_t>(maxBlock);
            for (int i = 0; i < n; ++i)
            {
                const auto t = sampleTime + static_cast<std::uint64_t>(i);
                const auto ui = o + static_cast<std::size_t>(i);
                if (t < lag)
                {
                    loopInL[ui] = loopInR[ui] = 0.0f;
                    continue;
                }
                const auto ring = static_cast<std::size_t>((t - lag) % catchCapacity);
                loopInL[ui] = catchL[ring];
                loopInR[ui] = catchR[ring];
            }
            li = loopInL.data() + o;
            ri = loopInR.data() + o;
        }
        looper.process(li, ri, L(StripId::Loop), R(StripId::Loop), n);
    }

    if (params.current(P::WeatherWind) + params.current(P::WeatherRain) + params.current(P::WeatherSurf) > 0.0f)
        weather.process(L(StripId::Weather), R(StripId::Weather), n, tide);
    else
    {
        std::fill_n(L(StripId::Weather), n, 0.0f);
        std::fill_n(R(StripId::Weather), n, 0.0f);
    }

    processFreeze(offset, n);

    for (int s = 0; s < kNumStrips; ++s)
    {
        const auto sid = static_cast<StripId>(s);
        for (int slotIndex : { s * 2, s * 2 + 1 })
        {
            auto& slot = fxSlots[static_cast<std::size_t>(slotIndex)];
            if (! slot.isActive())
                continue;
            const P mixParam = offsetParam(kFxSlots[static_cast<std::size_t>(slotIndex)].firstParam, 6);
            slot.process(L(sid), R(sid), n, mixRamp(mixParam, tickPos), mixRamp(mixParam, tickPos + n), scratchDryL.data(),
                         scratchDryR.data(), scratchAltL.data(), scratchAltR.data());
        }
        if (sid == StripId::Freeze)
        {
            const float duck = params.current(P::FreezeDuck);
            const float* fg = freezeGainBuf.data() + o;
            if (duck > 0.0f && (fg[0] > 0.0f || fg[n - 1] > 0.0f))
                for (int i = 0; i < n; ++i)
                {
                    const float g = 1.0f - duck * fg[i];
                    const auto ui = o + static_cast<std::size_t>(i);
                    masterL[ui] *= g;
                    masterR[ui] *= g;
                    busAL[ui] *= g;
                    busAR[ui] *= g;
                    busBL[ui] *= g;
                    busBR[ui] *= g;
                }
        }
        const bool stems = recordStride > 2;
        const auto us = static_cast<std::size_t>(s);
        strips[us].processAdd(L(sid), R(sid), masterL.data() + o, masterR.data() + o, busAL.data() + o, busAR.data() + o,
                              busBL.data() + o, busBR.data() + o, n, tickPos, kControlInterval,
                              stems ? stemL[us].data() + o : nullptr, stems ? stemR[us].data() + o : nullptr);
    }
}

void Engine::captureFreeze() noexcept
{
    const auto len = static_cast<std::uint64_t>(freezeBuffer.size());
    const auto lag = static_cast<std::uint64_t>(maxBlock);
    const auto end = sampleTime > lag ? sampleTime - lag : 0;
    for (std::uint64_t k = 0; k < len; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        if (end + k < len)
        {
            freezeBuffer.left[i] = freezeBuffer.right[i] = 0.0f;
            continue;
        }
        const auto ring = static_cast<std::size_t>((end - len + k) % preCapacity);
        freezeBuffer.left[i] = preRingL[ring];
        freezeBuffer.right[i] = preRingR[ring];
    }
    freezeCloud.reset();
    freezeCloud.setBuffer(&freezeBuffer);
    freezeLoaded = true;
}

void Engine::processFreeze(int offset, int n) noexcept
{
    const auto o = static_cast<std::size_t>(offset);
    float* l = stripL[static_cast<std::size_t>(StripId::Freeze)].data() + o;
    float* r = stripR[static_cast<std::size_t>(StripId::Freeze)].data() + o;
    float* g = freezeGainBuf.data() + o;

    const bool wanted = params.current(P::FreezeOn) > 0.5f;
    if (wanted && ! freezeLoaded && freezeGain <= 0.0f)
        captureFreeze();
    if (! freezeLoaded)
    {
        std::fill_n(l, n, 0.0f);
        std::fill_n(r, n, 0.0f);
        std::fill_n(g, n, 0.0f);
        return;
    }

    freezeCloud.process(l, r, n, tide);
    constexpr float kFreezeMakeup = 2.5f;
    const float target = wanted ? 1.0f : 0.0f;
    const float fs = static_cast<float>(sampleRate);
    const float inStep = 1.0f / (0.4f * fs);
    const float outStep = 1.0f / (1.5f * fs);
    for (int i = 0; i < n; ++i)
    {
        freezeGain = target > freezeGain ? std::min(target, freezeGain + inStep) : std::max(target, freezeGain - outStep);
        g[i] = freezeGain;
        l[i] *= freezeGain * kFreezeMakeup;
        r[i] *= freezeGain * kFreezeMakeup;
    }
    if (! wanted && freezeGain <= 0.0f)
        freezeLoaded = false;
}

void Engine::process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const dsp::ScopedFlushDenormals noDenormals;
    const bool measure = guardEnabled.load(std::memory_order_relaxed);
    const auto startTime = measure ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {};

    drainControl();

    int done = 0;
    while (done < numSamples)
    {
        const int block = std::min(numSamples - done, maxBlock);
        for (auto* v : { &masterL, &masterR, &busAL, &busAR, &busBL, &busBR })
            std::fill_n(v->data(), block, 0.0f);
        recordStride = recordTap.beginBlock();
        if (recordStride > 2)
            for (std::size_t k = 0; k < stemL.size(); ++k)
            {
                std::fill_n(stemL[k].data(), block, 0.0f);
                std::fill_n(stemR[k].data(), block, 0.0f);
            }

        const float levelStart = faderGain(params.current(P::MasterLevel));
        const float busAStart = faderGain(params.current(P::BusALevel));
        const float busBStart = faderGain(params.current(P::BusBLevel));

        int offset = 0;
        while (offset < block)
        {
            const int tickPos = static_cast<int>(sampleTime % kControlInterval);
            if (tickPos == 0)
                controlTick();
            const int chunk = std::min(block - offset, kControlInterval - tickPos);
            processChunk(inputs, numInputs, done + offset, offset, chunk);
            offset += chunk;
            sampleTime += static_cast<std::uint64_t>(chunk);
        }

        auto runBus = [&](int firstSlot, std::vector<float>& bl, std::vector<float>& br, float startGain, P levelParam, int stem) {
            for (int slotIndex : { firstSlot, firstSlot + 1 })
            {
                auto& slot = fxSlots[static_cast<std::size_t>(slotIndex)];
                if (! slot.isActive())
                    continue;
                const float mix = params.current(offsetParam(kFxSlots[static_cast<std::size_t>(slotIndex)].firstParam, 6));
                slot.process(bl.data(), br.data(), block, mix, mix, scratchDryL.data(), scratchDryR.data(), scratchAltL.data(),
                             scratchAltR.data());
            }
            const float endGain = faderGain(params.current(levelParam));
            const float step = (endGain - startGain) / static_cast<float>(block);
            for (int i = 0; i < block; ++i)
            {
                const float g = startGain + step * static_cast<float>(i + 1);
                masterL[static_cast<std::size_t>(i)] += bl[static_cast<std::size_t>(i)] * g;
                masterR[static_cast<std::size_t>(i)] += br[static_cast<std::size_t>(i)] * g;
            }
            if (recordStride > 2)
            {
                auto& sl = stemL[static_cast<std::size_t>(stem)];
                auto& sr = stemR[static_cast<std::size_t>(stem)];
                for (int i = 0; i < block; ++i)
                {
                    const float g = startGain + step * static_cast<float>(i + 1);
                    sl[static_cast<std::size_t>(i)] = bl[static_cast<std::size_t>(i)] * g;
                    sr[static_cast<std::size_t>(i)] = br[static_cast<std::size_t>(i)] * g;
                }
            }
        };
        runBus(kBusASlot, busAL, busAR, busAStart, P::BusALevel, kNumStrips);
        runBus(kBusBSlot, busBL, busBR, busBStart, P::BusBLevel, kNumStrips + 1);
        mixPreview(block);

        for (int slotIndex : { kMasterSlot, kMasterSlot + 1 })
        {
            auto& slot = fxSlots[static_cast<std::size_t>(slotIndex)];
            if (! slot.isActive())
                continue;
            const float mix = params.current(offsetParam(kFxSlots[static_cast<std::size_t>(slotIndex)].firstParam, 6));
            slot.process(masterL.data(), masterR.data(), block, mix, mix, scratchDryL.data(), scratchDryR.data(),
                         scratchAltL.data(), scratchAltR.data());
        }
        for (int i = 0; i < block; ++i)
        {
            const auto ring = static_cast<std::size_t>((preWritten + static_cast<std::uint64_t>(i)) % preCapacity);
            preRingL[ring] = masterL[static_cast<std::size_t>(i)];
            preRingR[ring] = masterR[static_cast<std::size_t>(i)];
            mixEnergy += 0.5 * (static_cast<double>(masterL[static_cast<std::size_t>(i)]) * masterL[static_cast<std::size_t>(i)]
                                + static_cast<double>(masterR[static_cast<std::size_t>(i)]) * masterR[static_cast<std::size_t>(i)]);
        }
        mixEnergyCount += block;
        preWritten += static_cast<std::uint64_t>(block);
        medium.process(masterL.data(), masterR.data(), block);
        autoMaster.process(masterL.data(), masterR.data(), block);

        const float levelEnd = faderGain(params.current(P::MasterLevel));
        const auto ev = master.process(masterL.data(), masterR.data(), block, levelStart, levelEnd);

        if (ev.guardTripped)
        {
            resetFeedback();
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

        writeCatch(masterL.data(), masterR.data(), block);
        pushRecording(block);
        accumulateTelemetry(masterL.data(), masterR.data(), block);
        done += block;
    }

    if (measure)
    {
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - startTime;
        updateGuardrails(elapsed.count(), numSamples);
    }
    else if (guard.getLevel() != 0)
    {
        guard.reset();
        applyGuardLimits(guard.getLimits());
    }
}

void Engine::pushRecording(int numSamples) noexcept
{
    if (recordStride == 0)
        return;
    std::array<const float*, RecordTap::kMaxChannels> ch {};
    ch[0] = masterL.data();
    ch[1] = masterR.data();
    for (std::size_t k = 0; k < stemL.size(); ++k)
    {
        ch[2 + 2 * k] = stemL[k].data();
        ch[3 + 2 * k] = stemR[k].data();
    }
    recordTap.push(ch.data(), numSamples);
}

void Engine::updateGuardrails(double elapsedSeconds, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    const double budget = static_cast<double>(numSamples) / sampleRate;
    const float forced = forcedLoad.load(std::memory_order_relaxed);
    const float load = forced >= 0.0f ? forced : static_cast<float>(elapsedSeconds / budget);
    if (guard.update(load, static_cast<float>(budget)))
        applyGuardLimits(guard.getLimits());
}

void Engine::applyGuardLimits(const GuardLimits& limits) noexcept
{
    for (auto& slot : clouds)
        slot.cloud.setGrainLimit(limits.cloudGrains);
    resonator.setModeLimit(limits.resonatorModes);
    bloom.setVoiceLimit(limits.bloomVoices);
    droneVoiceCap = limits.droneVoices;
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
    for (int s = 0; s < kNumStrips; ++s)
    {
        const auto [pl, pr] = strips[static_cast<std::size_t>(s)].takePeak();
        accStripL[static_cast<std::size_t>(s)] = std::max(accStripL[static_cast<std::size_t>(s)], pl);
        accStripR[static_cast<std::size_t>(s)] = std::max(accStripR[static_cast<std::size_t>(s)], pr);
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
    f.bpm = bpm;
    f.gestureState = gestureState;
    f.gestureSeconds = gestureState == GestureState::Idle ? 0.0f : static_cast<float>(static_cast<double>(sampleTime - gestureStart) / sampleRate);
    if (const auto* take = gestureChannel.current(); take != nullptr && take->sampleRate > 0.0)
        f.gestureLength = static_cast<float>(static_cast<double>(take->length) / take->sampleRate);
    else
        f.gestureLength = 0.0f;
    f.beatPhase = static_cast<float>(beatPos - std::floor(beatPos));
    f.syncOn = syncOn;
    f.hostTempo = hostBpm > 0.0;
    f.panicActive = master.isPanicActive();
    f.guardTrips = master.getGuardTrips();
    f.dspLoad = guardEnabled.load(std::memory_order_relaxed) ? guard.getSmoothedLoad() : 0.0f;
    f.guardLevel = guard.getLevel();

    f.tide = tide;
    f.harmonyRoot = harmony.getTarget().root;
    f.harmonyScale = std::clamp(toInt(params.current(P::HarmonyScale)), 0, static_cast<int>(dsp::kScaleTypes.size()) - 1);
    f.harmonyMorph = harmony.getProgress();
    f.mediumType = static_cast<int>(medium.getType());
    f.stripPeakL = accStripL;
    f.stripPeakR = accStripR;

    for (int v = 0; v < dsp::DroneGenerator::kMaxVoices; ++v)
    {
        const auto uv = static_cast<std::size_t>(v);
        f.droneVoiceLevel[uv] = drone.getVoiceLevel(v);
        f.droneVoiceInterval[uv] = drone.getVoiceInterval(v);
        f.droneVoiceNote[uv] = drone.getVoiceNote(v);
    }
    for (int k = 0; k < kNumClouds; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        const auto& slot = clouds[uk];
        f.cloudLoaded[uk] = rawBuffer(slot.buffers.current()) != nullptr;
        f.cloudGrainCount[uk] = slot.cloud.getActiveGrains();
        f.cloudGrainViews[uk] = slot.cloud.getGrainViews(f.cloudGrains[uk].data());
    }
    for (int m = 0; m < dsp::ResonatorBank::kMaxModes; ++m)
    {
        f.modeLevel[static_cast<std::size_t>(m)] = resonator.getModeLevel(m);
        f.modeNote[static_cast<std::size_t>(m)] = resonator.getModeNote(m);
    }
    f.bloomLoaded = rawBuffer(bloomBuffers.current()) != nullptr;
    for (int v = 0; v < dsp::BloomSampler::kMaxVoices; ++v)
        f.bloomVoices[static_cast<std::size_t>(v)] = bloom.getVoice(v);
    f.inputLevel = liveInput.getLevel();
    f.inputGateOpen = liveInput.isGateOpen();
    f.inputFreeze = inputFreeze.getGain();
    f.loopState = static_cast<int>(looper.getState());
    f.loopPosition = looper.getPosition();
    f.loopSeconds = looper.getLengthSeconds();
    f.loopPasses = looper.getPasses();
    f.weatherGust = weather.getGust();
    f.weatherWave = weather.getWave();
    f.freezeGain = freezeGain;
    f.swell = swellEnv;
    f.hush = hushEnv;
    f.slow = slowEnv;
    {
        const auto& a = autoMaster.getState();
        f.autoMaster = { a.loudness, a.gainDb, a.lowDb, a.mudDb, a.highDb, a.width, a.reductionDb, a.mix };
    }
    std::copy(seasonValue.begin(), seasonValue.end(), f.seasonValue.begin());
    std::copy(loopPhase.begin(), loopPhase.end(), f.loopPhase.begin());
    std::copy(loopNote.begin(), loopNote.end(), f.loopNote.begin());
    std::copy(loopFlash.begin(), loopFlash.end(), f.loopFlash.begin());

    f.cursor = cursor;
    f.position = position;
    if (const auto* set = sceneChannel.current())
    {
        f.numScenes = set->numScenes;
        f.sceneSetVersion = set->version;
        std::copy_n(weights.begin(), set->numScenes, f.sceneWeights.begin());
    }
    for (std::size_t i = 0; i < kNumParams; ++i)
    {
        f.paramTargets[i] = params.target(static_cast<ParamIndex>(i));
        f.paramMod[i] = params.modulation(static_cast<ParamIndex>(i));
        f.live[i] = live[i];
        f.midiPickup[i] = midiPickup[i];
    }
    f.sustainPedal = sustainPedal;
    f.modValue = modValue;

    telemetryQueue.push(f);

    accPeakL = accPeakR = 0.0f;
    accSumL = accSumR = 0.0;
    accCount = 0;
    accStripL.fill(0.0f);
    accStripR.fill(0.0f);
    telemetryCountdown += telemetryInterval;
}
}
