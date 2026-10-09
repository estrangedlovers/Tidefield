#include "AppCore.h"

#include "LinkSync.h"
#include "FactoryContent.h"

#include <BinaryData.h>
#include <AudioProcessorEffect.h>
#include <io/AudioFileIO.h>
#include <io/Session.h>

namespace tf::app {
AppCore::AppCore(Host& h)
    : host(h), engine(h.getEngine()), scenes(h.getEngine()), fx(h.getEngine()), guest(h.getEngine()), catcher(h.getEngine()), midi(h.getEngine()), seasons(h.getEngine()), paths(h.getEngine()), gestures(h.getEngine()), mod(h.getEngine()),
      controllers(h.getSettings().getFile().getSiblingFile("Controller templates"), h.getEngine().getRegistry()),
      session(h.getEngine(), scenes, fx, &midi, &seasons, &paths, &gestures, &mod, &guest), recorder(h.getEngine().getRecordTap())
{
    engine.setGuardrailsEnabled(true);
    session.setWorkers(&workers);
    const auto& registry = engine.getRegistry();
    for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
        lastFrame.paramTargets[i] = registry.spec(i).defaultValue;
    session.onApplied = [this](const io::SessionData& s) {
        seedTargets(s);
        if (cycleOut != nullptr)
            cycleOut->allNotesOff();
        if (auto p = io::performanceFromSession(s, engine.getRegistry()))
            performance.load(std::move(*p));
        else
            performance.load({});
    };
    keys.load(h.getSettings());
    if (h.getDeviceManager() != nullptr)
    {
        midiInputs = std::make_unique<MidiInputs>(engine, h.getSettings());
        midiInputs->onDevicesChanged = [this] { suggestControllerTemplates(); };
        juce::Timer::callAfterDelay(1500, [this, weak = std::weak_ptr<bool>(alive)] {
            if (! weak.expired())
                suggestControllerTemplates();
        });
    }
    fxjuce::registerUserEffects();
    fx.loadDefaultLayout();
    loadRigMidi();
    addFactoryPresets(presets);
    loadFactoryContent();

    catcher.onCaught = [this](int cloud, const std::string& name) {
        status("Caught into Cloud " + juce::String(cloud + 1) + " (" + juce::String(name) + ")");
    };
    gestures.onTakeFinished = [this] {
        const auto& t = gestures.getTake();
        status("Take recorded: " + juce::String(static_cast<int>(t.events.size())) + " moves over "
               + juce::String(static_cast<double>(t.length) / t.sampleRate, 1) + " s. Press G (or the pad) to play it.");
    };
    catcher.onRejected = [this](const std::string& reason) { status(reason, true); };
    midi.onLearned = [this](const std::string& d) { status("MIDI learned: " + juce::String(d)); };
    session.onStatus = [this](const juce::String& m) { status(m); };
    recorder.onFinished = [this, weak = std::weak_ptr<bool>(alive)](const juce::File& folder, bool ok) {
        juce::MessageManager::callAsync([this, weak, folder, ok] {
            if (weak.expired())
                return;
            if (ok)
                status("Recording saved: " + folder.getFileName());
            else
                status("Recording stopped: the disk could not keep up or is full. Saved what was written to "
                           + folder.getFileName(),
                       true);
        });
    };
    session.onSessionChanged = [this] {
        undo.clearUndoHistory();
        if (session.getFile() != juce::File())
            rememberSession(session.getFile());
        if (onSessionChanged)
            onSessionChanged();
    };
    session.askBeforeDiscard = ! host.isPlugin();
    if (! host.isPlugin())
    {
        plugins = std::make_unique<PluginHost>(host.getSettings(), host.getSettings().getFile().getSiblingFile("PluginScanCrashes.txt"));
        plugins->onStatus = [this](const juce::String& m, bool warning) { status(m, warning); };
        fx.setExternal(plugins.get());
        guest.setExternal(plugins.get());
        session.pluginEdits = [this](int slot) { return plugins->editRevision(slot); };
        clockOut = std::make_unique<MidiClockOut>(host.getSettings());
        cycleOut = std::make_unique<CycleMidiOut>(engine, host.getSettings(), host.getBlockClock());
        if (auto* link = host.getLink(); link != nullptr && LinkSync::isAvailable())
            link->setEnabled(host.getSettings().getBoolValue("link", false));
        osc = std::make_unique<OscRemote>(*this, host.getSettings());
        osc->onScene = [this](int index, bool) {
            const auto& list = scenes.getScenes();
            if (index < 0 || index >= static_cast<int>(list.size()))
                return;
            engine.setParam(engine::P::TerrainX, list[static_cast<std::size_t>(index)].position.x);
            engine.setParam(engine::P::TerrainY, list[static_cast<std::size_t>(index)].position.y);
        };
        recovery = std::make_unique<Recovery>(*this);
        installation = std::make_unique<Installation>(*this);
        installation->launch();
        if (! installation->isEnabled() && getOpenLastSession())
            if (const auto recent = recentSessions(); ! recent.isEmpty() && juce::File(recent[0]).existsAsFile())
                session.openFile(juce::File(recent[0]));
    }
    startTimerHz(30);
}

AppCore::~AppCore()
{
    stopTimer();
    performance.cancelRender();
    installation.reset();
    recovery.reset();
    osc.reset();
    clockOut.reset();
    cycleOut.reset();
    fx.setExternal(nullptr);
    guest.setExternal(nullptr);
    session.pluginEdits = nullptr;
    recorder.onFinished = nullptr;
    saveRigMidi();
}

juce::File AppCore::getRecordingsFolder() const
{
    const auto stored = host.getSettings().getValue("recordingsFolder");
    if (stored.isNotEmpty() && juce::File::isAbsolutePath(stored))
        return juce::File(stored);
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Tidefield");
}

void AppCore::setRecordingsFolder(const juce::File& folder)
{
    host.getSettings().setValue("recordingsFolder", folder.getFullPathName());
    host.getSettings().saveIfNeeded();
}

juce::StringArray AppCore::recentSessions() const
{
    auto list = juce::StringArray::fromLines(host.getSettings().getValue("recentSessions"));
    list.removeEmptyStrings();
    return list;
}

void AppCore::rememberSession(const juce::File& file)
{
    auto list = recentSessions();
    list.removeString(file.getFullPathName());
    list.insert(0, file.getFullPathName());
    while (list.size() > 12)
        list.remove(list.size() - 1);
    host.getSettings().setValue("recentSessions", list.joinIntoString("\n"));
    host.getSettings().saveIfNeeded();
}

void AppCore::clearRecentSessions()
{
    host.getSettings().setValue("recentSessions", juce::String());
    host.getSettings().saveIfNeeded();
}

bool AppCore::getOpenLastSession() const { return host.getSettings().getBoolValue("openLastSession", false); }

void AppCore::setOpenLastSession(bool open)
{
    host.getSettings().setValue("openLastSession", open);
    host.getSettings().saveIfNeeded();
}

double AppCore::getRenderSampleRate() const
{
    const double rate = host.getSettings().getDoubleValue("renderSampleRate", 48000.0);
    return rate == 44100.0 || rate == 48000.0 || rate == 96000.0 ? rate : 48000.0;
}

void AppCore::setRenderSampleRate(double rate)
{
    host.getSettings().setValue("renderSampleRate", rate);
    host.getSettings().saveIfNeeded();
}

bool AppCore::getRecordStems() const { return host.getSettings().getBoolValue("recordStems", false); }

void AppCore::setRecordStems(bool stems)
{
    host.getSettings().setValue("recordStems", stems);
    host.getSettings().saveIfNeeded();
}

void AppCore::startRecording()
{
    if (recorder.isActive())
        return;
    if (! host.isRunning())
    {
        status("No audio device is running, so there is nothing to record.", true);
        return;
    }
    const auto parent = getRecordingsFolder();
    if (! parent.createDirectory())
    {
        status("Could not create the recordings folder " + parent.getFullPathName(), true);
        return;
    }
    const auto take = io::Recorder::makeFolder(parent, session.getName());
    recordingRate = engine.getSampleRate();
    const auto result = recorder.start(take, recordingRate, getRecordStems());
    if (result.failed())
        status("Could not start recording: " + result.getErrorMessage(), true);
    else
        status(getRecordStems() ? "Recording master and stems" : "Recording");
}

void AppCore::stopRecording() { recorder.stop(); }

void AppCore::toggleRecording()
{
    if (recorder.getStatus().state == io::Recorder::State::Recording)
        stopRecording();
    else
        startRecording();
}

void AppCore::status(const juce::String& message, bool warning)
{
    if (onStatus)
        onStatus(message, warning);
}

void AppCore::captureSceneAtCursor()
{
    int captured = -1;
    editScenes("Capture scene", [&] { captured = scenes.captureScene({}, lastFrame.cursor, lastFrame); });
    if (captured < 0)
        status("The terrain is full (32 scenes). Remove one to capture another.", true);
}

void AppCore::loadFactoryContent()
{
    session.makeNewSession = [this] { return makeStarterSession(engine); };
    session.newSession();
}

void AppCore::seedTargets(const io::SessionData& s)
{
    const auto& registry = engine.getRegistry();
    for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
    {
        const auto it = s.params.find(registry.spec(i).id);
        lastFrame.paramTargets[i] = it != s.params.end() ? registry.spec(i).clamp(it->second) : registry.spec(i).defaultValue;
    }
    lastFrame.live.fill(0);
}

void AppCore::loadRigMidi()
{
    const auto stored = host.getSettings().getValue("midiMapping");
    if (stored.isNotEmpty())
        io::applyMidiJson(juce::JSON::parse(stored), midi, engine.getRegistry());
    else
        midi.loadDefaultLayout();
}

void AppCore::saveRigMidi()
{
    host.getSettings().setValue("midiMapping", juce::JSON::toString(io::midiToJson(midi, engine.getRegistry()), true));
    host.getSettings().saveIfNeeded();
}

namespace {
struct SceneState
{
    std::vector<engine::Scene> scenes;
    std::vector<engine::ParamIndex> pins;
};

bool sameScenes(const std::vector<engine::Scene>& a, const std::vector<engine::Scene>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].position.x != b[i].position.x || a[i].position.y != b[i].position.y || a[i].values != b[i].values)
            return false;
    return true;
}
}

