#pragma once

#include <app/AppCore.h>
#include <app/Host.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <io/Session.h>
#include <memory>
#include <mutex>

namespace tf::plugin {

/** Tidefield inside a DAW (AU, VST3). The DAW drives the engine from processBlock;
    MIDI from the track plays Bloom, drives mappings and learns like a controller;
    the whole piece (scenes, sounds, effects, seasons, path) is saved in the project.
    The interface and everything above the engine are the standalone app's own. */
class TidefieldProcessor final : public juce::AudioProcessor, public app::Host, private juce::Timer
{
public:
    TidefieldProcessor();
    ~TidefieldProcessor() override;

    // --- AudioProcessor ---------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tidefield"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; } // reverbs and freezes ring a long time

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // --- Host -------------------------------------------------------------------------
    engine::Engine& getEngine() noexcept override { return engine; }
    juce::PropertiesFile& getSettings() noexcept override { return *settings.getUserSettings(); }
    double getCpuLoad() const override { return loadMeasurer.getLoadAsProportion(); }
    bool isRunning() const override { return prepared; }
    juce::String describeOutput() const override;
    bool isPlugin() const noexcept override { return true; }

    app::AppCore& getCore() noexcept { return *core; }

private:
    void timerCallback() override;
    std::shared_ptr<const io::SessionData> captureNow();

    juce::ApplicationProperties settings;
    engine::Engine engine;
    std::unique_ptr<app::AppCore> core;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<bool> prepared { false };
    // State for hosts that ask from other threads: refreshed on the message thread.
    std::mutex stateLock;
    std::shared_ptr<const io::SessionData> snapshot;
    std::atomic<bool> restorePending { false };
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TidefieldProcessor)
};

} // namespace tf::plugin
