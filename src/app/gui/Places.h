#pragma once

#include <io/AudioFolder.h>

#include <juce_data_structures/juce_data_structures.h>

namespace tf::app::gui::places {
inline constexpr const char* kPlacesKey = "browserPlaces";
inline constexpr const char* kOpenKey = "browserPlacesOpen";

inline int& version() noexcept
{
    static int v = 0;
    return v;
}

inline juce::StringArray get(juce::PropertiesFile& settings) { return io::parsePlaces(settings.getValue(kPlacesKey)); }

inline void set(juce::PropertiesFile& settings, const juce::StringArray& list)
{
    settings.setValue(kPlacesKey, io::storePlaces(list));
    settings.saveIfNeeded();
    ++version();
}

inline bool add(juce::PropertiesFile& settings, const juce::File& folder)
{
    if (! folder.isDirectory())
        return false;
    auto list = get(settings);
    if (list.contains(folder.getFullPathName()))
        return false;
    list.add(folder.getFullPathName());
    set(settings, list);
    return true;
}

inline void remove(juce::PropertiesFile& settings, const juce::String& path)
{
    auto list = get(settings);
    list.removeString(path);
    set(settings, list);
}

inline juce::String revealName()
{
#if JUCE_MAC
    return "Show in Finder";
#elif JUCE_WINDOWS
    return "Show in Explorer";
#else
    return "Show in file manager";
#endif
}
}