void AppCore::recordParamChange(engine::ParamIndex param, float from, float to, bool continuing)
{
    if (undoing || from == to || param >= engine::kNumParams)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (! (continuing || (param == lastUndoParam && now - lastUndoTime < 700.0)))
        undo.beginNewTransaction(juce::String(engine.getRegistry().spec(param).name));
    lastUndoParam = param;
    lastUndoTime = now;
    undo.perform(new ParamAction(static_cast<int>(param), from, to, [this](int p, float v) {
        engine.post(engine::ControlEvent::setParam(static_cast<engine::ParamIndex>(p), v));
    }));
}

void AppCore::editScenes(const juce::String& name, const std::function<void()>& change)
{
    SceneState before { scenes.getScenes(), scenes.getPins() };
    change();
    SceneState after { scenes.getScenes(), scenes.getPins() };
    if (sameScenes(before.scenes, after.scenes) && before.pins == after.pins)
        return;
    undo.beginNewTransaction(name);
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<SceneState>(std::move(before), std::move(after), [this](const SceneState& s) { scenes.replaceAll(s.scenes, s.pins); }));
}

void AppCore::editRoutes(const juce::String& name, const std::function<void()>& change)
{
    auto before = mod.getRoutes();
    change();
    auto after = mod.getRoutes();
    undo.beginNewTransaction(name);
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<std::vector<engine::ModRouteManager::Route>>(std::move(before), std::move(after),
                                                                                [this](const auto& r) { mod.replaceAll(r); }));
}

