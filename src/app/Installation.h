#pragma once

#include "Schedule.h"

#include <juce_events/juce_events.h>

namespace tf::app {
class AppCore;

class Installation final : private juce::Timer
{
public:
    explicit Installation(AppCore& core);
    ~Installation() override;

    bool isEnabled() const;
    void setEnabled(bool on);
    bool hasSchedule() const;
    void setSchedule(bool on);
    DailyWindow getWindow() const;
    void setWindow(DailyWindow window);
    bool getKeepAwake() const;
    void setKeepAwake(bool on);
    juce::File getSessionFile() const;
    void setSessionFile(const juce::File& file);

    void launch();
    juce::String describe() const;
    juce::File getLogFile() const;

private:
    void timerCallback() override;
    void log(const juce::String& message);
    bool shouldPlay() const;
    static int minuteNow();

    AppCore& core;
    bool pendingFadeIn = false;
    int lastWanted = -1;
    int panicSeconds = 0;
    int deviceSeconds = 0;
    bool deviceLost = false;
    bool awakeHeld = false;
};
}
