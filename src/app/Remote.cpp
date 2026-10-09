#include "Remote.h"

#include "AppCore.h"

namespace tf::app {
namespace {
constexpr const char* kClockKey = "clockOut";
constexpr const char* kCycleOutKey = "cyclesMidiOut";
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

CycleMidiOut::CycleMidiOut(engine::Engine& e, juce::PropertiesFile& s, const BlockClock* c) : engine(e), settings(s), clock(c)
{
    deviceId = settings.getValue(kCycleOutKey);
    if (deviceId.isNotEmpty())
        output = juce::MidiOutput::openDevice(deviceId);
    startTimer(1);
}

CycleMidiOut::~CycleMidiOut()
{
    stopTimer();
    sendAllOff();
}

void CycleMidiOut::setDevice(const juce::String& identifier)
{
    stopTimer();
    sendAllOff();
    output.reset();
    deviceId = identifier;
    settings.setValue(kCycleOutKey, deviceId);
    if (deviceId.isNotEmpty())
        output = juce::MidiOutput::openDevice(deviceId);
    startTimer(1);
}

double CycleMidiOut::dueTime(const engine::MidiOutEvent& e, double now) const
{
    BlockClock::Reading r;
    if (clock == nullptr || ! clock->read(r))
        return now;
    const double offset = (static_cast<double>(e.sampleTime) - static_cast<double>(r.sampleTime)) * 1000.0 / r.sampleRate;
    const double due = r.ms + offset + r.blockMs;
    return due < now || due > now + 250.0 ? now : due;
}

void CycleMidiOut::send(const Scheduled& s)
{
    const int type = s.status & 0xf0, ch = s.status & 0x0f;
    auto& held = sounding[static_cast<std::size_t>(ch)][s.data1 & 127];
    if (type == 0x90 && s.data2 > 0)
    {
        if (held > 0 && output != nullptr)
            output->sendMessageNow(juce::MidiMessage::noteOff(ch + 1, s.data1 & 127));
        if (held == 0)
            soundingCount.fetch_add(1, std::memory_order_relaxed);
        held = 1;
    }
    else if (type == 0x80 || type == 0x90)
    {
        if (held == 0)
            return;
        held = 0;
        soundingCount.fetch_sub(1, std::memory_order_relaxed);
    }
    else if (type == 0xb0 && s.data1 == 123)
    {
        for (auto& n : sounding[static_cast<std::size_t>(ch)])
            if (n != 0)
            {
                n = 0;
                soundingCount.fetch_sub(1, std::memory_order_relaxed);
            }
    }
    if (output != nullptr)
        output->sendMessageNow(juce::MidiMessage(s.status, s.data1, s.data2));
}

void CycleMidiOut::sendAllOff()
{
    count = 0;
    for (int ch = 0; ch < 16; ++ch)
    {
        bool any = false;
        for (int n = 0; n < 128; ++n)
            if (sounding[static_cast<std::size_t>(ch)][static_cast<std::size_t>(n)] != 0)
            {
                any = true;
                if (output != nullptr)
                    output->sendMessageNow(juce::MidiMessage::noteOff(ch + 1, n));
            }
        if (any && output != nullptr)
            output->sendMessageNow(juce::MidiMessage::allNotesOff(ch + 1));
        sounding[static_cast<std::size_t>(ch)].fill(0);
    }
    soundingCount.store(0, std::memory_order_relaxed);
}

void CycleMidiOut::hiResTimerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (wantAllOff.exchange(false, std::memory_order_acq_rel))
    {
        engine::MidiOutEvent e;
        while (engine.popMidiOut(e)) {}
        sendAllOff();
    }
    engine::MidiOutEvent e;
    while (count < kMaxScheduled && engine.popMidiOut(e))
    {
        if (output == nullptr)
            continue;
        scheduled[static_cast<std::size_t>((head + count) % kMaxScheduled)] = { dueTime(e, now), e.status, e.data1, e.data2 };
        ++count;
    }
    while (count > 0)
    {
        const auto& next = scheduled[static_cast<std::size_t>(head)];
        if (next.due > now)
            break;
        send(next);
        head = (head + 1) % kMaxScheduled;
        --count;
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
