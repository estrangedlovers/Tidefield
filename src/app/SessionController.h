#pragma once

#include <engine/Engine.h>
#include <engine/guest/GuestManager.h>
#include <engine/midi/MidiManager.h>
#include <engine/mod/ModRouteManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/mix/FxManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>
#include <io/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace tf::app {
class SessionController
{
public:
    SessionController(engine::Engine& engine, engine::SceneManager& scenes, engine::FxManager& fx, engine::MidiManager* midi,
                      engine::SeasonManager* seasons = nullptr, engine::PathManager* path = nullptr,
                      engine::GestureManager* gestures = nullptr, engine::ModRouteManager* mod = nullptr, engine::GuestManager* guest = nullptr);
    ~SessionController();

    void newSession();
    void open();
    void openFile(const juce::File& file);
    void save(std::function<void(bool)> then = nullptr);
    void saveAs(std::function<void(bool)> then = nullptr);
    void autosaveTo(const juce::File& file, std::function<void(bool)> done);
    void openRecovered(const juce::File& recovery, const juce::File& original);

    bool hasUnsavedChanges() const;
    void whenSafeToDiscard(const juce::String& action, std::function<void()> proceed);
    void tick();
    bool askBeforeDiscard = true;

    void handleNotice(const engine::EngineNotice& notice);
    void setLatest(const engine::TelemetryFrame& frame) { latest = frame; }

    juce::String getName() const { return current == juce::File() ? juce::String("Untitled") : current.getFileNameWithoutExtension(); }
    bool isBusy() const noexcept { return busy; }
    juce::File getFile() const { return current; }

    std::function<void(const juce::String&)> onStatus;
    std::function<void()> onSessionChanged;
    std::function<io::SessionData()> makeNewSession;
    std::function<void(const io::SessionData&)> onApplied;
    std::function<std::uint64_t(int hostSlot)> pluginEdits;

    void setWorkers(juce::ThreadPool* pool) { workers = pool; }

private:
    void apply(std::shared_ptr<io::SessionData> data);
    void applyNow();
    void saveTo(const juce::File& file, std::function<void(bool)> then);
    void openFileNow(const juce::File& file);
    void newSessionNow();
    io::SessionData captureNow() const;

    engine::Engine& engine;
    engine::SceneManager& scenes;
    engine::FxManager& fx;
    engine::MidiManager* midi;
    engine::SeasonManager* seasons;
    engine::PathManager* path;
    engine::GestureManager* gestures;
    engine::ModRouteManager* mod;
    engine::GuestManager* guest;
    engine::TelemetryFrame latest;
    juce::File current;
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<io::SessionData> pending;
    bool waitingForFadeOut = false;
    bool busy = false;
    bool autosaving = false;
    juce::File afterOpen;
    bool replaceAfterOpen = false;
    bool asking = false;
    bool changedSinceSave = false;
    int baselineDue = 0;
    std::shared_ptr<const io::SessionData> baseline;
    juce::ThreadPool* workers = nullptr;
    void runInBackground(std::function<void()> job);
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
