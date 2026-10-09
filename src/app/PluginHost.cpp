#include "PluginHost.h"

#include <algorithm>

namespace tf::app {
namespace {
constexpr const char* kKnownKey = "knownPlugins";
constexpr const char* kFoldersKey = "pluginFolders";
constexpr const char* kSystemFoldersKey = "pluginSystemFolders";
constexpr const char* kFormatKeyPrefix = "pluginFormat.";
constexpr int kMaxControls = 6;

constexpr const char* kChoicePrefix = "map=";

std::vector<juce::AudioProcessorParameter*> controllableParameters(juce::AudioPluginInstance& instance)
{
    std::vector<juce::AudioProcessorParameter*> out;
    const auto* bypass = instance.getBypassParameter();
    for (auto* p : instance.getParameters())
        if (p != bypass && p->isAutomatable() && ! p->isMetaParameter())
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

juce::String PluginHost::slotName(int slot)
{
    if (slot == kGuestSlot)
        return "Guest";
    return slot >= 0 && slot < engine::kNumFxSlots ? juce::String(engine::kFxSlots[static_cast<std::size_t>(slot)].name) : juce::String();
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::instantiate(const juce::PluginDescription& d, const std::string& state, std::string& choices,
                                                                   const dsp::ProcessSpec& spec, juce::String& error)
{
    auto instance = formats.createPluginInstance(d, spec.sampleRate, spec.maxBlockSize, error);
    if (instance == nullptr)
    {
        error = d.name + " could not be opened: " + (error.isNotEmpty() ? error : juce::String("unknown error"));
        return nullptr;
    }
    std::string pluginState = state;
    if (pluginState.rfind(kChoicePrefix, 0) == 0)
    {
        const auto end = pluginState.find(';');
        const auto from = std::char_traits<char>::length(kChoicePrefix);
        choices = pluginState.substr(from, end == std::string::npos ? std::string::npos : end - from);
        pluginState = end == std::string::npos ? std::string() : pluginState.substr(end + 1);
    }
    if (! pluginState.empty())
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding(pluginState))
            instance->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }
    return instance;
}

dsp::ProcessorPtr PluginHost::create(int slot, std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec)
{
    const auto d = describe(typeId);
    if (! d.has_value() || slot < 0 || slot >= engine::kNumFxSlots)
        return nullptr;
    std::string choices;
    juce::String error;
    auto instance = instantiate(*d, state, choices, spec, error);
    if (instance == nullptr)
    {
        status(error, true);
        return nullptr;
    }

    auto& holder = infoFor(std::string(typeId), *d);
    auto params = controllableParameters(*instance);
    if (params.size() > static_cast<std::size_t>(kMaxControls))
        params.resize(static_cast<std::size_t>(kMaxControls));
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
    if (! choices.empty())
        effect->applyChoices(choices);
    effect->prepare(spec);
    registry->live[static_cast<std::size_t>(slot)] = effect.get();
    ++loads[static_cast<std::size_t>(slot)];
    return effect;
}

const engine::InstrumentInfo* PluginHost::findInstrument(std::string_view typeId) const
{
    const auto d = describe(typeId);
    if (! d.has_value() || ! d->isInstrument)
        return nullptr;
    auto& info = instrumentInfos[std::string(typeId)];
    if (info == nullptr)
    {
        info = std::make_unique<engine::InstrumentInfo>();
        info->typeId = std::string(typeId);
        info->name = d->name.toStdString();
    }
    return info.get();
}

engine::InstrumentPtr PluginHost::createInstrument(std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec)
{
    const auto d = describe(typeId);
    if (! d.has_value() || ! d->isInstrument)
        return nullptr;
    std::string choices;
    juce::String error;
    auto instance = instantiate(*d, state, choices, spec, error);
    if (instance == nullptr)
    {
        status(error, true);
        return nullptr;
    }

    findInstrument(typeId);
    auto& info = *instrumentInfos[std::string(typeId)];
    if (registry->editorOwner[static_cast<std::size_t>(kGuestSlot)] != nullptr)
        closeEditor(kGuestSlot);
    auto instrument = std::make_unique<HostedInstrument>(registry, std::move(instance));
    if (! choices.empty())
        instrument->applyChoices(choices);
    for (int k = 0; k < kMaxControls; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        auto* p = instrument->mappedAt(k);
        info.controls[uk] = p != nullptr ? p->getName(20).toStdString() : std::string();
        info.defaults[uk] = p != nullptr ? p->getValue() : 0.5f;
    }
    instrument->prepare(spec);
    registry->live[static_cast<std::size_t>(kGuestSlot)] = instrument.get();
    ++loads[static_cast<std::size_t>(kGuestSlot)];
    return instrument;
}

std::shared_ptr<engine::Instrument> PluginHost::createRenderInstrument(std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec,
                                                                       juce::String& error)
{
    const auto d = describe(typeId);
    if (! d.has_value() || ! d->isInstrument)
    {
        error = "it is not installed here, or not found by a plugin scan yet";
        return nullptr;
    }
    std::string choices;
    auto instance = instantiate(*d, state, choices, spec, error);
    if (instance == nullptr)
        return nullptr;
    auto instrument = std::make_shared<HostedInstrument>(std::weak_ptr<Registry>(), std::move(instance), true);
    if (! choices.empty())
        instrument->applyChoices(choices);
    instrument->prepare(spec);
    return instrument;
}

std::uint64_t PluginHost::editRevision(int slot) const
{
    if (slot < 0 || slot >= kNumHostSlots)
        return 0;
    const auto* live = registry->live[static_cast<std::size_t>(slot)];
    return (static_cast<std::uint64_t>(loads[static_cast<std::size_t>(slot)]) << 32) | (live != nullptr ? live->getEdits() : 0u);
}

std::string PluginHost::captureState(int slot) const
{
    if (slot < 0 || slot >= kNumHostSlots)
        return {};
    auto* fx = registry->live[static_cast<std::size_t>(slot)];
    if (fx == nullptr)
        return {};
    juce::MemoryBlock block;
    fx->getInstance().getStateInformation(block);
    return std::string(kChoicePrefix) + fx->describeChoices() + ";" + block.toBase64Encoding().toStdString();
}

std::vector<juce::PluginDescription> PluginHost::effects() const { return listed(false); }

std::vector<juce::PluginDescription> PluginHost::instruments() const { return listed(true); }

std::vector<juce::PluginDescription> PluginHost::listed(bool instrumentsOnly) const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : known.getTypes())
        if (d.isInstrument == instrumentsOnly && isFormatEnabled(d.pluginFormatName))
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
        if (isFormatEnabled(f->getName()))
            formatsToScan.push_back(f);
    if (formatsToScan.empty())
    {
        status(formats.getNumFormats() == 0 ? "This build cannot host plugins." : "Every plugin format is turned off in Settings.", true);
        return;
    }
    progress = 0.0f;
    status("Looking for plugins...");
    beginNextFormat();
}

