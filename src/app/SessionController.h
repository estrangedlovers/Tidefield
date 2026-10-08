#pragma once

#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/mix/FxManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace tf::app {
class SessionController
{
public:
    SessionController(engine::Engine& engine, engine::SceneManager& scenes, engine::FxManager& fx, engine::MidiManager* midi,
                      engine::SeasonManager* seasons = nullptr, engine::PathManager* path = nullptr,
                      engine::GestureManager* gestures = nullptr);
    ~SessionController();

    void newSession();
    void open();
    void openFile(const juce::File& file);
    void save();
    void saveAs();

    void handleNotice(const engine::EngineNotice& notice);
    void setLatest(const engine::TelemetryFrame& frame) { latest = frame; }

    juce::String getName() const { return current == juce::File() ? juce::String("Untitled") : current.getFileNameWithoutExtension(); }
    bool isBusy() const noexcept { return busy; }

    std::function<void(const juce::String&)> onStatus;
    std::function<void()> onSessionChanged;
    std::function<io::SessionData()> makeNewSession;
    std::function<void(const io::SessionData&)> onApplied;

    void setWorkers(juce::ThreadPool* pool) { workers = pool; }

private:
    void apply(std::shared_ptr<io::SessionData> data);
    void applyNow();
    void saveTo(const juce::File& file);

    engine::Engine& engine;
    engine::SceneManager& scenes;
    engine::FxManager& fx;
    engine::MidiManager* midi;
    engine::SeasonManager* seasons;
    engine::PathManager* path;
    engine::GestureManager* gestures;
    engine::TelemetryFrame latest;
    juce::File current;
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<io::SessionData> pending;
    bool waitingForFadeOut = false;
    bool busy = false;
    juce::ThreadPool* workers = nullptr;
    void runInBackground(std::function<void()> job);
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
