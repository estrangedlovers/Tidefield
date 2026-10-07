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

/** New / Open / Save for the app. File work happens on a background thread; applying
    happens on the message thread. Opening while sound is playing fades the master
    out, swaps everything with no smoothing (so nothing audibly sweeps), then fades
    back in. */
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

    /** Feed every engine notice and the latest telemetry (message thread). */
    void handleNotice(const engine::EngineNotice& notice);
    void setLatest(const engine::TelemetryFrame& frame) { latest = frame; }

    juce::String getName() const { return current == juce::File() ? juce::String("Untitled") : current.getFileNameWithoutExtension(); }
    bool isBusy() const noexcept { return busy; }

    /** UI feedback: short status messages and warnings. */
    std::function<void(const juce::String&)> onStatus;
    std::function<void()> onSessionChanged;
    /** What New starts from (the app's starter session); the plain defaults if unset. */
    std::function<io::SessionData()> makeNewSession;
    /** After a session is applied (message thread), with what was applied. */
    std::function<void(const io::SessionData&)> onApplied;

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
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

} // namespace tf::app