void AppCore::editMacros(const juce::String& name, const std::function<void()>& change)
{
    auto before = mod.getMacros();
    change();
    auto after = mod.getMacros();
    undo.beginNewTransaction(name);
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<std::array<engine::ModRouteManager::Macro, engine::kNumMacros>>(
        std::move(before), std::move(after), [this](const auto& m) { mod.replaceMacros(m); }));
}

void AppCore::editSeasons(const juce::String& name, const std::function<void()>& change)
{
    auto before = seasons.getSeasons();
    change();
    auto after = seasons.getSeasons();
    undo.beginNewTransaction(name);
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<std::vector<engine::Season>>(std::move(before), std::move(after), [this](const auto& s) { seasons.replaceAll(s); }));
}

void AppCore::editMidi(const juce::String& name, const std::function<void()>& change)
{
    auto before = midi.getBindings();
    change();
    auto after = midi.getBindings();
    undo.beginNewTransaction(name);
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<std::vector<engine::MidiBinding>>(std::move(before), std::move(after), [this](const auto& b) {
        midi.setBindings(b);
        saveRigMidi();
    }));
    saveRigMidi();
}

void AppCore::applyControllerTemplate(const ControllerTemplate& t, bool replace)
{
    const auto& registry = engine.getRegistry();
    std::vector<std::string> warnings;
    editMidi("Controller template", [&] {
        warnings = io::applyMidiJson(mergeMidiJson(io::midiToJson(midi, registry), t.midi, replace), midi, registry);
    });
    if (! warnings.empty())
        return status("Applied " + t.name + ", skipping " + juce::String(static_cast<int>(warnings.size())) + " mappings to controls this version does not have", true);
    const auto undoKey = keys.get(KeyAction::Undo);
    status((replace ? "Your mappings are now " : "Added ") + t.name + (replace ? juce::String() : juce::String(" to your mappings")) + " ("
           + juce::String(countBindings(t.midi)) + " controls)." + (undoKey.isValid() ? " Undo with " + KeyBindings::describe(undoKey, false) + "." : juce::String()));
}

