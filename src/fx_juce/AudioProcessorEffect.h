#pragma once

#include <dsp/fx/ProcessorFactory.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace tf::fxjuce {

/** Hosts a juce::AudioProcessor (the processor class of one of your own JUCE
    plugins, compiled into the app) in any Tidefield FX slot.

    Threading follows Tidefield's rules: the audio thread only copies the slot's six
    controls into atomics and calls processBlock with the slot's buffers (wrapped,
    not copied, so nothing is allocated). Parameter changes reach the plugin on the
    message thread (a 60 Hz timer calling setValueNotifyingHost), because JUCE's
    listener notification takes locks that must never be taken on the audio thread.
    Knob moves therefore land up to ~16 ms late; the plugin's own smoothing hides it.

    The six controls map to the parameters named at registration, or to the plugin's
    first six parameters. */
template <typename AudioProcessorType>
class AudioProcessorEffect final : public dsp::Processor, private juce::Timer
{
public:
    AudioProcessorEffect(const dsp::ProcessorInfo& i, const std::vector<std::string>& parameterIds)
        : infoRef(i), processor(std::make_unique<AudioProcessorType>())
    {
        const auto& all = processor->getParameters();
        for (const auto& id : parameterIds)
            for (auto* p : all)
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId != nullptr && withId->paramID == juce::String(id))
                    mapped.push_back(p);
        if (parameterIds.empty())
            for (int k = 0; k < std::min(6, all.size()); ++k)
                mapped.push_back(all[k]);
        for (auto& v : values)
            v.store(-1.0f);
        midi.ensureSize(512);
        startTimerHz(60);
    }

    ~AudioProcessorEffect() override
    {
        stopTimer();
        processor->releaseResources();
    }

    const dsp::ProcessorInfo& info() const noexcept override { return infoRef; }

    void prepare(const dsp::ProcessSpec& spec) override
    {
        processor->setPlayConfigDetails(2, 2, spec.sampleRate, spec.maxBlockSize);
        processor->setRateAndBufferSizeDetails(spec.sampleRate, spec.maxBlockSize);
        processor->prepareToPlay(spec.sampleRate, spec.maxBlockSize);
        maxBlock = spec.maxBlockSize;
    }

    void reset() noexcept override { processor->reset(); }

    void setControls(const std::array<float, 6>& c, const dsp::ModContext&) noexcept override
    {
        for (std::size_t k = 0; k < c.size(); ++k)
            values[k].store(c[k], std::memory_order_relaxed);
    }

    void process(float* left, float* right, int numSamples) noexcept override
    {
        float* channels[2] = { left, right };
        for (int done = 0; done < numSamples; done += maxBlock)
        {
            const int n = std::min(maxBlock, numSamples - done);
            float* offset[2] = { channels[0] + done, channels[1] + done };
            juce::AudioBuffer<float> buffer(offset, 2, n); // refers to the slot's data
            midi.clear();
            processor->processBlock(buffer, midi);
        }
    }

    int getLatencySamples() const noexcept override { return processor->getLatencySamples(); }
    float getTailSeconds() const noexcept override { return static_cast<float>(processor->getTailLengthSeconds()); }

    /** Pushes pending control values to the plugin's parameters (the timer does this;
        tests and offline renders without a message loop call it directly). */
    void flushParameters()
    {
        for (std::size_t k = 0; k < mapped.size() && k < values.size(); ++k)
        {
            const float v = values[k].load(std::memory_order_relaxed);
            if (v >= 0.0f && std::abs(v - mapped[k]->getValue()) > 1.0e-5f)
                mapped[k]->setValueNotifyingHost(v);
        }
    }

    AudioProcessorType& getProcessor() noexcept { return *processor; }

private:
    void timerCallback() override { flushParameters(); }

    const dsp::ProcessorInfo& infoRef;
    std::unique_ptr<AudioProcessorType> processor;
    std::vector<juce::AudioProcessorParameter*> mapped;
    std::array<std::atomic<float>, 6> values;
    juce::MidiBuffer midi;
    int maxBlock = 512;
};

/** Registers an AudioProcessor class as an FX type. Call once at startup, on the
    message thread, before sessions load (see registerUserEffects()).

      typeId        stable id stored in sessions ("user.shimmer"); never rename it
      name          shown in the FX menus
      sendStyle     true for effects meant to sit fully wet on a send bus
      parameterIds  which of the plugin's parameters the six slot controls drive (by
                    parameter ID); empty = its first six parameters

    Control names and defaults come from the plugin's own parameters. */
template <typename AudioProcessorType>
void registerAudioProcessorEffect(const char* typeId, const char* name, bool sendStyle,
                                  std::initializer_list<const char*> parameterIds = {})
{
    // One ProcessorInfo per registered class, living for the whole program.
    struct Registration
    {
        dsp::ProcessorInfo info;
        std::vector<std::string> ids, controlNames;
        std::string typeId, name;
    };
    static Registration reg;
    reg.typeId = typeId;
    reg.name = name;
    reg.ids.assign(parameterIds.begin(), parameterIds.end());
    reg.controlNames.clear();
    reg.controlNames.reserve(6); // the info keeps pointers into these strings

    const auto probe = std::make_unique<AudioProcessorType>(); // only to read its parameters
    const auto& all = probe->getParameters();
    std::vector<juce::AudioProcessorParameter*> chosen;
    for (const auto& id : reg.ids)
        for (auto* p : all)
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId != nullptr && withId->paramID == juce::String(id))
                chosen.push_back(p);
    if (reg.ids.empty())
        for (int k = 0; k < std::min(6, all.size()); ++k)
            chosen.push_back(all[k]);

    reg.info.typeId = reg.typeId.c_str();
    reg.info.name = reg.name.c_str();
    reg.info.sendStyle = sendStyle;
    for (std::size_t k = 0; k < 6; ++k)
    {
        auto& control = reg.info.controls[k];
        if (k < chosen.size())
        {
            reg.controlNames.push_back(chosen[k]->getName(24).toStdString());
            control.name = reg.controlNames.back().c_str();
            control.defaultValue = chosen[k]->getDefaultValue();
            control.display = { dsp::DisplayMap::Curve::Linear, 0.0f, 100.0f, "%" };
        }
        else
        {
            control.name = "";
            control.display = { dsp::DisplayMap::Curve::Hidden };
        }
    }
    dsp::ProcessorFactory::instance().add(reg.info, [] {
        return dsp::ProcessorPtr(new AudioProcessorEffect<AudioProcessorType>(reg.info, reg.ids));
    });
}

/** Registers the user's own JUCE effects (UserEffects.cpp). */
void registerUserEffects();

} // namespace tf::fxjuce
