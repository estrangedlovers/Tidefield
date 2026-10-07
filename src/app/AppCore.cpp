#include "AppCore.h"

#include <BinaryData.h>
#include <io/AudioFileIO.h>
#include <io/Session.h>

namespace tf::app {

AppCore::AppCore(AudioHost& h)
    : host(h), engine(h.getEngine()), scenes(h.getEngine()), fx(h.getEngine()), catcher(h.getEngine()), midi(h.getEngine()),
      midiInputs(h.getEngine(), h.getSettings()), session(h.getEngine(), scenes, fx, &midi)
{
    fx.loadDefaultLayout();
    loadRigMidi();
    loadFactoryContent();

    catcher.onCaught = [this](int cloud, const std::string& name) {
        status("Caught into Cloud " + juce::String(cloud + 1) + " (" + juce::String(name) + ")");
    };
    catcher.onRejected = [this](const std::string& reason) { status(reason, true); };
    midi.onLearned = [this](const std::string& d) { status("MIDI learned: " + juce::String(d)); };
    session.onStatus = [this](const juce::String& m) { status(m); };
    session.onSessionChanged = [this] {
        if (onSessionChanged)
            onSessionChanged();
    };
    startTimerHz(30);
}

AppCore::~AppCore()
{
    stopTimer();
    saveRigMidi();
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
    // A first launch should make sound: Bloom gets the glass one-shot, Cloud 1 a pad.
    auto decode = [](const void* data, int size, const char* name) -> std::shared_ptr<const dsp::SampleBuffer> {
        juce::String error;
        auto b = io::loadSample(std::make_unique<juce::MemoryInputStream>(data, static_cast<size_t>(size), false), name, error);
        return std::shared_ptr<const dsp::SampleBuffer>(std::move(b));
    };
    engine.loadBloomSample(decode(BinaryData::glass_wav, BinaryData::glass_wavSize, "glass"));
    engine.loadCloudSample(0, decode(BinaryData::chord_wav, BinaryData::chord_wavSize, "chord"));
    engine.setParam(engine::P::BloomRoot, 81.0f); // glass.wav rings at A5
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
        if (onTelemetry)
            onTelemetry(lastFrame);
    }

    engine::EngineNotice notice;
    while (engine.popNotice(notice))
    {
        if (notice.type == engine::EngineNotice::Type::GuardTripped)
            status("Safety guard tripped: a non-finite sample was caught and the engine reset.", true);
        if (notice.type == engine::EngineNotice::Type::CaptureSceneRequest)
            captureSceneAtCursor();
        catcher.handle(notice);
        session.handleNotice(notice);
    }
}

} // namespace tf::app