void PluginHost::clearAndRescan()
{
    if (isScanning())
        return;
    known.clear();
    ++listVersion;
    startScan();
}

juce::StringArray PluginHost::formatNames() const
{
    juce::StringArray names;
    for (auto* f : formats.getFormats())
        names.add(f->getName());
    return names;
}

bool PluginHost::isFormatEnabled(const juce::String& format) const { return settings.getBoolValue(kFormatKeyPrefix + format, true); }

void PluginHost::setFormatEnabled(const juce::String& format, bool enabled)
{
    settings.setValue(kFormatKeyPrefix + format, enabled);
    settings.saveIfNeeded();
    ++listVersion;
    if (onListChanged)
        onListChanged();
}

bool PluginHost::usesSystemFolders() const { return settings.getBoolValue(kSystemFoldersKey, true); }

void PluginHost::setUseSystemFolders(bool use)
{
    settings.setValue(kSystemFoldersKey, use);
    settings.saveIfNeeded();
}

juce::StringArray PluginHost::getCustomFolders() const
{
    auto folders = juce::StringArray::fromLines(settings.getValue(kFoldersKey));
    folders.removeEmptyStrings();
    return folders;
}

void PluginHost::setCustomFolders(const juce::StringArray& folders)
{
    settings.setValue(kFoldersKey, folders.joinIntoString("\n"));
    settings.saveIfNeeded();
}

