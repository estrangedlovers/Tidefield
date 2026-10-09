#pragma once

#include "ControllerTemplates.h"
#include "Host.h"
#include "Installation.h"
#include "KeyBindings.h"
#include "MidiInputs.h"
#include "PerformanceController.h"
#include "PluginHost.h"
#include "Recovery.h"
#include "Remote.h"
#include "Undo.h"
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
    ControllerTemplateLibrary controllers;
    KeyBindings keys;
    std::unique_ptr<MidiInputs> midiInputs;
    SessionController session;
    io::Recorder recorder;
    std::unique_ptr<MidiClockOut> clockOut;
    std::unique_ptr<OscRemote> osc;
    PerformanceController performance { *this };
    std::unique_ptr<Installation> installation;
    std::unique_ptr<Recovery> recovery;

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

    juce::UndoManager undo { 0, 300 };
    void recordParamChange(engine::ParamIndex param, float from, float to, bool continuing);
    void editScenes(const juce::String& name, const std::function<void()>& change);
    void editRoutes(const juce::String& name, const std::function<void()>& change);
    void editMacros(const juce::String& name, const std::function<void()>& change);
    void editSeasons(const juce::String& name, const std::function<void()>& change);
    void editMidi(const juce::String& name, const std::function<void()>& change);
    void applyControllerTemplate(const ControllerTemplate& t, bool replace);
    void setKey(KeyAction action, const juce::KeyPress& key);
    void resetKeys();
    std::function<void()> onKeysChanged;
    void setEffect(int slot, const std::string& type);
    bool isUndoing() const noexcept { return undoing; }

    void startRecording();
    void stopRecording();
    void toggleRecording();
    bool getRecordStems() const;
    void setRecordStems(bool stems);
    juce::File getRecordingsFolder() const;
    void setRecordingsFolder(const juce::File& folder);
    juce::File getLastRecording() const { return recorder.getStatus().folder; }

    juce::StringArray recentSessions() const;
    void rememberSession(const juce::File& file);
    void clearRecentSessions();
    bool getOpenLastSession() const;
    void setOpenLastSession(bool open);
    double getRenderSampleRate() const;
    void setRenderSampleRate(double rate);

private:
    void timerCallback() override;
    void loadRigMidi();
    void saveRigMidi();
    void suggestControllerTemplates();

    juce::StringArray connectedInputs;

    engine::TelemetryFrame lastFrame;
    double recordingRate = 0.0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int lastGuardLevel = 0;
    float lastTempoTarget = -1.0f;
    bool undoing = false;
    engine::ParamIndex lastUndoParam = engine::kNumParams;
    double lastUndoTime = 0.0;

public:
    juce::ThreadPool workers { juce::ThreadPoolOptions {}.withNumberOfThreads(2).withThreadName("Tidefield worker") };
};
}
