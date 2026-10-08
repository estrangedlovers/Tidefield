#pragma once

#include "Host.h"
#include "MidiInputs.h"
#include "SessionController.h"

#include <engine/capture/CatchManager.h>
#include <engine/midi/MidiManager.h>
#include <engine/mix/FxManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Presets.h>
#include <io/Recorder.h>

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace tf::app {

/** Everything on the message thread that is not drawing: the managers, the 30 Hz pump
    that drains telemetry, notices and MIDI monitor messages, factory content, and the
    rig's MIDI mapping. Both front ends (the web UI and the JUCE fallback panel)
    observe it through the callbacks below. */
class AppCore final : private juce::Timer
{
public:
    explicit AppCore(Host& host);
    ~AppCore() override;

    Host& host;
    engine::Engine& engine;
    engine::SceneManager scenes;
    engine::FxManager fx;
    engine::CatchManager catcher;
    engine::MidiManager midi;
    engine::SeasonManager seasons;
    engine::PathManager paths;
    engine::GestureManager gestures;
    io::PresetLibrary presets;
    /** Hardware MIDI inputs: the standalone app opens them itself; in a DAW, MIDI
        arrives with the audio and this is null. */
    std::unique_ptr<MidiInputs> midiInputs;
    SessionController session;
    io::Recorder recorder;

    const engine::TelemetryFrame& latest() const noexcept { return lastFrame; }

    /** Called on the message thread. */
    std::function<void(const engine::TelemetryFrame&)> onTelemetry;
    std::function<void(const engine::RawMidi&)> onMidiActivity;
    std::function<void(const juce::String& message, bool warning)> onStatus;
    std::function<void()> onCaptureSceneRequest;
    std::function<void()> onSessionChanged;

    void status(const juce::String& message, bool warning = false);
    void captureSceneAtCursor();
    void loadFactoryContent();

    /** Records a session's parameter values as the current targets. Telemetry is the
        source of truth while audio runs; before it does (no device yet, or a DAW that
        has not started processing) this keeps saving from writing stale values. */
    void seedTargets(const io::SessionData& session);

    // Recording to disk (post-Medium master, optional stems).
    void startRecording();
    void stopRecording();
    void toggleRecording();
    bool getRecordStems() const;
    void setRecordStems(bool stems);
    juce::File getRecordingsFolder() const;
    void setRecordingsFolder(const juce::File& folder);
    /** The folder of the most recent take, if any. */
    juce::File getLastRecording() const { return recorder.getStatus().folder; }

private:
    void timerCallback() override;
    void loadRigMidi();
    void saveRigMidi();

    engine::TelemetryFrame lastFrame;
    double recordingRate = 0.0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true); // for callbacks posted from other threads
    int lastGuardLevel = 0;

public:
    /** Background work (file loading, decoding, saving). Declared last, so it is
        destroyed first: its destructor waits for running jobs before anything they
        touch goes away. */
    juce::ThreadPool workers { juce::ThreadPoolOptions {}.withNumberOfThreads(2).withThreadName("Tidefield worker") };
};

} // namespace tf::app
