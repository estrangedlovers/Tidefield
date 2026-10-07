#include "Engine.h"

#include "scene/TerrainMath.h"

#include <dsp/core/Denormal.h>
#include <dsp/core/MathUtil.h>

#include <algorithm>
#include <cmath>

namespace tf::engine {

namespace {

// Offsets inside a TF_CLOUD block (see ParamDefs.h).
enum CloudOffset : int { kDensity, kGrainMs, kPosition, kSpray, kScan, kPitch, kPitchSpread, kHarmonize, kReverse, kShape, kStereo, kGravity };

P offsetParam(P first, int offset) noexcept { return static_cast<P>(idx(first) + offset); }

int toInt(float v) noexcept { return static_cast<int>(std::lround(v)); }

dsp::Scale scaleFrom(float root, float scaleIndex) noexcept
{
    const int s = std::clamp(toInt(scaleIndex), 0, static_cast<int>(dsp::kScaleTypes.size()) - 1);
    return { dsp::kScaleTypes[static_cast<std::size_t>(s)].mask, std::clamp(toInt(root), 0, 11) };
}

} // namespace

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
    medium.prepare(spec, config.seed + 300u);
    master.prepare(spec);
    for (auto& slot : fxSlots)
        slot.prepareAll(spec);

    const double terrainRate = sampleRate / (kControlInterval * kTerrainDecimation);
    cursorX.prepare(terrainRate, 1.0f, false);
    cursorY.prepare(terrainRate, 1.0f, false);
    cursorX.reset(params.current(P::TerrainX));
    cursorY.reset(params.current(P::TerrainY));
    wander.setSeed(config.seed ^ 0x77616e64ull);

    const auto n = static_cast<std::size_t>(maxBlock);
    for (int s = 0; s < kNumStrips; ++s)
    {
        stripL[static_cast<std::size_t>(s)].assign(n, 0.0f);
        stripR[static_cast<std::size_t>(s)].assign(n, 0.0f);
    }
    for (auto* v : { &busAL, &busAR, &busBL, &busBR, &masterL, &masterR, &inputMono, &excite, &scratchDryL, &scratchDryR,
                     &scratchAltL, &scratchAltR })
        v->assign(n, 0.0f);

    catchCapacity = static_cast<std::size_t>(kCatchRingSeconds * sampleRate);
    catchL.assign(catchCapacity, 0.0f);
    catchR.assign(catchCapacity, 0.0f);
    catchIn.assign(catchCapacity, 0.0f);
    catchWritten.store(0);
    inputWritten.store(0);

    telemetryInterval = std::max(1, static_cast<int>(sampleRate / config.telemetryRateHz));
    telemetryCountdown = telemetryInterval;

    // Prime the first tick so ramps start from settled values rather than zero.
    controlTick();
    for (auto& strip : strips)
        strip.settle();
}

void Engine::release()
{
    for (int s = 0; s < kNumStrips; ++s)
    {
        stripL[static_cast<std::size_t>(s)].clear();
        stripR[static_cast<std::size_t>(s)].clear();
    }
    for (auto* v : { &busAL, &busAR, &busBL, &busBR, &masterL, &masterR, &inputMono, &excite, &scratchDryL, &scratchDryR,
                     &scratchAltL, &scratchAltR })
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
    // The writer must not have lapped the region's start.
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
    // Re-check: if the writer reached the region while copying, the copy is torn.
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
    // Settings, not sound: read targets so values posted with the command apply to it.
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
    midiMapChannel.collectGarbage();
    bloomBuffers.collectGarbage();
    for (auto& c : clouds)
        c.buffers.collectGarbage();
}

