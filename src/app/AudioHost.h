#pragma once

#include "Host.h"

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tf::app {

/** Owns the audio device and the engine. The device callback forwards straight to
    Engine::process; everything else (settings, persistence) happens on the message
    thread. */
class AudioHost final : public Host, private juce::AudioIODeviceCallback
{
public:
    explicit AudioHost(juce::PropertiesFile& settings);
    ~AudioHost() override;

    engine::Engine& getEngine() noexcept override { return engine; }
    juce::AudioDeviceManager* getDeviceManager() noexcept override { return &deviceManager; }

    /** 0..1 share of the callback's time budget, smoothed by JUCE. */
    double getCpuLoad() const override { return loadMeasurer.getLoadAsProportion(); }
    int getXrunCount() const { return loadMeasurer.getXRunCount(); }
    bool isRunning() const override { return deviceManager.getCurrentAudioDevice() != nullptr; }
    juce::String describeOutput() const override;

    void saveDeviceState();
    juce::PropertiesFile& getSettings() noexcept override { return settings; }

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
