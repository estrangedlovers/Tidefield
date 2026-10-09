#pragma once

#include <engine/params/ParamRegistry.h>

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace tf::app {
struct ControllerTemplate
{
    juce::String name;
    juce::String device;
    juce::String description;
    juce::var midi;
    bool factory = false;
    juce::File file;
};

std::vector<ControllerTemplate> factoryControllerTemplates(const engine::ParamRegistry& registry);

juce::var controllerTemplateToJson(const ControllerTemplate& t);
std::optional<ControllerTemplate> controllerTemplateFromJson(const juce::var& json, juce::String& error);
std::vector<std::string> validateControllerTemplate(const ControllerTemplate& t, const engine::ParamRegistry& registry);

bool deviceMatches(const juce::String& templateDevice, const juce::String& deviceName);
int countBindings(const juce::var& midi);
juce::var mergeMidiJson(const juce::var& current, const juce::var& added, bool replace);
bool midiContains(const juce::var& current, const juce::var& wanted);

class ControllerTemplateLibrary
{
public:
    ControllerTemplateLibrary(juce::File folder, const engine::ParamRegistry& registry);

    const juce::File& getFolder() const noexcept { return folder; }
    std::vector<ControllerTemplate> list() const;
    std::vector<ControllerTemplate> userTemplates() const;
    bool save(const juce::String& name, const juce::String& device, const juce::var& midi, juce::String& error);
    bool remove(const juce::String& name);
    bool exists(const juce::String& name) const;
    std::optional<ControllerTemplate> suggestFor(const juce::String& deviceName) const;
    int getVersion() const noexcept { return version; }

    static juce::String safeFileName(const juce::String& name);

private:
    juce::File folder;
    const engine::ParamRegistry& registry;
    std::vector<ControllerTemplate> factory;
    int version = 0;
};
}
