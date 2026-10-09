#pragma once

#include "LinkSync.h"

#include "Host.h"

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tf::app {
class AudioHost final : public Host, private juce::AudioIODeviceCallback
{
public:
    AudioHost(juce::PropertiesFile& settings, bool nullAudio = false);
    ~AudioHost() override;

    engine::Engine& getEngine() noexcept override { return engine; }
    juce::AudioDeviceManager* getDeviceManager() noexcept override { return &deviceManager; }

    double getCpuLoad() const override { return loadMeasurer.getLoadAsProportion(); }
    int getXrunCount() const { return loadMeasurer.getXRunCount(); }
    bool isRunning() const override { return deviceManager.getCurrentAudioDevice() != nullptr || nullThread != nullptr; }
    juce::String describeOutput() const override;

    void saveDeviceState();
    LinkSync* getLink() noexcept override { return &link; }
    juce::PropertiesFile& getSettings() noexcept override { return settings; }
    const BlockClock* getBlockClock() const noexcept override { return &clock; }

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                          int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::PropertiesFile& settings;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    engine::Engine engine;
    LinkSync link;
    double outputLatencySeconds = 0.0;
    double deviceRate = 0.0;
    BlockClock clock;
    std::unique_ptr<juce::Thread> nullThread;
};
}
