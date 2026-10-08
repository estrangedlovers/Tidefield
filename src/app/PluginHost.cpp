#include "PluginHost.h"

#include <algorithm>

namespace tf::app {
namespace {
constexpr const char* kKnownKey = "knownPlugins";
constexpr int kMaxControls = 6;

std::vector<juce::AudioProcessorParameter*> controllableParameters(juce::AudioPluginInstance& instance)
{
    std::vector<juce::AudioProcessorParameter*> out;
    const auto* bypass = instance.getBypassParameter();
    for (auto* p : instance.getParameters())
        if (p != bypass && p->isAutomatable() && ! p->isMetaParameter() && static_cast<int>(out.size()) < kMaxControls)
            out.push_back(p);
    return out;
}

class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow(const juce::String& name, juce::AudioProcessorEditor* editor, std::function<void()> closed)
        : juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          onClosed(std::move(closed))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        if (onClosed)
            onClosed();
    }

private:
    std::function<void()> onClosed;
};
}

PluginHost::PluginHost(juce::PropertiesFile& s, juce::File scanCrashFile) : settings(s), crashFile(std::move(scanCrashFile))
{
    juce::addDefaultFormatsToManager(formats);
    if (auto xml = settings.getXmlValue(kKnownKey))
    {
        known.recreateFromXml(*xml);
        scannedOnce = true;
    }
}

PluginHost::~PluginHost()
{
    stopTimer();
    for (auto& e : registry->editors)
        e.reset();
    registry->editorOwner = {};
    registry->live = {};
    registry.reset();
}

bool PluginHost::handles(std::string_view typeId) const { return typeId.starts_with(kTypePrefix); }

std::string PluginHost::typeIdFor(const juce::PluginDescription& d) { return kTypePrefix + d.createIdentifierString().toStdString(); }

std::optional<juce::PluginDescription> PluginHost::describe(std::string_view typeId) const
{
    for (const auto& d : known.getTypes())
        if (typeIdFor(d) == typeId)
            return d;
    return std::nullopt;
}

PluginHost::InfoHolder& PluginHost::infoFor(const std::string& typeId, const juce::PluginDescription& d) const
{
    auto& holder = infos[typeId];
    if (holder == nullptr)
    {
        holder = std::make_unique<InfoHolder>();
        holder->typeId = typeId;
        holder->name = d.name.toStdString();
        holder->info.typeId = holder->typeId.c_str();
        holder->info.name = holder->name.c_str();
        for (auto& c : holder->info.controls)
            c.display = { dsp::DisplayMap::Curve::Hidden };
    }
    return *holder;
}

const dsp::ProcessorInfo* PluginHost::find(std::string_view typeId) const
{
    const auto d = describe(typeId);
    return d.has_value() ? &infoFor(std::string(typeId), *d).info : nullptr;
}

dsp::ProcessorPtr PluginHost::create(int slot, std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec)
{
    const auto d = describe(typeId);
    if (! d.has_value() || slot < 0 || slot >= engine::kNumFxSlots)
        return nullptr;
    juce::String error;
    auto instance = formats.createPluginInstance(*d, spec.sampleRate, spec.maxBlockSize, error);
    if (instance == nullptr)
    {
        status(d->name + " could not be opened: " + (error.isNotEmpty() ? error : juce::String("unknown error")), true);
        return nullptr;
    }
    if (! state.empty())
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding(state))
            instance->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }

    auto& holder = infoFor(std::string(typeId), *d);
    const auto params = controllableParameters(*instance);
    for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxControls); ++k)
    {
        auto& control = holder.info.controls[k];
        if (k < params.size())
        {
            holder.controlNames[k] = params[k]->getName(20).toStdString();
            control.name = holder.controlNames[k].c_str();
            control.defaultValue = params[k]->getDefaultValue();
            control.display = { dsp::DisplayMap::Curve::Linear, 0.0f, 100.0f, "%" };
        }
        else
        {
            control.name = "";
            control.display = { dsp::DisplayMap::Curve::Hidden };
        }
    }

    if (registry->editorOwner[static_cast<std::size_t>(slot)] != nullptr)
        closeEditor(slot);
    auto effect = std::make_unique<HostedPluginEffect>(registry, slot, std::move(instance), holder.info);
    effect->prepare(spec);
    registry->live[static_cast<std::size_t>(slot)] = effect.get();
    return effect;
}

