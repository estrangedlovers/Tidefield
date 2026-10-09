#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>

namespace {
class TestSine final : public juce::AudioProcessor
{
public:
    TestSine()
        : juce::AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo())),
          state(*this, nullptr, "TestSine",
                { std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "level", 1 }, "Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f),
                  std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "release", 1 }, "Release", juce::NormalisableRange<float>(0.01f, 2.0f), 0.2f,
                                                              juce::AudioParameterFloatAttributes().withLabel("s")) })
    {
    }

    const juce::String getName() const override { return "Tidefield Test Sine"; }

    void prepareToPlay(double sampleRate, int) override
    {
        rate = sampleRate;
        for (auto& v : voices)
            v = {};
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && l.getMainInputChannelSet().isDisabled();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        const float level = state.getRawParameterValue("level")->load();
        const float fall = 1.0f / std::max(1.0f, state.getRawParameterValue("release")->load() * static_cast<float>(rate));
        auto it = midi.begin();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            while (it != midi.end() && (*it).samplePosition <= i)
            {
                handle((*it).getMessage());
                ++it;
            }
            float x = 0.0f;
            for (auto& v : voices)
            {
                if (v.gain <= 0.0f && ! v.held)
                    continue;
                v.gain = v.held ? std::min(1.0f, v.gain + 0.01f) : std::max(0.0f, v.gain - fall);
                x += static_cast<float>(std::sin(v.phase)) * v.gain * v.velocity * 0.25f * level;
                v.phase += v.step;
                if (v.phase > juce::MathConstants<double>::twoPi)
                    v.phase -= juce::MathConstants<double>::twoPi;
            }
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample(c, i, x);
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
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
    struct Voice
    {
        int note = -1;
        bool held = false;
        float gain = 0.0f;
        float velocity = 0.0f;
        double phase = 0.0;
        double step = 0.0;
    };

    void handle(const juce::MidiMessage& m)
    {
        if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices)
                v.held = false;
            return;
        }
        if (m.isNoteOn())
        {
            Voice* slot = &voices[0];
            for (auto& v : voices)
                if (! v.held && v.gain <= 0.0f)
                {
                    slot = &v;
                    break;
                }
            *slot = { m.getNoteNumber(), true, 0.0f, m.getFloatVelocity(), 0.0,
                      juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz(m.getNoteNumber()) / rate };
        }
        else if (m.isNoteOff())
            for (auto& v : voices)
                if (v.held && v.note == m.getNoteNumber())
                    v.held = false;
    }

    juce::AudioProcessorValueTreeState state;
    std::array<Voice, 16> voices {};
    double rate = 48000.0;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestSine(); }
