#pragma once

#include <io/Performance.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace tf::app {
class AppCore;

class PerformanceController
{
public:
    explicit PerformanceController(AppCore& core);
    ~PerformanceController();

    enum class State { Idle, Recording, Playing };

    State getState() const noexcept { return state; }
    const io::Performance& get() const noexcept { return performance; }
    double getPosition() const noexcept;
    std::uint64_t getRevision() const noexcept { return revision; }

    void record();
    void play(double fromSeconds);
    void stop();
    void tick();

    void load(io::Performance p);
    void clear();
    void edit(const juce::String& name, const std::function<void(io::Performance&)>& change);

    struct Selection
    {
        double from = 0.0, to = 0.0;
        std::optional<io::Performance::Lane> lane;
        bool hasRange() const noexcept { return to - from > 1.0e-3; }
    };
    Selection selection;

    void save();
    void render(bool stems, double loopCrossfadeSeconds);
    void cancelRender() { cancel.store(true); }
    bool isRendering() const noexcept { return rendering; }
    float getRenderProgress() const noexcept { return progress.load(); }

private:
    void stopRecording();
    void startRender(const juce::File& folder, bool stems, double loopCrossfadeSeconds);

    AppCore& core;
    io::Performance performance;
    State state = State::Idle;
    std::uint64_t takeVersion = 0, revision = 0;
    double playOrigin = 0.0;
    int confirmCountdown = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::atomic<bool> cancel { false };
    std::atomic<float> progress { 0.0f };
    bool rendering = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
