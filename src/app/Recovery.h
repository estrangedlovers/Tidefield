#pragma once

#include <juce_events/juce_events.h>

namespace tf::app {
class AppCore;

class Recovery final : private juce::Timer
{
public:
    explicit Recovery(AppCore& core);
    ~Recovery() override;

    static constexpr int kIntervals[] { 1, 2, 5, 10 };

    bool isEnabled() const;
    void setEnabled(bool on);
    int getMinutes() const;
    void setMinutes(int minutes);

    juce::File getRecoveryFile() const;
    juce::File getMarkerFile() const;

    bool crashedLastTime() const noexcept { return crashed; }
    void offerRestore();
    void saveNow();
    void finish();
    juce::String describe() const;

private:
    void timerCallback() override;
    void writeMarker(const juce::File& original);
    juce::File originalFromMarker() const;

    AppCore& core;
    bool crashed = false;
    bool finished = false;
    int seconds = 0;
    juce::Time lastSaved;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