std::string PluginHost::captureState(int slot) const
{
    if (slot < 0 || slot >= engine::kNumFxSlots)
        return {};
    auto* fx = registry->live[static_cast<std::size_t>(slot)];
    if (fx == nullptr)
        return {};
    juce::MemoryBlock block;
    fx->getInstance().getStateInformation(block);
    return block.toBase64Encoding().toStdString();
}

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : known.getTypes())
        if (! d.isInstrument)
            out.push_back(d);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        const int m = a.manufacturerName.compareIgnoreCase(b.manufacturerName);
        if (m != 0)
            return m < 0;
        const int n = a.name.compareIgnoreCase(b.name);
        return n != 0 ? n < 0 : a.pluginFormatName < b.pluginFormatName;
    });
    return out;
}

void PluginHost::startScan()
{
    if (isScanning())
        return;
    formatsToScan.clear();
    for (auto* f : formats.getFormats())
        formatsToScan.push_back(f);
    if (formatsToScan.empty())
    {
        status("This build cannot host plugins.", true);
        return;
    }
    progress = 0.0f;
    status("Looking for plugins...");
    beginNextFormat();
}

int PluginHost::scanNow(const juce::FileSearchPath& paths)
{
    const auto before = known.getNumTypes();
    for (auto* format : formats.getFormats())
    {
        juce::PluginDirectoryScanner direct(known, *format, paths, true, crashFile, false);
        juce::String name;
        while (direct.scanNextFile(true, name)) {}
    }
    scannedOnce = true;
    ++listVersion;
    if (auto xml = known.createXml())
        settings.setValue(kKnownKey, xml.get());
    return known.getNumTypes() - before;
}

void PluginHost::beginNextFormat()
{
    scanner.reset();
    if (formatsToScan.empty())
        return finishScan();
    auto* format = formatsToScan.front();
    formatsToScan.erase(formatsToScan.begin());
    scanner = std::make_unique<juce::PluginDirectoryScanner>(known, *format, format->getDefaultLocationsToSearch(), true, crashFile, false);
    startTimer(15);
}

void PluginHost::timerCallback()
{
    if (scanner == nullptr)
    {
        stopTimer();
        return;
    }
    const auto started = juce::Time::getMillisecondCounterHiRes();
    juce::String name;
    while (juce::Time::getMillisecondCounterHiRes() - started < 40.0)
    {
        name = scanner->getNextPluginFileThatWillBeScanned();
        if (! scanner->scanNextFile(true, name))
        {
            stopTimer();
            beginNextFormat();
            return;
        }
    }
    progress = scanner->getProgress();
    status("Looking for plugins: " + juce::File::createFileWithoutCheckingPath(name).getFileNameWithoutExtension());
}

void PluginHost::finishScan()
{
    stopTimer();
    scannedOnce = true;
    ++listVersion;
    if (auto xml = known.createXml())
        settings.setValue(kKnownKey, xml.get());
    settings.saveIfNeeded();
    const auto count = effects().size();
    status(count == 0 ? juce::String("No effect plugins were found.")
                      : "Found " + juce::String(static_cast<int>(count)) + " effect plugin" + (count == 1 ? "" : "s") + ".");
    if (onListChanged)
        onListChanged();
}

bool PluginHost::hasInstance(int slot) const
{
    return slot >= 0 && slot < engine::kNumFxSlots && registry->live[static_cast<std::size_t>(slot)] != nullptr;
}

void PluginHost::openEditor(int slot)
{
    if (! hasInstance(slot))
        return;
    const auto s = static_cast<std::size_t>(slot);
    if (registry->editors[s] != nullptr)
    {
        registry->editors[s]->toFront(true);
        return;
    }
    auto* fx = registry->live[s];
    auto& instance = fx->getInstance();
    juce::AudioProcessorEditor* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor(instance);
    std::weak_ptr<Registry> weak = registry;
    registry->editors[s] = std::make_unique<PluginWindow>(instance.getName() + " - " + engine::kFxSlots[s].name, editor, [weak, s] {
        juce::MessageManager::callAsync([weak, s] {
            if (auto r = weak.lock())
            {
                r->editors[s].reset();
                r->editorOwner[s] = nullptr;
            }
        });
    });
    registry->editorOwner[s] = fx;
}

void PluginHost::closeEditor(int slot)
{
    if (slot < 0 || slot >= engine::kNumFxSlots)
        return;
    registry->editors[static_cast<std::size_t>(slot)].reset();
    registry->editorOwner[static_cast<std::size_t>(slot)] = nullptr;
}