juce::StringArray PluginHost::skippedPlugins() const { return known.getBlacklistedFiles(); }

void PluginHost::retrySkipped()
{
    known.clearBlacklistedFiles();
    crashFile.deleteFile();
    if (auto xml = known.createXml())
        settings.setValue(kKnownKey, xml.get());
    startScan();
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
    juce::FileSearchPath paths;
    if (usesSystemFolders())
        paths = format->getDefaultLocationsToSearch();
    if (format->canScanForPlugins() && format->getDefaultLocationsToSearch().getNumPaths() > 0)
        for (const auto& folder : getCustomFolders())
            if (juce::File::isAbsolutePath(folder) && juce::File(folder).isDirectory())
                paths.addIfNotAlreadyThere(juce::File(folder));
    scanner = std::make_unique<juce::PluginDirectoryScanner>(known, *format, paths, true, crashFile, false);
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
    const auto synths = instruments().size();
    if (count == 0 && synths == 0)
        status("No plugins were found.");
    else
        status("Found " + juce::String(static_cast<int>(count)) + " effect" + (count == 1 ? "" : "s") + " and " + juce::String(static_cast<int>(synths))
               + " instrument" + (synths == 1 ? "" : "s") + ".");
    if (onListChanged)
        onListChanged();
}

bool PluginHost::hasInstance(int slot) const
{
    return slot >= 0 && slot < kNumHostSlots && registry->live[static_cast<std::size_t>(slot)] != nullptr;
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
    registry->editors[s] = std::make_unique<PluginWindow>(instance.getName() + " - " + slotName(slot), editor, [weak, s] {
        juce::MessageManager::callAsync([weak, s] {
            if (auto r = weak.lock())
            {
                r->editors[s].reset();
                if (r->editorOwner[s] != nullptr)
                    r->editorOwner[s]->watchEdits(false);
                r->editorOwner[s] = nullptr;
            }
        });
    });
    registry->editorOwner[s] = fx;
    fx->watchEdits(true);
}

void PluginHost::closeEditor(int slot)
{
    if (slot < 0 || slot >= kNumHostSlots)
        return;
    registry->editors[static_cast<std::size_t>(slot)].reset();
    if (auto* owner = registry->editorOwner[static_cast<std::size_t>(slot)])
        owner->watchEdits(false);
    registry->editorOwner[static_cast<std::size_t>(slot)] = nullptr;
}

juce::String PluginHost::parameterText(int slot, int control, float normalised) const
{
    if (! hasInstance(slot))
        return {};
    auto* p = registry->live[static_cast<std::size_t>(slot)]->mappedAt(control);
    if (p == nullptr)
        return {};
    const auto label = p->getLabel();
    return p->getText(normalised, 16) + (label.isNotEmpty() ? " " + label : juce::String());
}

float PluginHost::parameterValue(int slot, int control) const
{
    if (! hasInstance(slot))
        return -1.0f;
    auto* p = registry->live[static_cast<std::size_t>(slot)]->mappedAt(control);
    return p != nullptr ? p->getValue() : -1.0f;
}

juce::String PluginHost::parameterName(int slot, int control) const
{
    if (! hasInstance(slot))
        return {};
    auto* p = registry->live[static_cast<std::size_t>(slot)]->mappedAt(control);
    return p != nullptr ? p->getName(20) : juce::String();
}

