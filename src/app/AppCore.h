#pragma once

#include "Host.h"
#include "MidiInputs.h"
#include "PluginHost.h"
#include "SessionController.h"

#include <engine/capture/CatchManager.h>
#include <engine/midi/MidiManager.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/ModRouteManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Presets.h>
#include <io/Recorder.h>

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace tf::app {
class AppCore final : private juce::Timer
{
public:
    explicit AppCore(Host& host);
    ~AppCore() override;

    Host& host;
    engine::Engine& engine;
    std::unique_ptr<PluginHost> plugins;
    engine::SceneManager scenes;
    engine::FxManager fx;
    engine::CatchManager catcher;
    engine::MidiManager midi;
    engine::SeasonManager seasons;
    engine::PathManager paths;
    engine::GestureManager gestures;
    engine::ModRouteManager mod;
    io::PresetLibrary presets;
    std::unique_ptr<MidiInputs> midiInputs;
    SessionController session;
    io::Recorder recorder;

    const engine::TelemetryFrame& latest() const noexcept { return lastFrame; }

    std::function<void(const engine::TelemetryFrame&)> onTelemetry;
    std::function<void(const engine::RawMidi&)> onMidiActivity;
    std::function<void(const juce::String& message, bool warning)> onStatus;
    std::function<void()> onCaptureSceneRequest;
    std::function<void()> onSessionChanged;

    void status(const juce::String& message, bool warning = false);
    void captureSceneAtCursor();
    void loadFactoryContent();

    void seedTargets(const io::SessionData& session);

    void startRecording();
    void stopRecording();
    void toggleRecording();
    bool getRecordStems() const;
    void setRecordStems(bool stems);
    juce::File getRecordingsFolder() const;
    void setRecordingsFolder(const juce::File& folder);
    juce::File getLastRecording() const { return recorder.getStatus().folder; }

private:
    void timerCallback() override;
    void loadRigMidi();
    void saveRigMidi();

    engine::TelemetryFrame lastFrame;
    double recordingRate = 0.0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int lastGuardLevel = 0;

public:
    juce::ThreadPool workers { juce::ThreadPoolOptions {}.withNumberOfThreads(2).withThreadName("Tidefield worker") };
};
}
