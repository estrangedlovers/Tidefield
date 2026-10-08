#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>

namespace {
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
}

struct Result
{
    bool finite = true;
    float peak = 0.0f;
    double rmsDb = -200.0;
};

Result render(juce::AudioPluginInstance& p, double rate, std::initializer_list<int> blockSizes, double seconds)
{
    const int maxBlock = *std::max_element(blockSizes.begin(), blockSizes.end());
    p.releaseResources();
    p.setRateAndBufferSizeDetails(rate, maxBlock);
    p.prepareToPlay(rate, maxBlock);

    juce::AudioBuffer<float> buffer(std::max(2, p.getTotalNumInputChannels()), maxBlock);
    juce::MidiBuffer midi;
    Result r;
    double energy = 0.0;
    std::int64_t done = 0, total = static_cast<std::int64_t>(seconds * rate);
    std::size_t k = 0;
    bool noteSent = false;
    const std::vector<int> sizes(blockSizes);
    while (done < total)
    {
        const int n = static_cast<int>(std::min<std::int64_t>(sizes[k++ % sizes.size()], total - done));
        buffer.setSize(buffer.getNumChannels(), n, false, false, true);
        buffer.clear();
        midi.clear();
        if (! noteSent && done > static_cast<std::int64_t>(0.3 * rate))
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, 62, 0.8f), 0);
            noteSent = true;
        }
        p.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = buffer.getSample(ch, i);
                r.finite = r.finite && std::isfinite(v);
                r.peak = std::max(r.peak, std::abs(v));
                energy += static_cast<double>(v) * v;
            }
        done += n;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
    }
    r.rmsDb = 10.0 * std::log10(energy / (2.0 * static_cast<double>(total)) + 1.0e-20);
    return r;
}