juce::StringArray PluginHost::parameterNames(int slot) const
{
    juce::StringArray names;
    if (hasInstance(slot))
        for (auto* p : registry->live[static_cast<std::size_t>(slot)]->getAll())
            names.add(p->getName(40));
    return names;
}

int PluginHost::chosenParameter(int slot, int control) const
{
    return hasInstance(slot) ? registry->live[static_cast<std::size_t>(slot)]->chosen(control) : -1;
}

void PluginHost::chooseParameter(int slot, int control, int index)
{
    if (! hasInstance(slot))
        return;
    auto* live = registry->live[static_cast<std::size_t>(slot)];
    if (live->chosen(control) == index)
        return;
    live->choose(control, index);
    ++loads[static_cast<std::size_t>(slot)];
}

void PluginHost::status(const juce::String& message, bool warning) const
{
    if (onStatus)
        onStatus(message, warning);
}

HostedPlugin::HostedPlugin(std::weak_ptr<PluginHost::Registry> r, int s, std::unique_ptr<juce::AudioPluginInstance> i)
    : registry(std::move(r)), slot(s), instance(std::move(i))
{
    instance->addListener(this);
    all = controllableParameters(*instance);
    for (int k = 0; k < kMaxControls; ++k)
    {
        choice[static_cast<std::size_t>(k)].store(k < static_cast<int>(all.size()) ? k : -1);
        applied[static_cast<std::size_t>(k)] = choice[static_cast<std::size_t>(k)].load();
    }
    sent.fill(-1.0f);
}

juce::AudioProcessorParameter* HostedPlugin::mappedAt(int control) const noexcept
{
    const int index = chosen(control);
    return index >= 0 && index < static_cast<int>(all.size()) ? all[static_cast<std::size_t>(index)] : nullptr;
}

int HostedPlugin::chosen(int control) const noexcept
{
    return control >= 0 && control < kMaxControls ? choice[static_cast<std::size_t>(control)].load(std::memory_order_acquire) : -1;
}

void HostedPlugin::choose(int control, int index) noexcept
{
    if (control >= 0 && control < kMaxControls && index >= -1 && index < static_cast<int>(all.size()))
        choice[static_cast<std::size_t>(control)].store(index, std::memory_order_release);
}

std::string HostedPlugin::describeChoices() const
{
    std::string text;
    for (int k = 0; k < kMaxControls; ++k)
        text += (k > 0 ? "," : "") + std::to_string(chosen(k));
    return text;
}

void HostedPlugin::applyChoices(const std::string& text) noexcept
{
    const auto parts = juce::StringArray::fromTokens(juce::String(text), ",", "");
    for (int k = 0; k < kMaxControls && k < parts.size(); ++k)
        if (parts[k].trim().containsOnly("-0123456789") && parts[k].isNotEmpty())
            choose(k, parts[k].getIntValue());
    for (int k = 0; k < kMaxControls; ++k)
        applied[static_cast<std::size_t>(k)] = chosen(k);
}

HostedPlugin::~HostedPlugin()
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
    instance->removeListener(this);
    instance->releaseResources();
}

void HostedPlugin::audioProcessorParameterChanged(juce::AudioProcessor*, int, float)
{
    if (watching.load(std::memory_order_relaxed))
        edits.fetch_add(1, std::memory_order_relaxed);
}

void HostedPlugin::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details)
{
    if (watching.load(std::memory_order_relaxed) && (details.programChanged || details.nonParameterStateChanged))
        edits.fetch_add(1, std::memory_order_relaxed);
}

void HostedPlugin::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) { edits.fetch_add(1, std::memory_order_relaxed); }

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

HostedPluginEffect::HostedPluginEffect(std::weak_ptr<PluginHost::Registry> r, int s, std::unique_ptr<juce::AudioPluginInstance> i,
                                       const dsp::ProcessorInfo& info)
    : HostedPlugin(std::move(r), s, std::move(i)), infoRef(info)
{
}

