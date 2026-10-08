#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tf::io {

/** A device preset: values for one device's parameters, keyed by the parameter ID
    without the device's prefix ("density", not "cloud2.density"), so a cloud preset
    loads into any cloud. Effect presets have kind "fx:<type>" and keys p1..p6, mix. */
struct Preset
{
    std::string name;
    std::string kind;   // "drone", "cloud", "fx:tf.reverb", ...
    std::map<std::string, float> values;
    bool factory = false;
    juce::File file; // where a user preset was read from
};

/** User presets as JSON files, one folder per kind, plus the built-in ones the app
    registers. Message thread. */
class PresetLibrary
{
public:
    explicit PresetLibrary(juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    juce::File getFolder() const { return folder; }
    juce::File folderFor(const std::string& kind) const;

    void addFactory(Preset p);
    /** Factory presets first, then the user's, each sorted by name. */
    std::vector<Preset> list(const std::string& kind) const;

    bool save(const Preset& p, juce::String& error);
    bool remove(const Preset& p);

    static juce::var toJson(const Preset& p);
    static std::optional<Preset> fromJson(const juce::var& json);

private:
    juce::File folder;
    std::vector<Preset> factory;
};

} // namespace tf::io
