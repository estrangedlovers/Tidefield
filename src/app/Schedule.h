#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace tf::app {
struct DailyWindow
{
    static constexpr int kMinutesPerDay = 24 * 60;
    int start = 10 * 60;
    int stop = 18 * 60;

    bool contains(int minute) const noexcept
    {
        if (start == stop)
            return true;
        return start < stop ? minute >= start && minute < stop : minute >= start || minute < stop;
    }

    int minutesUntilChange(int minute) const noexcept
    {
        if (start == stop)
            return -1;
        const int next = contains(minute) ? stop : start;
        return ((next - minute) % kMinutesPerDay + kMinutesPerDay) % kMinutesPerDay;
    }
};

inline std::optional<int> parseClock(const juce::String& text)
{
    const auto t = text.trim();
    const auto colon = t.indexOfChar(':');
    if (colon <= 0 || ! t.substring(0, colon).containsOnly("0123456789") || ! t.substring(colon + 1).containsOnly("0123456789") || t.length() - colon - 1 != 2)
        return std::nullopt;
    const int h = t.substring(0, colon).getIntValue(), m = t.substring(colon + 1).getIntValue();
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return std::nullopt;
    return h * 60 + m;
}

inline juce::String formatClock(int minute)
{
    const int m = ((minute % DailyWindow::kMinutesPerDay) + DailyWindow::kMinutesPerDay) % DailyWindow::kMinutesPerDay;
    return juce::String(m / 60).paddedLeft('0', 2) + ":" + juce::String(m % 60).paddedLeft('0', 2);
}
}