std::map<juce::String, juce::int64> sessionEntries(const juce::MemoryBlock& state, juce::var& json)
{
    juce::MemoryBlock zipData;
    const auto* bytes = static_cast<const char*>(state.getData());
    const auto size = state.getSize();
    if (size > 8 && juce::String(juce::CharPointer_UTF8(bytes), 4) == "VC2!")
    {
        const auto text = juce::String::fromUTF8(bytes + 8, static_cast<int>(size - 8));
        const auto start = text.indexOf("<IComponent>");
        if (start >= 0)
            zipData.fromBase64Encoding(text.substring(start + 12, text.indexOf("</IComponent>")));
    }
    else
    {
        const juce::MemoryBlock pk("PK\x03\x04", 4);
        for (size_t i = 0; i + 4 <= size && zipData.isEmpty(); ++i)
            if (std::memcmp(bytes + i, pk.getData(), 4) == 0)
                zipData.replaceAll(bytes + i, size - i);
        if (zipData.isEmpty())
        {
            const auto text = juce::String::fromUTF8(bytes, static_cast<int>(size));
            const auto key = text.indexOf("jucePluginState");
            const auto from = text.indexOf(key, "<data>");
            if (key >= 0 && from >= 0)
            {
                juce::MemoryOutputStream decoded;
                juce::Base64::convertFromBase64(decoded, text.substring(from + 6, text.indexOf(from, "</data>")).removeCharacters(" \t\r\n"));
                zipData = decoded.getMemoryBlock();
            }
        }
    }
    std::map<juce::String, juce::int64> entries;
    if (zipData.isEmpty())
        return {};
    juce::MemoryInputStream stream(zipData, false);
    juce::ZipFile zip(stream);
    for (int i = 0; i < zip.getNumEntries(); ++i)
        entries[zip.getEntry(i)->filename] = zip.getEntry(i)->uncompressedSize;
    if (const auto* e = zip.getEntry("session.json"))
        json = juce::JSON::parse(std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(*e))->readEntireStreamAsString());
    return entries;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: tidefield_plugincheck <plugin.vst3 | AudioUnit:Synths/aumu,Tdfl,Tdfd> [--editor]\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager(formats);

    const juce::String arg(argv[1]);
    const juce::String path = arg.startsWith("AudioUnit:") ? arg : juce::File::getCurrentWorkingDirectory().getChildFile(arg).getFullPathName();
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : formats.getFormats())
        f->findAllTypesForFile(found, path);
    check(! found.isEmpty(), "plugin found in " + path);
    if (found.isEmpty())
        return 1;
    const auto desc = *found[0];
    check(desc.isInstrument, "registers as an instrument (" + desc.pluginFormatName + ")");

    juce::String error;
    auto plugin = formats.createPluginInstance(desc, 48000.0, 512, error);
    check(plugin != nullptr, "instantiates" + (error.isNotEmpty() ? ": " + error : juce::String()));
    if (plugin == nullptr)
        return 1;
    check(plugin->acceptsMidi(), "accepts MIDI");
    check(plugin->getTotalNumOutputChannels() == 2, "stereo out");

    const float ceiling = std::pow(10.0f, -1.0f / 20.0f) + 1.0e-4f;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto r = render(*plugin, rate, { 512, 37, 1024, 128, 1 }, 10.0);
        const auto at = juce::String(rate / 1000.0, 1) + " kHz";
        check(r.finite, "finite at " + at + " with odd block sizes");
        check(r.peak <= ceiling, "under the ceiling at " + at + " (peak " + juce::String(juce::Decibels::gainToDecibels(r.peak), 1) + " dB)");
        check(r.rmsDb > -60.0, "audible at " + at + " (" + juce::String(r.rmsDb, 1) + " dB RMS)");
        check(plugin->getLatencySamples() >= 0 && plugin->getLatencySamples() < static_cast<int>(rate * 0.05),
              "latency reported: " + juce::String(plugin->getLatencySamples()) + " samples");
    }

    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    check(state.getSize() > 1000, "saves its state (" + juce::String(static_cast<int>(state.getSize() / 1024)) + " KB, sounds included)");

    auto second = formats.createPluginInstance(desc, 48000.0, 512, error);
    check(second != nullptr, "second instance");
    if (second != nullptr)
    {
        second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        juce::MemoryBlock again;
        second->getStateInformation(again);
        if (auto dump = juce::SystemStats::getEnvironmentVariable("TF_DUMP_STATE", {}); dump.isNotEmpty())
        {
            juce::File(dump + "/a.zip").replaceWithData(state.getData(), state.getSize());
            juce::File(dump + "/b.zip").replaceWithData(again.getData(), again.getSize());
        }
        juce::var a, b;
        const auto ea = sessionEntries(state, a), eb = sessionEntries(again, b);
        if (ea.empty())
            std::cout << "      state begins: " << juce::String::toHexString(state.getData(), static_cast<int>(std::min<size_t>(64, state.getSize())))
                      << "\n      as text: " << juce::String::fromUTF8(static_cast<const char*>(state.getData()), static_cast<int>(std::min<size_t>(300, state.getSize()))).replaceCharacters("\r\n", "  ") << std::endl;
        auto names = [](const std::map<juce::String, juce::int64>& m) {
            juce::StringArray n;
            for (const auto& [name, size] : m)
                n.add(name);
            return n;
        };
        auto same = [&](const char* key) { return juce::JSON::toString(a.getProperty(key, {})) == juce::JSON::toString(b.getProperty(key, {})); };
        check(! ea.empty() && names(ea) == names(eb) && same("samples"), "restored state carries the same sounds (" + names(ea).joinIntoString(", ") + ")");
        check(same("scenes") && same("fx") && same("path"), "restored state carries the same scenes and effects");
        int params = 0, matching = 0;
        if (auto* pa = a.getProperty("params", {}).getDynamicObject())
            for (const auto& prop : pa->getProperties())
            {
                ++params;
                const double va = prop.value, vb = b.getProperty("params", {}).getProperty(prop.name, 1.0e9);
                matching += std::abs(va - vb) < 1.0e-3 * std::max(1.0, std::abs(va)) ? 1 : 0;
            }
        check(params > 100 && matching == params, "restored state carries every parameter (" + juce::String(matching) + " of " + juce::String(params) + ")");
        const auto r = render(*second, 48000.0, { 256 }, 4.0);
        check(r.finite && r.peak <= ceiling, "restored instance plays cleanly");
        second->releaseResources();
    }

    if (argc > 2 && juce::String(argv[2]) == "--ui-load")
    {
        const bool withEditor = argc > 3 && juce::String(argv[3]) == "on";
        std::unique_ptr<juce::AudioProcessorEditor> editor(withEditor ? plugin->createEditorAndMakeActive() : nullptr);
        if (editor != nullptr)
        {
            editor->setVisible(true);
            editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
        }
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        const auto cpuStart = std::clock();
        const double start = juce::Time::getMillisecondCounterHiRes();
        double audioMs = 0.0;
        for (int block = 0; block < 48000 * 20 / 512; ++block)
        {
            buffer.clear();
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            plugin->processBlock(buffer, midi);
            audioMs += juce::Time::getMillisecondCounterHiRes() - t0;
            midi.clear();
            const double due = start + (block + 1) * 512.0 / 48.0;
            while (juce::Time::getMillisecondCounterHiRes() < due)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
        const double wall = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        const double cpu = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        std::cout << "ui-load editor=" << (withEditor ? "on" : "off") << "  process CPU " << juce::String(100.0 * cpu / wall, 1)
                  << "% of a core over " << juce::String(wall, 1) << " s (audio " << juce::String(audioMs / 10.0 / wall, 1) << "%)" << std::endl;
        editor.reset();
        plugin.reset();
        second.reset();
        return 0;
    }

    if (argc > 2 && juce::String(argv[2]) == "--editor")
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorAndMakeActive());
        check(editor != nullptr && editor->getWidth() >= 1100, "editor opens");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        editor.reset();
    }

    plugin->releaseResources();
    plugin.reset();
    second.reset();
    std::cout << (failures == 0 ? "Plugin check passed" : "Plugin check FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
