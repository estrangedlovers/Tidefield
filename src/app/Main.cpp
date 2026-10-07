#include "AppCore.h"
#include "AudioHost.h"
#include "ClassicUI.h"
#include "web/WebUI.h"

#include <AudioProcessorEffect.h>
#include <BinaryData.h>
#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <io/AudioFileIO.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <iostream>

namespace tf::app {

namespace {

/** `Tidefield --self-test`: checks the shipped app without a window or an audio
    device (CI runs it on the built bundle). The bundled UI must be present, the
    factory sounds must decode, and a few seconds of the full engine with every
    effect type loaded must render finite, audible and under the ceiling. */
int runSelfTest()
{
    int failures = 0;
    auto check = [&](bool ok, const juce::String& what) {
        std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    };

    const auto uiRoot = WebUI::findUiRoot();
    check(uiRoot.has_value() && uiRoot->getChildFile("index.html").existsAsFile(), "bundled UI present");
    if (uiRoot.has_value())
    {
        const auto html = uiRoot->getChildFile("index.html").loadFileAsString();
        const auto script = html.fromFirstOccurrenceOf("src=\"./", false, false).upToFirstOccurrenceOf("\"", false, false);
        check(script.isNotEmpty() && uiRoot->getChildFile(script).existsAsFile(), "UI script bundle present: " + script);
    }

    fxjuce::registerUserEffects();
    engine::Engine engine;
    engine.prepare(48000.0, 512);
    engine::FxManager fx(engine);
    fx.loadDefaultLayout();

    struct Res { const char* data; int size; const char* name; };
    const Res samples[] = { { BinaryData::glass_wav, BinaryData::glass_wavSize, "glass" },
                            { BinaryData::chord_wav, BinaryData::chord_wavSize, "chord" },
                            { BinaryData::pluck_wav, BinaryData::pluck_wavSize, "pluck" },
                            { BinaryData::breath_wav, BinaryData::breath_wavSize, "breath" } };
    int cloud = 0;
    for (const auto& r : samples)
    {
        juce::String error;
        std::shared_ptr<const dsp::SampleBuffer> b(io::loadSample(std::make_unique<juce::MemoryInputStream>(r.data, static_cast<size_t>(r.size), false), r.name, error));
        check(b != nullptr && b->size() > 1000, juce::String("factory sound decodes: ") + r.name);
        if (b != nullptr)
        {
            engine.loadCloudSample(cloud++, b);
            if (cloud == 1)
                engine.loadBloomSample(b);
        }
    }

    // Every effect type in some slot, every source on.
    const auto& types = dsp::ProcessorFactory::instance().entries();
    for (std::size_t k = 0; k < types.size(); ++k)
        fx.setType(static_cast<int>(k % engine::kNumStrips) * 2, types[k].info->typeId, false);
    using engine::P;
    engine.setParam(P::MasterFadeSecs, 0.5f);
    engine.setParam(P::MasterAuto, 1.0f);
    engine.setParam(P::WeatherWind, 0.5f);
    engine.setParam(P::LoopsOn, 1.0f);
    engine.setParam(P::LoopsRate, 4.0f);
    engine.command(engine::Command::FadeIn);
    engine.noteOn(62, 0.8f);

    std::vector<float> l(512), r(512);
    float* outs[2] = { l.data(), r.data() };
    bool finite = true;
    float peak = 0.0f;
    double energy = 0.0;
    for (int b = 0; b < 48000 * 6 / 512; ++b)
    {
        fx.tick();
        engine.process(nullptr, 0, outs, 2, 512);
        for (int i = 0; i < 512; ++i)
        {
            finite = finite && std::isfinite(l[static_cast<size_t>(i)]) && std::isfinite(r[static_cast<size_t>(i)]);
            peak = std::max({ peak, std::fabs(l[static_cast<size_t>(i)]), std::fabs(r[static_cast<size_t>(i)]) });
            energy += static_cast<double>(l[static_cast<size_t>(i)]) * l[static_cast<size_t>(i)];
        }
        engine::TelemetryFrame f;
        while (engine.popTelemetry(f)) {}
        engine.collectGarbage();
    }
    const double rmsDb = 10.0 * std::log10(energy / (48000.0 * 6.0) + 1.0e-20);
    check(finite, "render is finite");
    check(peak <= dsp::dbToGain(-1.0f) + 1.0e-5f, "render stays under the -1 dBFS ceiling (peak " + juce::String(juce::Decibels::gainToDecibels(peak), 1) + " dB)");
    check(rmsDb > -50.0, "render is audible (" + juce::String(rmsDb, 1) + " dB RMS)");

    std::cout << (failures == 0 ? "Self-test passed" : "Self-test FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

} // namespace

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, juce::Colour(0xff0d1014), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
        setResizeLimits(1100, 720, 10000, 10000);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class TidefieldApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; } // two copies would fight over the audio device

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--self-test"))
        {
            setApplicationReturnValue(runSelfTest());
            quit();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "Tidefield";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "Tidefield";
        settings.setStorageParameters(options);

        host = std::make_unique<AudioHost>(*settings.getUserSettings());
        core = std::make_unique<AppCore>(*host);

        // The web UI is the instrument's face; the JUCE panel is the fallback when the
        // built UI is missing, or on request (--classic or TIDEFIELD_CLASSIC_UI=1).
        const bool forceClassic = commandLine.contains("--classic")
                                  || juce::SystemStats::getEnvironmentVariable("TIDEFIELD_CLASSIC_UI", {}) == "1";
        juce::Component* content = nullptr;
        const auto uiRoot = WebUI::findUiRoot();
        if (! forceClassic && (uiRoot.has_value() || WebUI::devServerUrl().isNotEmpty()))
            content = new WebUI(*core, uiRoot.value_or(juce::File()));
        else
            content = new ClassicUI(*core);

        window = std::make_unique<MainWindow>(getApplicationName() + " - " + core->session.getName(), content);
    }

    void shutdown() override
    {
        window.reset();
        core.reset();
        host.reset();
        settings.closeFiles();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties settings;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<AppCore> core;
    std::unique_ptr<MainWindow> window;
};

} // namespace tf::app

START_JUCE_APPLICATION(tf::app::TidefieldApplication)
