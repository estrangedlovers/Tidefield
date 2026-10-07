#include "PluginProcessor.h"

#include <app/gui/MainView.h>
#include <io/Session.h>

namespace tf::plugin {

namespace {

/** The standalone interface inside the DAW's plugin window. */
class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor(TidefieldProcessor& p) : juce::AudioProcessorEditor(p), view(p.getCore())
    {
        addAndMakeVisible(view);
        setResizable(true, true);
        setResizeLimits(1100, 720, 3000, 2000);
        setSize(1440, 900);
    }
    void resized() override { view.setBounds(getLocalBounds()); }

private:
    app::gui::MainView view;
};

} // namespace

TidefieldProcessor::TidefieldProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Live input", juce::AudioChannelSet::stereo(), false)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Tidefield";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = "Tidefield";
    settings.setStorageParameters(options);
    // Sensible until the DAW says otherwise; the DAW's prepareToPlay replaces it.
    engine.prepare(48000.0, 512);
    core = std::make_unique<app::AppCore>(*this);
    // An instrument in a DAW should sound when the track plays, not wait for Fade in.
    engine.command(engine::Command::FadeIn);
}

TidefieldProcessor::~TidefieldProcessor()
{
    core.reset();
    settings.closeFiles();
}

juce::String TidefieldProcessor::describeOutput() const
{
    if (! prepared)
        return {};
    return "Plugin  " + juce::String(getSampleRate() / 1000.0, 1) + " kHz";
}

void TidefieldProcessor::prepareToPlay(double sampleRate, int maxBlockSize)
{
    prepared = false;
    engine.prepare(sampleRate, maxBlockSize);
    loadMeasurer.reset(sampleRate, maxBlockSize);
    lastLatency = engine.getLatencySamples();
    setLatencySamples(lastLatency);
    prepared = true;
}

void TidefieldProcessor::releaseResources()
{
    prepared = false;
}

bool TidefieldProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void TidefieldProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::AudioProcessLoadMeasurer::ScopedTimer timer(loadMeasurer, buffer.getNumSamples());

    // The track's MIDI goes through the same port a controller would: notes play
    // Bloom, CCs drive mappings and MIDI learn hears them.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.getRawDataSize() < 1 || m.isSysEx())
            continue;
        const auto* d = m.getRawData();
        engine::RawMidi raw;
        raw.status = d[0];
        raw.data1 = m.getRawDataSize() > 1 ? d[1] : 0;
        raw.data2 = m.getRawDataSize() > 2 ? d[2] : 0;
        raw.port = 0;
        engine.postMidi(0, raw);
    }
    midi.clear();

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    // In place: the engine reads each block's inputs before writing its outputs.
    engine.process(buffer.getArrayOfReadPointers(), numIn, buffer.getArrayOfWritePointers(), numOut, buffer.getNumSamples());
}

juce::AudioProcessorEditor* TidefieldProcessor::createEditor() { return new Editor(*this); }

void TidefieldProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // The whole piece, sounds included, in the same format as a .tidefield file.
    const auto session = io::captureSession(engine, core->latest(), core->scenes, core->fx, &core->midi, &core->seasons, &core->paths);
    juce::MemoryOutputStream out(dest, false);
    juce::String error;
    if (! io::writeSession(session, out, error))
        juce::Logger::writeToLog("Tidefield: could not save state: " + error);
}

void TidefieldProcessor::setStateInformation(const void* data, int size)
{
    juce::String error;
    auto session = io::readSession(data, static_cast<std::size_t>(std::max(0, size)), error);
    if (! session)
    {
        juce::Logger::writeToLog("Tidefield: could not restore state: " + error);
        return;
    }
    auto apply = [this, s = std::make_shared<io::SessionData>(std::move(*session))] {
        io::applySession(*s, engine, core->scenes, core->fx, true, &core->midi, &core->seasons, &core->paths);
        core->seedTargets(*s);
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync(apply);
}

} // namespace tf::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tf::plugin::TidefieldProcessor(); }
