#include <juce_audio_processors/juce_audio_processors.h>

namespace {
class TestGain final : public juce::AudioProcessor
{
public:
    TestGain()
        : juce::AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo())),
          state(*this, nullptr, "TestGain",
                { std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "gain", 1 }, "Gain", juce::NormalisableRange<float>(-60.0f, 12.0f), 0.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("dB")),
                  std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "balance", 1 }, "Balance", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f) })
    {
    }

    const juce::String getName() const override { return "Tidefield Test Gain"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == l.getMainOutputChannelSet();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const float gain = juce::Decibels::decibelsToGain(state.getRawParameterValue("gain")->load(), -60.0f);
        const float balance = state.getRawParameterValue("balance")->load();
        buffer.applyGain(0, 0, buffer.getNumSamples(), gain * std::min(1.0f, 1.0f - balance));
        if (buffer.getNumChannels() > 1)
            buffer.applyGain(1, 0, buffer.getNumSamples(), gain * std::min(1.0f, 1.0f + balance));
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary(*xml, dest);
    }

    void setStateInformation(const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary(data, size))
            state.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    juce::AudioProcessorValueTreeState state;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGain(); }