void Engine::notify(EngineNotice::Type type) noexcept
{
    noticeQueue.push(EngineNotice { type, sampleTime }); // dropped if the UI is not reading
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
    if (midiMapChannel.acquire())
    {
        // A new map: every binding must pick up again.
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
        case MidiAction::CaptureScene: notify(EngineNotice::Type::CaptureSceneRequest); break; // needs the message thread
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

    // Controller position -> normalised parameter position, through range and curve.
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
        // Something else (terrain, UI, a scene) moved the parameter: pick up again.
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

void Engine::handleMidi(const RawMidi& m) noexcept
{
    midiMonitor.push(m); // for learn and activity; dropped if the UI is behind

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
                return; // a pad bound to an action does not also play Bloom
        }
        if (m.isCc())
        {
            if (m.data1 == 64) // sustain pedal
            {
                sustainPedal = m.data2 >= 64;
                bloom.setSustain(sustainPedal);
            }
            return;
        }
    }

    const bool channelOk = map == nullptr || map->noteChannel < 0 || map->noteChannel == ch;
    if (! channelOk)
        return;
    if (m.isNoteOn())
    {
        bloom.noteOn(m.data1, static_cast<float>(m.data2) / 127.0f);
        if (map != nullptr && map->notesToDrone)
        {
            // Fold into the drone's range by octaves.
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

void Engine::applyEvent(const ControlEvent& e) noexcept
{
    switch (e.type)
    {
        case ControlEvent::Type::SetParam:
            if (e.param >= registry.size())
                return;
            // A performer touching a terrain-driven parameter takes it over.
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
            applyCommand(e.command);
            break;

        case ControlEvent::Type::Note:
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
        case Command::ReleaseLiveLayer: std::fill(live.begin(), live.end(), std::uint8_t { 0 }); break;
        case Command::Catch: requestCatch(); break;
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
    for (auto& slot : fxSlots)
        slot.reset();
    medium.reset();
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
    const auto style = static_cast<Wander::Style>(std::clamp(toInt(params.current(P::TerrainWanderStyle)), 0, 2));
    position = wander.update(cursor, params.current(P::TerrainWander), params.current(P::TerrainWanderRate), style, set, dt * tide);

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
    d.density = params.current(P::DroneDensity);
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
        c.rootNote = params.current(P::DroneRoot) + 12.0f; // samples are assumed to sit near the drone's key
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
    (void) t; // sources receive Tide in process(); kept for symmetry with updateFx
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
    const dsp::ModContext ctx { t };
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
}

void Engine::controlTick() noexcept
{
    const float tickSeconds = static_cast<float>(kControlInterval / sampleRate);
    if (tickCount++ % kTerrainDecimation == 0)
        updateTerrain(tickSeconds * kTerrainDecimation);

    params.advance(kControlInterval);
    tide = params.current(P::TideRate);

    harmony.setTarget(scaleFrom(params.current(P::HarmonyRoot), params.current(P::HarmonyScale)));
    harmony.setMorphSeconds(params.current(P::HarmonyMorph));
    harmony.advance(tickSeconds * tide);

    updateSources(tide);

    for (int s = 0; s < kNumStrips; ++s)
    {
        const auto& info = kStrips[static_cast<std::size_t>(s)];
        ChannelStrip::Settings st;
        st.levelDb = params.current(info.level);
        st.pan = params.current(info.pan);
        st.width = params.current(info.width);
        st.sendADb = params.current(info.sendA);
        st.sendBDb = params.current(info.sendB);
        st.gate = s == static_cast<int>(StripId::Input) ? params.current(P::InputArmed) : 1.0f;
        strips[static_cast<std::size_t>(s)].update(st);
    }

    updateFx(tide);
    master.setFadeSeconds(params.current(P::MasterFadeSecs));
    master.setCeilingDb(params.current(P::MasterCeiling));
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

    // 1. Live input (mono).
    float* in = inputMono.data() + o;
    liveInput.process(inputs, numInputs, inputOffset, in, n);
    {
        auto pos = inputWritten.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++pos)
            catchIn[static_cast<std::size_t>(pos % catchCapacity)] = in[i];
        inputWritten.store(pos, std::memory_order_release);
    }

    // 2. Drone.
    drone.process(L(StripId::Drone), R(StripId::Drone), n, tide);

    // 3. Clouds, with fade-out / swap / fade-in when a new sample arrives.
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
                slot.buffers.acquire(); // retires the old buffer; grains stop in setBuffer
                slot.cloud.setBuffer(rawBuffer(slot.buffers.current()));
                slot.swap = CloudSlot::Swap::FadingIn;
            }
            else if (slot.swap == CloudSlot::Swap::FadingIn && slot.swapGain >= 1.0f)
            {
                slot.swap = CloudSlot::Swap::Idle;
            }
        }
    }

    // 4. Bloom. A new one-shot waits until sounding voices have released (20 ms).
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

    // 5. Resonator, excited by its own rain plus input, drone, clouds and Bloom.
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
    resonator.process(ex, L(StripId::Resonator), R(StripId::Resonator), n, tide);

    // 6. The input strip carries the conditioned input.
    std::copy_n(in, n, L(StripId::Input));
    std::copy_n(in, n, R(StripId::Input));

    // 7. Inserts and strips.
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
        strips[static_cast<std::size_t>(s)].processAdd(L(sid), R(sid), masterL.data() + o, masterR.data() + o, busAL.data() + o,
                                                       busAR.data() + o, busBL.data() + o, busBR.data() + o, n, tickPos,
                                                       kControlInterval);
    }
}

