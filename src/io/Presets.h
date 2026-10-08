#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tf::io {
struct Preset
{
    std::string name;
    std::string kind;
    std::map<std::string, float> values;
    bool factory = false;
    juce::File file;
};

class PresetLibrary
{
public:
    explicit PresetLibrary(juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    juce::File getFolder() const { return folder; }
    juce::File folderFor(const std::string& kind) const;

    void addFactory(Preset p);
    std::vector<Preset> list(const std::string& kind) const;

    bool save(const Preset& p, juce::String& error);
    bool remove(const Preset& p);

    static juce::var toJson(const Preset& p);
    static std::optional<Preset> fromJson(const juce::var& json);

private:
    juce::File folder;
    std::vector<Preset> factory;
};
}
