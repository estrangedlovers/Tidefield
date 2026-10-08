#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tf::app {
class Host
{
public:
    virtual ~Host() = default;

    virtual engine::Engine& getEngine() noexcept = 0;
    virtual juce::PropertiesFile& getSettings() noexcept = 0;
    virtual juce::AudioDeviceManager* getDeviceManager() noexcept { return nullptr; }
    virtual double getCpuLoad() const { return 0.0; }
    virtual bool isRunning() const = 0;
    virtual juce::String describeOutput() const = 0;
    virtual bool isPlugin() const noexcept { return false; }
};
}
