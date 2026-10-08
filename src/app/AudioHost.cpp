#include "AudioHost.h"

namespace tf::app {
namespace {
constexpr auto kDeviceStateKey = "audioDeviceState";
}

namespace {
class NullAudioThread final : public juce::Thread
{
public:
    explicit NullAudioThread(engine::Engine& e) : juce::Thread("Tidefield null audio"), engine(e) {}
    ~NullAudioThread() override { stopThread(2000); }

    void run() override
    {
        constexpr int kBlock = 512;
        constexpr double kRate = 48000.0;
        engine.prepare(kRate, kBlock);
        std::vector<float> l(kBlock), r(kBlock);
        float* outs[2] = { l.data(), r.data() };
        auto due = juce::Time::getMillisecondCounterHiRes();
        while (! threadShouldExit())
        {
            engine.process(nullptr, 0, outs, 2, kBlock);
            due += kBlock / kRate * 1000.0;
            const auto ms = due - juce::Time::getMillisecondCounterHiRes();
            if (ms > 0.0)
                wait(static_cast<int>(ms));
        }
    }

private:
    engine::Engine& engine;
};
}

AudioHost::AudioHost(juce::PropertiesFile& s, bool nullAudio) : settings(s)
{
    if (nullAudio)
    {
        nullThread = std::make_unique<NullAudioThread>(engine);
        nullThread->startThread(juce::Thread::Priority::high);
        return;
    }
    const auto saved = settings.getXmlValue(kDeviceStateKey);
    const auto error = deviceManager.initialise(2, 2, saved.get(), true);
    if (error.isNotEmpty())
        juce::Logger::writeToLog("Audio device error: " + error);

    deviceManager.addAudioCallback(this);
}

AudioHost::~AudioHost()
{
    if (nullThread != nullptr)
    {
        nullThread.reset();
        return;
    }
    saveDeviceState();
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
}

juce::String AudioHost::describeOutput() const
{
    if (nullThread != nullptr)
        return "No device (test)  48.0 kHz";
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return {};
    return device->getName() + "  " + juce::String(device->getCurrentSampleRate() / 1000.0, 1) + " kHz";
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
    outputLatencySeconds = static_cast<double>(device->getOutputLatencyInSamples() + blockSize) / sampleRate;
    link.prepare(sampleRate);
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
    link.apply(engine, numSamples, outputLatencySeconds);
    engine.process(inputs, numInputs, outputs, numOutputs, numSamples);
}
}
