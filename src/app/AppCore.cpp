#include "AppCore.h"
#include "FactoryContent.h"

#include <BinaryData.h>
#include <AudioProcessorEffect.h>
#include <io/AudioFileIO.h>
#include <io/Session.h>

namespace tf::app {

AppCore::AppCore(Host& h)
    : host(h), engine(h.getEngine()), scenes(h.getEngine()), fx(h.getEngine()), catcher(h.getEngine()), midi(h.getEngine()), seasons(h.getEngine()), paths(h.getEngine()), gestures(h.getEngine()),
      session(h.getEngine(), scenes, fx, &midi, &seasons, &paths, &gestures), recorder(h.getEngine().getRecordTap())
{
    engine.setGuardrailsEnabled(true);
    session.setWorkers(&workers);
    const auto& registry = engine.getRegistry();
    for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
        lastFrame.paramTargets[i] = registry.spec(i).defaultValue;
    session.onApplied = [this](const io::SessionData& s) { seedTargets(s); };
    if (h.getDeviceManager() != nullptr)
        midiInputs = std::make_unique<MidiInputs>(engine, h.getSettings());
    fxjuce::registerUserEffects(); // before any slot or session asks for an FX type
    fx.loadDefaultLayout();
    loadRigMidi();
    addFactoryPresets(presets);
    loadFactoryContent();

    catcher.onCaught = [this](int cloud, const std::string& name) {
        status("Caught into Cloud " + juce::String(cloud + 1) + " (" + juce::String(name) + ")");
    };
    gestures.onTakeFinished = [this] {
        const auto& t = gestures.getTake();
        status("Gesture recorded: " + juce::String(static_cast<int>(t.events.size())) + " moves over "
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
        if (onSessionChanged)
            onSessionChanged();
    };
    startTimerHz(30);
}

AppCore::~AppCore()
{
    stopTimer();
    recorder.onFinished = nullptr; // the take is still finished by the recorder
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
    if (scenes.captureScene({}, lastFrame.cursor, lastFrame) < 0)
        status("The terrain is full (32 scenes). Remove one to capture another.", true);
}

void AppCore::loadFactoryContent()
{
    // A first launch (and every New) should play from the first touch: factory sounds
    // loaded and a terrain of starter scenes.
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
    // The controller mapping belongs to the rig: it persists in the app settings and
    // only changes when a session that carries its own mapping is opened.
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

void AppCore::timerCallback()
{
    scenes.tick();
    fx.tick();
    midi.tick();
    seasons.tick();
    paths.tick();
    gestures.tick();
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

    // A sample-rate change mid-take would corrupt the file: end it there.
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

} // namespace tf::app