juce::String PluginHost::parameterText(int slot, int control, float normalised) const
{
    if (! hasInstance(slot))
        return {};
    const auto& mapped = registry->live[static_cast<std::size_t>(slot)]->getMapped();
    if (control < 0 || control >= static_cast<int>(mapped.size()))
        return {};
    auto* p = mapped[static_cast<std::size_t>(control)];
    const auto label = p->getLabel();
    return p->getText(normalised, 16) + (label.isNotEmpty() ? " " + label : juce::String());
}

float PluginHost::parameterValue(int slot, int control) const
{
    if (! hasInstance(slot))
        return -1.0f;
    const auto& mapped = registry->live[static_cast<std::size_t>(slot)]->getMapped();
    return control >= 0 && control < static_cast<int>(mapped.size()) ? mapped[static_cast<std::size_t>(control)]->getValue() : -1.0f;
}

void PluginHost::status(const juce::String& message, bool warning) const
{
    if (onStatus)
        onStatus(message, warning);
}

HostedPluginEffect::HostedPluginEffect(std::weak_ptr<PluginHost::Registry> r, int s, std::unique_ptr<juce::AudioPluginInstance> i,
                                       const dsp::ProcessorInfo& info)
    : registry(std::move(r)), slot(s), instance(std::move(i)), infoRef(info)
{
    mapped = controllableParameters(*instance);
    sent.fill(-1.0f);
}

HostedPluginEffect::~HostedPluginEffect()
{
    if (auto r = registry.lock())
    {
        const auto s = static_cast<std::size_t>(slot);
        if (r->editorOwner[s] == this)
        {
            r->editors[s].reset();
            r->editorOwner[s] = nullptr;
        }
        if (r->live[s] == this)
            r->live[s] = nullptr;
    }
    instance->releaseResources();
}

void HostedPluginEffect::prepare(const dsp::ProcessSpec& spec)
{
    instance->releaseResources();
    instance->disableNonMainBuses();
    const auto stereo = juce::AudioChannelSet::stereo(), mono = juce::AudioChannelSet::mono();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(stereo);
    layout.outputBuses.add(stereo);
    if (! instance->setBusesLayout(layout))
    {
        layout.inputBuses.getReference(0) = mono;
        layout.outputBuses.getReference(0) = stereo;
        if (! instance->setBusesLayout(layout))
        {
            layout.outputBuses.getReference(0) = mono;
            instance->setBusesLayout(layout);
        }
    }
    channels = std::max(instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
    usable = channels >= 1 && channels <= 2 && instance->getTotalNumOutputChannels() >= 1;
    maxBlock = std::max(1, spec.maxBlockSize);
    instance->setRateAndBufferSizeDetails(spec.sampleRate, maxBlock);
    instance->prepareToPlay(spec.sampleRate, maxBlock);
    scratch.setSize(2, maxBlock, false, true, true);
    midi.ensureSize(256);
}

void HostedPluginEffect::reset() noexcept { instance->reset(); }

void HostedPluginEffect::setControls(const std::array<float, 6>& c, const dsp::ModContext&) noexcept
{
    for (std::size_t k = 0; k < mapped.size() && k < c.size(); ++k)
        if (std::abs(c[k] - sent[k]) > 1.0e-4f)
        {
            sent[k] = c[k];
            mapped[k]->setValue(std::clamp(c[k], 0.0f, 1.0f));
        }
}

void HostedPluginEffect::process(float* left, float* right, int numSamples) noexcept
{
    if (! usable)
        return;
    for (int done = 0; done < numSamples; done += maxBlock)
    {
        const int n = std::min(maxBlock, numSamples - done);
        midi.clear();
        if (channels == 2)
        {
            float* data[2] = { left + done, right + done };
            juce::AudioBuffer<float> buffer(data, 2, n);
            instance->processBlock(buffer, midi);
        }
        else
        {
            float* mono = scratch.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                mono[i] = 0.5f * (left[done + i] + right[done + i]);
            float* data[1] = { mono };
            juce::AudioBuffer<float> buffer(data, 1, n);
            instance->processBlock(buffer, midi);
            std::copy_n(mono, n, left + done);
            std::copy_n(mono, n, right + done);
        }
    }
}

int HostedPluginEffect::getLatencySamples() const noexcept { return instance->getLatencySamples(); }

float HostedPluginEffect::getTailSeconds() const noexcept { return static_cast<float>(instance->getTailLengthSeconds()); }
}