void Engine::process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const dsp::ScopedFlushDenormals noDenormals;

    drainControl();

    int done = 0;
    while (done < numSamples)
    {
        const int block = std::min(numSamples - done, maxBlock);
        for (auto* v : { &masterL, &masterR, &busAL, &busAR, &busBL, &busBR })
            std::fill_n(v->data(), block, 0.0f);

        const float levelStart = dsp::dbToGain(params.current(P::MasterLevel));
        const float busAStart = dsp::dbToGain(params.current(P::BusALevel));
        const float busBStart = dsp::dbToGain(params.current(P::BusBLevel));

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

        // Send buses, returned to master with a ramped level.
        auto runBus = [&](int firstSlot, std::vector<float>& bl, std::vector<float>& br, float startGain, P levelParam) {
            for (int slotIndex : { firstSlot, firstSlot + 1 })
            {
                auto& slot = fxSlots[static_cast<std::size_t>(slotIndex)];
                if (! slot.isActive())
                    continue;
                const float mix = params.current(offsetParam(kFxSlots[static_cast<std::size_t>(slotIndex)].firstParam, 6));
                slot.process(bl.data(), br.data(), block, mix, mix, scratchDryL.data(), scratchDryR.data(), scratchAltL.data(),
                             scratchAltR.data());
            }
            const float endGain = dsp::dbToGain(params.current(levelParam));
            const float step = (endGain - startGain) / static_cast<float>(block);
            for (int i = 0; i < block; ++i)
            {
                const float g = startGain + step * static_cast<float>(i + 1);
                masterL[static_cast<std::size_t>(i)] += bl[static_cast<std::size_t>(i)] * g;
                masterR[static_cast<std::size_t>(i)] += br[static_cast<std::size_t>(i)] * g;
            }
        };
        runBus(kBusASlot, busAL, busAR, busAStart, P::BusALevel);
        runBus(kBusBSlot, busBL, busBR, busBStart, P::BusBLevel);

        // Master inserts, then the Medium, then the safety chain.
        for (int slotIndex : { kMasterSlot, kMasterSlot + 1 })
        {
            auto& slot = fxSlots[static_cast<std::size_t>(slotIndex)];
            if (! slot.isActive())
                continue;
            const float mix = params.current(offsetParam(kFxSlots[static_cast<std::size_t>(slotIndex)].firstParam, 6));
            slot.process(masterL.data(), masterR.data(), block, mix, mix, scratchDryL.data(), scratchDryR.data(),
                         scratchAltL.data(), scratchAltR.data());
        }
        medium.process(masterL.data(), masterR.data(), block);

        const float levelEnd = dsp::dbToGain(params.current(P::MasterLevel));
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

        writeCatch(masterL.data(), masterR.data(), block);
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
    f.panicActive = master.isPanicActive();
    f.guardTrips = master.getGuardTrips();

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
        f.live[i] = live[i];
        f.midiPickup[i] = midiPickup[i];
    }
    f.sustainPedal = sustainPedal;

    telemetryQueue.push(f); // dropped if the UI is behind; the next frame supersedes it

    accPeakL = accPeakR = 0.0f;
    accSumL = accSumR = 0.0;
    accCount = 0;
    accStripL.fill(0.0f);
    accStripR.fill(0.0f);
    telemetryCountdown += telemetryInterval;
}

} // namespace tf::engine
