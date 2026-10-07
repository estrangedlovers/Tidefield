#pragma once

#include "AudioHost.h"
#include "MidiInputs.h"
#include "SessionController.h"

#include <engine/capture/CatchManager.h>
#include <engine/midi/MidiManager.h>
#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>

#include <juce_events/juce_events.h>

#include <functional>

namespace tf::app {

/** Everything on the message thread that is not drawing: the managers, the 30 Hz pump
    that drains telemetry, notices and MIDI monitor messages, factory content, and the
    rig's MIDI mapping. Both front ends (the web UI and the JUCE fallback panel)
    observe it through the callbacks below. */
class AppCore final : private juce::Timer
{
public:
    explicit AppCore(AudioHost& host);
    ~AppCore() override;

    AudioHost& host;
    engine::Engine& engine;
    engine::SceneManager scenes;
    engine::FxManager fx;
    engine::CatchManager catcher;
    engine::MidiManager midi;
    MidiInputs midiInputs;
    SessionController session;

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

private:
    void timerCallback() override;
    void loadRigMidi();
    void saveRigMidi();

    engine::TelemetryFrame lastFrame;
};

} // namespace tf::app
