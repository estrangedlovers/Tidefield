#include <AudioProcessorEffect.h>
#include <dsp/fx/ProcessorFactory.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {
class TestGainProcessor final : public juce::AudioProcessor
{
public:
    TestGainProcessor()
        : AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()))
    {
        addParameter(gain = new juce::AudioParameterFloat(juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
        addParameter(tone = new juce::AudioParameterFloat(juce::ParameterID { "tone", 1 }, "Tone", 0.0f, 1.0f, 0.2f));
    }

    const juce::String getName() const override { return "TestGain"; }
    void prepareToPlay(double, int) override { prepared = true; }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain(gain->get()); }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    bool prepared = false;
};
}

TEST_CASE("A JUCE AudioProcessor registers as an FX type and processes in place", "[fxjuce]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    tf::fxjuce::registerAudioProcessorEffect<TestGainProcessor>("test.gain", "Test Gain", false, { "tone", "gain" });

    const auto* info = tf::dsp::ProcessorFactory::instance().find("test.gain");
    REQUIRE(info != nullptr);
    CHECK(std::string(info->name) == "Test Gain");
    CHECK(std::string(info->controls[0].name) == "Tone");
    CHECK(std::string(info->controls[1].name) == "Gain");
    CHECK(info->controls[1].defaultValue == Catch::Approx(0.5f));
    CHECK(info->controls[2].display.curve == tf::dsp::DisplayMap::Curve::Hidden);

    auto p = tf::dsp::ProcessorFactory::instance().create("test.gain");
    REQUIRE(p != nullptr);
    p->prepare({ 48000.0, 256 });
    auto* hosted = dynamic_cast<tf::fxjuce::AudioProcessorEffect<TestGainProcessor>*>(p.get());
    REQUIRE(hosted != nullptr);
    CHECK(hosted->getProcessor().prepared);

    p->setControls({ 0.2f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f }, {});
    hosted->flushParameters();
    CHECK(hosted->getProcessor().gain->get() == Catch::Approx(0.25f));

    std::vector<float> l(600, 1.0f), r(600, -1.0f);
    p->process(l.data(), r.data(), 600);
    CHECK(l[0] == Catch::Approx(0.25f));
    CHECK(l[599] == Catch::Approx(0.25f));
    CHECK(r[300] == Catch::Approx(-0.25f));
}