void HostedPluginEffect::setControls(const std::array<float, 6>& c, const dsp::ModContext&) noexcept { sendControls(c); }

void HostedPlugin::sendControls(const std::array<float, 6>& c) noexcept
{
    for (std::size_t k = 0; k < c.size(); ++k)
    {
        const int index = choice[k].load(std::memory_order_acquire);
        if (index < 0 || index >= static_cast<int>(all.size()))
            continue;
        if (index != applied[k])
        {
            applied[k] = index;
            sent[k] = c[k];
            continue;
        }
        if (std::abs(c[k] - sent[k]) > 1.0e-4f)
        {
            sent[k] = c[k];
            all[static_cast<std::size_t>(index)]->setValue(std::clamp(c[k], 0.0f, 1.0f));
        }
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

namespace tf::app {
HostedInstrument::HostedInstrument(std::weak_ptr<PluginHost::Registry> r, std::unique_ptr<juce::AudioPluginInstance> i, bool renderOffline)
    : HostedPlugin(std::move(r), PluginHost::kGuestSlot, std::move(i)), offline(renderOffline)
{
}

void HostedInstrument::prepare(const dsp::ProcessSpec& spec)
{
    instance->releaseResources();
    instance->disableNonMainBuses();
    const auto stereo = juce::AudioChannelSet::stereo(), mono = juce::AudioChannelSet::mono();
    auto layoutWith = [&](bool input, const juce::AudioChannelSet& out) {
        juce::AudioProcessor::BusesLayout layout;
        for (int b = 0; b < instance->getBusCount(true); ++b)
            layout.inputBuses.add(input && b == 0 ? out : juce::AudioChannelSet::disabled());
        layout.outputBuses.add(out);
        for (int b = 1; b < instance->getBusCount(false); ++b)
            layout.outputBuses.add(juce::AudioChannelSet::disabled());
        return instance->setBusesLayout(layout);
    };
    if (! layoutWith(false, stereo) && ! layoutWith(true, stereo) && ! layoutWith(false, mono))
        layoutWith(true, mono);
    outputs = instance->getTotalNumOutputChannels();
    channels = std::max(instance->getTotalNumInputChannels(), outputs);
    usable = outputs >= 1 && channels <= kMaxChannels;
    maxBlock = std::max(1, spec.maxBlockSize);
    instance->setNonRealtime(offline);
    instance->setRateAndBufferSizeDetails(spec.sampleRate, maxBlock);
    instance->prepareToPlay(spec.sampleRate, maxBlock);
    scratch.setSize(std::max(1, channels), maxBlock, false, true, true);
    midi.ensureSize(kMidiBytes);
}

void HostedInstrument::reset() noexcept { instance->reset(); }

void HostedInstrument::process(const engine::GuestEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept
{
    if (! usable)
        return;
    for (int done = 0; done < numSamples; done += maxBlock)
    {
        const int n = std::min(maxBlock, numSamples - done);
        const bool last = done + n >= numSamples;
        midi.clear();
        for (int k = 0; k < numEvents; ++k)
        {
            const auto& e = events[k];
            const int at = static_cast<int>(e.offset) - done;
            if (at < 0 || (at >= n && ! last))
                continue;
            const std::uint8_t bytes[3] = { e.status, e.data1, e.data2 };
            midi.addEvent(bytes, e.type() == 0xc0 || e.type() == 0xd0 ? 2 : 3, std::min(at, n - 1));
        }
        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::clear(scratch.getWritePointer(c), n);
        juce::AudioBuffer<float> buffer(scratch.getArrayOfWritePointers(), channels, n);
        instance->processBlock(buffer, midi);
        std::copy_n(scratch.getReadPointer(0), n, left + done);
        std::copy_n(scratch.getReadPointer(outputs > 1 ? 1 : 0), n, right + done);
    }
}

int HostedInstrument::getLatencySamples() const noexcept { return instance->getLatencySamples(); }
}
