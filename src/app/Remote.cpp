#include "Remote.h"

#include "AppCore.h"

namespace tf::app {
namespace {
constexpr const char* kClockKey = "clockOut";
constexpr const char* kOscInKey = "oscReceivePort";
constexpr const char* kOscOutKey = "oscSendTarget";
}

MidiClockOut::MidiClockOut(juce::PropertiesFile& s) : settings(s)
{
    deviceId = settings.getValue(kClockKey);
    open();
}

MidiClockOut::~MidiClockOut()
{
    stopTimer();
    if (output != nullptr && sentRunning)
        output->sendMessageNow(juce::MidiMessage::midiStop());
}

void MidiClockOut::setDevice(const juce::String& identifier)
{
    stopTimer();
    if (output != nullptr && sentRunning)
        output->sendMessageNow(juce::MidiMessage::midiStop());
    sentRunning = false;
    deviceId = identifier;
    settings.setValue(kClockKey, deviceId);
    open();
}

void MidiClockOut::open()
{
    output.reset();
    if (deviceId.isEmpty())
        return;
    output = juce::MidiOutput::openDevice(deviceId);
    if (output != nullptr)
        startTimer(1);
}

void MidiClockOut::update(float bpm, bool running)
{
    tempo.store(std::clamp(bpm, 20.0f, 400.0f), std::memory_order_relaxed);
    wantRunning.store(running, std::memory_order_relaxed);
}

void MidiClockOut::hiResTimerCallback()
{
    if (output == nullptr)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool running = wantRunning.load(std::memory_order_relaxed);
    if (running != sentRunning)
    {
        output->sendMessageNow(running ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiStop());
        sentRunning = running;
        nextTick = now;
    }
    const double period = 60.0 / (24.0 * static_cast<double>(tempo.load(std::memory_order_relaxed)));
    if (nextTick <= 0.0 || now - nextTick > 0.25)
        nextTick = now;
    while (nextTick <= now)
    {
        output->sendMessageNow(juce::MidiMessage::midiClock());
        nextTick += period;
    }
}

OscRemote::OscRemote(AppCore& c, juce::PropertiesFile& s) : core(c), settings(s)
{
    receiver.addListener(this);
    setReceivePort(settings.getIntValue(kOscInKey, 0));
    if (const auto target = settings.getValue(kOscOutKey); target.isNotEmpty())
        setSendTarget(target);
}

OscRemote::~OscRemote()
{
    receiver.removeListener(this);
    receiver.disconnect();
    sender.disconnect();
}

void OscRemote::setReceivePort(int port)
{
    receiver.disconnect();
    receiving = false;
    receivePort = std::clamp(port, 0, 65535);
    settings.setValue(kOscInKey, receivePort);
    if (receivePort == 0)
        return;
    receiving = receiver.connect(receivePort);
    if (! receiving)
        core.status("OSC: port " + juce::String(receivePort) + " is in use by another program.", true);
}

void OscRemote::setSendTarget(const juce::String& hostAndPort)
{
    sender.disconnect();
    sending = false;
    sendHost = hostAndPort.upToLastOccurrenceOf(":", false, false).trim();
    sendPort = hostAndPort.fromLastOccurrenceOf(":", false, false).getIntValue();
    if (sendHost.isEmpty())
        sendHost = "127.0.0.1";
    if (sendPort <= 0 || sendPort > 65535)
        return;
    settings.setValue(kOscOutKey, getSendTarget());
    sending = sender.connect(sendHost, sendPort);
}

void OscRemote::stopSending()
{
    sender.disconnect();
    sending = false;
    settings.removeValue(kOscOutKey);
}

float OscRemote::number(const juce::OSCMessage& m, int index, float fallback) const
{
    if (index >= m.size())
        return fallback;
    const auto& a = m[index];
    if (a.isFloat32())
        return a.getFloat32();
    if (a.isInt32())
        return static_cast<float>(a.getInt32());
    return fallback;
}

void OscRemote::oscMessageReceived(const juce::OSCMessage& message) { handle(message); }

void OscRemote::oscBundleReceived(const juce::OSCBundle& bundle)
{
    for (const auto& element : bundle)
    {
        if (element.isMessage())
            handle(element.getMessage());
        else if (element.isBundle())
            oscBundleReceived(element.getBundle());
    }
}

void OscRemote::handle(const juce::OSCMessage& m)
{
    const auto address = m.getAddressPattern().toString();
    if (! address.startsWith("/tidefield/"))
        return;
    const auto path = address.fromFirstOccurrenceOf("/tidefield/", false, false);
    auto& engine = core.engine;
    const auto& reg = engine.getRegistry();
    auto setParam = [&](const juce::String& id, float value, bool normalised) {
        if (const auto p = reg.find(id.toStdString()))
        {
            const auto& spec = reg.spec(*p);
            engine.post(engine::ControlEvent::setParam(*p, normalised ? spec.fromNormalised(std::clamp(value, 0.0f, 1.0f)) : value,
                                                       engine::ControlSource::Midi));
        }
    };

    if (path.startsWith("param/"))
        setParam(path.fromFirstOccurrenceOf("param/", false, false), number(m, 0), false);
    else if (path.startsWith("norm/"))
        setParam(path.fromFirstOccurrenceOf("norm/", false, false), number(m, 0), true);
    else if (path == "terrain")
    {
        setParam("terrain.x", number(m, 0, 0.5f), false);
        setParam("terrain.y", number(m, 1, 0.5f), false);
    }
    else if (path == "scene" || path == "scene/jump")
    {
        if (onScene)
            onScene(static_cast<int>(number(m, 0, 1.0f)) - 1, path.endsWith("jump"));
    }
    else if (path == "note")
        engine.noteOn(static_cast<int>(number(m, 0, 60.0f)), std::clamp(number(m, 1, 0.8f), 0.0f, 1.0f));
    else if (path == "fade")
    {
        const auto st = core.latest().fadeState;
        engine.command(st == engine::FadeState::Silent || st == engine::FadeState::FadingOut ? engine::Command::FadeIn : engine::Command::FadeOut);
    }
    else if (path == "panic")
        engine.command(core.latest().panicActive ? engine::Command::ResumeFromPanic : engine::Command::Panic);
    else if (path == "catch")
        engine.command(engine::Command::Catch);
    else if (path == "loop")
        engine.command(engine::Command::LoopRecord);
    else if (path == "capture")
        core.captureSceneAtCursor();
    else if (path == "release")
        core.scenes.releaseLiveLayer();
    else if (path == "record")
        core.toggleRecording();
    else if (path == "swell" || path == "hush" || path == "slow")
        setParam(path + ".hold", number(m, 0, 1.0f) > 0.5f ? 1.0f : 0.0f, false);
}

void OscRemote::sendFrame(const engine::TelemetryFrame& f)
{
    if (! sending || ++frameCounter % 2 != 0)
        return;
    sender.send("/tidefield/cursor", f.cursor.x, f.cursor.y);
    sender.send("/tidefield/position", f.position.x, f.position.y);
    sender.send("/tidefield/level", f.rmsL, f.rmsR);
    sender.send("/tidefield/tide", f.tide);
    sender.send("/tidefield/beat", f.bpm, f.beatPhase);
    for (int s = 0; s < engine::kNumModSources; ++s)
        sender.send(juce::String("/tidefield/mod/") + engine::kModSources[static_cast<std::size_t>(s)].id, f.modValue[static_cast<std::size_t>(s)]);
    juce::OSCMessage weights("/tidefield/scenes");
    for (int k = 0; k < f.numScenes; ++k)
        weights.addFloat32(f.sceneWeights[static_cast<std::size_t>(k)]);
    sender.send(weights);
}
}
