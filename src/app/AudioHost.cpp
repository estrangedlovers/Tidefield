#include "AudioHost.h"

namespace tf::app {

namespace {
constexpr auto kDeviceStateKey = "audioDeviceState";
} // namespace

AudioHost::AudioHost(juce::PropertiesFile& s) : settings(s)
{
    const auto saved = settings.getXmlValue(kDeviceStateKey);
    // Stereo out, up to two inputs for the live channel (guitar/cello).
    const auto error = deviceManager.initialise(2, 2, saved.get(), true);
    if (error.isNotEmpty())
        juce::Logger::writeToLog("Audio device error: " + error);

    deviceManager.addAudioCallback(this);
}

AudioHost::~AudioHost()
{
    saveDeviceState();
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
}

void AudioHost::saveDeviceState()
{
    if (const auto xml = deviceManager.createStateXml())
        settings.setValue(kDeviceStateKey, xml.get());
    settings.saveIfNeeded();
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const double sampleRate = device->getCurrentSampleRate();
    const int blockSize = device->getCurrentBufferSizeSamples();
    loadMeasurer.reset(sampleRate, blockSize);
    engine.prepare(sampleRate, blockSize);
}

void AudioHost::audioDeviceStopped()
{
    loadMeasurer.reset();
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                                 int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const juce::AudioProcessLoadMeasurer::ScopedTimer timer(loadMeasurer, numSamples);
    engine.process(inputs, numInputs, outputs, numOutputs, numSamples);
}

} // namespace tf::app
