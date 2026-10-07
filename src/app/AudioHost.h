#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tf::app {

/** Owns the audio device and the engine. The device callback forwards straight to
    Engine::process; everything else (settings, persistence) happens on the message
    thread. */
class AudioHost final : private juce::AudioIODeviceCallback
{
public:
    explicit AudioHost(juce::PropertiesFile& settings);
    ~AudioHost() override;

    engine::Engine& getEngine() noexcept { return engine; }
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    /** 0..1 share of the callback's time budget, smoothed by JUCE. */
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }
    int getXrunCount() const { return loadMeasurer.getXRunCount(); }

    void saveDeviceState();

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                          int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::PropertiesFile& settings;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    engine::Engine engine;
};

} // namespace tf::app
