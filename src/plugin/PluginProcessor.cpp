#include "PluginProcessor.h"

#include <app/gui/MainView.h>
#include <io/Session.h>

#include <mutex>
#include <optional>

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
    snapshot = captureNow();
    startTimer(1000);
}

TidefieldProcessor::~TidefieldProcessor()
{
    stopTimer();
    *alive = false;
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

    // The DAW's tempo and song position: synced loops and delays follow them.
    double tempo = 0.0, ppq = 0.0;
    bool playing = false;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            tempo = pos->getBpm().orFallback(0.0);
            ppq = pos->getPpqPosition().orFallback(0.0);
            playing = pos->getIsPlaying();
        }
    engine.setHostTransport(tempo, ppq, playing);

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    // In place: the engine reads each block's inputs before writing its outputs.
    engine.process(buffer.getArrayOfReadPointers(), numIn, buffer.getArrayOfWritePointers(), numOut, buffer.getNumSamples());
}

juce::AudioProcessorEditor* TidefieldProcessor::createEditor() { return new Editor(*this); }

std::shared_ptr<const io::SessionData> TidefieldProcessor::captureNow()
{
    return std::make_shared<const io::SessionData>(
        io::captureSession(engine, core->latest(), core->scenes, core->fx, &core->midi, &core->seasons, &core->paths, &core->gestures));
}

void TidefieldProcessor::timerCallback()
{
    // Keep a recent snapshot for hosts that ask for state from another thread (the
    // managers belong to this one). Cheap: sounds are shared, not copied.
    if (restorePending.load())
        return;
    auto fresh = captureNow();
    const std::scoped_lock lock(stateLock);
    snapshot = std::move(fresh);
}

void TidefieldProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // The whole piece, sounds included, in the same format as a .tidefield file. On the
    // message thread it is captured now; from another thread (or while a restore is
    // still on its way) the latest snapshot is used. Nothing here blocks on another
    // thread, so a host cannot deadlock against us.
    std::shared_ptr<const io::SessionData> session;
    if (juce::MessageManager::getInstance()->isThisTheMessageThread() && ! restorePending.load())
        session = captureNow();
    else
    {
        const std::scoped_lock lock(stateLock);
        session = snapshot;
    }
    if (session == nullptr)
        return;
    juce::MemoryOutputStream out(dest, false);
    juce::String error;
    if (! io::writeSession(*session, out, error))
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
    auto restored = std::make_shared<const io::SessionData>(std::move(*session));
    {
        // Until it is applied, saving returns exactly what was restored.
        const std::scoped_lock lock(stateLock);
        snapshot = restored;
    }
    restorePending = true;
    auto apply = [this, restored] {
        io::applySession(*restored, engine, core->scenes, core->fx, true, &core->midi, &core->seasons, &core->paths, &core->gestures);
        core->seedTargets(*restored);
        restorePending = false;
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync([weak = std::weak_ptr<bool>(alive), apply] {
            if (! weak.expired()) // the host may delete us before the message loop runs
                apply();
        });
}

} // namespace tf::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tf::plugin::TidefieldProcessor(); }
