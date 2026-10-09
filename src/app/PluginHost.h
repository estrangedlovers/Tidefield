#pragma once

#include <engine/guest/Instrument.h>
#include <engine/mix/FxManager.h>
#include <engine/mix/Layout.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::app {
class HostedPlugin;

class PluginHost final : public engine::ExternalEffects, public engine::ExternalInstruments, private juce::Timer
{
public:
    static constexpr const char* kTypePrefix = "plugin:";
    static constexpr int kGuestSlot = engine::kNumFxSlots;
    static constexpr int kNumHostSlots = engine::kNumFxSlots + 1;

    PluginHost(juce::PropertiesFile& settings, juce::File scanCrashFile);
    ~PluginHost() override;

    bool handles(std::string_view typeId) const override;
    const dsp::ProcessorInfo* find(std::string_view typeId) const override;
    dsp::ProcessorPtr create(int slot, std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec) override;
    std::string captureState(int slot) const override;

    bool handlesInstrument(std::string_view typeId) const override { return handles(typeId); }
    const engine::InstrumentInfo* findInstrument(std::string_view typeId) const override;
    engine::InstrumentPtr createInstrument(std::string_view typeId, const std::string& state, const dsp::ProcessSpec& spec) override;
    std::string captureInstrumentState() const override { return captureState(kGuestSlot); }

    static std::string typeIdFor(const juce::PluginDescription& d);
    static juce::String slotName(int slot);
    std::vector<juce::PluginDescription> effects() const;
    std::vector<juce::PluginDescription> instruments() const;
    bool hasScanned() const { return scannedOnce; }
    int getListVersion() const noexcept { return listVersion; }

    void startScan();
    void clearAndRescan();
    int scanNow(const juce::FileSearchPath& paths);

    juce::StringArray formatNames() const;
    bool isFormatEnabled(const juce::String& format) const;
    void setFormatEnabled(const juce::String& format, bool enabled);
    bool usesSystemFolders() const;
    void setUseSystemFolders(bool use);
    juce::StringArray getCustomFolders() const;
    void setCustomFolders(const juce::StringArray& folders);
    juce::StringArray skippedPlugins() const;
    void retrySkipped();
    int numEffects() const { return static_cast<int>(effects().size()); }
    int numInstruments() const { return static_cast<int>(instruments().size()); }
    bool isScanning() const noexcept { return scanner != nullptr || ! formatsToScan.empty(); }
    float scanProgress() const noexcept { return progress; }

    bool hasInstance(int slot) const;
    void openEditor(int slot);
    void closeEditor(int slot);
    juce::String parameterText(int slot, int control, float normalised) const;
    float parameterValue(int slot, int control) const;
    juce::String parameterName(int slot, int control) const;
    juce::StringArray parameterNames(int slot) const;
    int chosenParameter(int slot, int control) const;
    void chooseParameter(int slot, int control, int index);

    std::function<void(const juce::String&, bool warning)> onStatus;
    std::function<void()> onListChanged;

    struct Registry
    {
        std::array<HostedPlugin*, kNumHostSlots> live {};
        std::array<HostedPlugin*, kNumHostSlots> editorOwner {};
        std::array<std::unique_ptr<juce::DocumentWindow>, kNumHostSlots> editors;
    };

private:
    struct InfoHolder
    {
        dsp::ProcessorInfo info;
        std::string typeId, name;
        std::array<std::string, 6> controlNames;
    };

    void timerCallback() override;
    void beginNextFormat();
    void finishScan();
    std::optional<juce::PluginDescription> describe(std::string_view typeId) const;
    InfoHolder& infoFor(const std::string& typeId, const juce::PluginDescription& d) const;
    std::unique_ptr<juce::AudioPluginInstance> instantiate(const juce::PluginDescription& d, const std::string& state, std::string& choices,
                                                           const dsp::ProcessSpec& spec);
    std::vector<juce::PluginDescription> listed(bool instrumentsOnly) const;
    void status(const juce::String& message, bool warning = false) const;

    juce::PropertiesFile& settings;
    juce::File crashFile;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    mutable std::map<std::string, std::unique_ptr<InfoHolder>> infos;
    mutable std::map<std::string, std::unique_ptr<engine::InstrumentInfo>> instrumentInfos;
    std::shared_ptr<Registry> registry = std::make_shared<Registry>();
    std::vector<juce::AudioPluginFormat*> formatsToScan;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    float progress = 0.0f;
    bool scannedOnce = false;
    int listVersion = 0;
};

class HostedPlugin
{
public:
    HostedPlugin(std::weak_ptr<PluginHost::Registry> registry, int slot, std::unique_ptr<juce::AudioPluginInstance> instance);
    virtual ~HostedPlugin();

    HostedPlugin(const HostedPlugin&) = delete;
    HostedPlugin& operator=(const HostedPlugin&) = delete;

    juce::AudioPluginInstance& getInstance() noexcept { return *instance; }
    const std::vector<juce::AudioProcessorParameter*>& getAll() const noexcept { return all; }
    juce::AudioProcessorParameter* mappedAt(int control) const noexcept;
    int chosen(int control) const noexcept;
    void choose(int control, int index) noexcept;
    std::string describeChoices() const;
    void applyChoices(const std::string& text) noexcept;

protected:
    void sendControls(const std::array<float, 6>& controls) noexcept;

    std::weak_ptr<PluginHost::Registry> registry;
    int slot;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::vector<juce::AudioProcessorParameter*> all;
    std::array<std::atomic<int>, 6> choice {};
    std::array<int, 6> applied {};
    std::array<float, 6> sent {};
};

class HostedPluginEffect final : public dsp::Processor, public HostedPlugin
{
public:
    HostedPluginEffect(std::weak_ptr<PluginHost::Registry> registry, int slot, std::unique_ptr<juce::AudioPluginInstance> instance,
                       const dsp::ProcessorInfo& info);

    const dsp::ProcessorInfo& info() const noexcept override { return infoRef; }
    void prepare(const dsp::ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& controls, const dsp::ModContext& ctx) noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    int getLatencySamples() const noexcept override;
    float getTailSeconds() const noexcept override;

private:
    const dsp::ProcessorInfo& infoRef;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    int maxBlock = 512;
    int channels = 2;
    bool usable = true;
};

class HostedInstrument final : public engine::Instrument, public HostedPlugin
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kMidiBytes = 8192;

    HostedInstrument(std::weak_ptr<PluginHost::Registry> registry, std::unique_ptr<juce::AudioPluginInstance> instance);

    void prepare(const dsp::ProcessSpec& spec) override;
    void reset() noexcept override;
    void setControls(const std::array<float, 6>& controls) noexcept override { sendControls(controls); }
    void process(const engine::GuestEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept override;
    int getLatencySamples() const noexcept override;

private:
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    int maxBlock = 128;
    int channels = 2;
    int outputs = 2;
    bool usable = true;
};
}