void AppCore::suggestControllerTemplates()
{
    if (midiInputs == nullptr)
        return;
    juce::StringArray now;
    for (const auto& d : midiInputs->getDevices())
        if (d.enabled && d.open)
            now.add(d.info.name);
    for (const auto& name : now)
    {
        if (connectedInputs.contains(name))
            continue;
        if (const auto t = controllers.suggestFor(name); t && ! midiContains(io::midiToJson(midi, engine.getRegistry()), t->midi))
        {
            status(name + " is connected. Settings > Controllers has a template for it: " + t->name + ".");
            break;
        }
    }
    connectedInputs = now;
}

void AppCore::setKey(KeyAction action, const juce::KeyPress& key)
{
    keys.set(action, key);
    keys.save(host.getSettings());
    if (onKeysChanged)
        onKeysChanged();
}

void AppCore::resetKeys()
{
    keys.resetAll();
    keys.save(host.getSettings());
    if (onKeysChanged)
        onKeysChanged();
}

void AppCore::setEffect(int slot, const std::string& type)
{
    struct FxState
    {
        std::string type, state;
    };
    FxState before { fx.getType(slot), fx.getState(slot) };
    fx.setType(slot, type);
    undo.beginNewTransaction("Effect");
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<FxState>(std::move(before), FxState { type, {} }, [this, slot](const FxState& s) {
        fx.setType(slot, s.type, false, s.state);
    }));
}

void AppCore::setInstrument(const std::string& type)
{
    struct GuestState
    {
        std::string type, state, name;
    };
    GuestState before { guest.getType(), guest.getState(), guest.getName() };
    guest.setType(type);
    undo.beginNewTransaction("Instrument");
    lastUndoParam = engine::kNumParams;
    undo.perform(new SnapshotAction<GuestState>(std::move(before), GuestState { type, {}, {} }, [this](const GuestState& s) {
        guest.setType(s.type, false, s.state, s.name);
    }));
}

void AppCore::timerCallback()
{
    session.tick();
    scenes.tick();
    fx.tick();
    guest.tick();
    midi.tick();
    seasons.tick();
    paths.tick();
    gestures.tick();
    mod.tick();
    engine.collectGarbage();

    engine::RawMidi monitored;
    while (engine.popMidiMonitor(monitored))
    {
        midi.handleMonitor(monitored);
        if (onMidiActivity)
            onMidiActivity(monitored);
    }

    engine::TelemetryFrame f;
    bool got = false;
    while (engine.popTelemetry(f))
        got = true;
    if (got)
    {
        lastFrame = f;
        session.setLatest(lastFrame);
        if (clockOut != nullptr)
            clockOut->update(lastFrame.bpm, lastFrame.syncOn);
        if (osc != nullptr)
            osc->sendFrame(lastFrame);
        const float tempoTarget = lastFrame.paramTargets[engine::idx(engine::P::SyncBpm)];
        if (auto* link = host.getLink(); link != nullptr && link->isEnabled() && lastTempoTarget >= 0.0f && std::abs(tempoTarget - lastTempoTarget) > 0.01f)
            link->setTempo(tempoTarget);
        lastTempoTarget = tempoTarget;
        if (lastFrame.guardLevel != lastGuardLevel)
        {
            if (lastFrame.guardLevel > lastGuardLevel)
                status("CPU is running hot: lightening grains and voices (level " + juce::String(lastFrame.guardLevel) + " of "
                           + juce::String(engine::kMaxGuardLevel) + ")",
                       true);
            else if (lastFrame.guardLevel == 0)
                status("CPU has headroom again: full quality restored");
            lastGuardLevel = lastFrame.guardLevel;
        }
        if (onTelemetry)
            onTelemetry(lastFrame);
    }
    performance.tick();

    if (recorder.getStatus().state == io::Recorder::State::Recording && engine.getSampleRate() != recordingRate)
    {
        recorder.stop();
        status("The sample rate changed, so the recording was stopped and saved.", true);
    }

    engine::EngineNotice notice;
    while (engine.popNotice(notice))
    {
        if (notice.type == engine::EngineNotice::Type::GuardTripped)
            status("Safety guard tripped: a non-finite sample was caught and the engine reset.", true);
        if (notice.type == engine::EngineNotice::Type::CaptureSceneRequest)
            captureSceneAtCursor();
        if (notice.type == engine::EngineNotice::Type::RecordToggleRequest)
            toggleRecording();
        catcher.handle(notice);
        session.handleNotice(notice);
    }
}
}
