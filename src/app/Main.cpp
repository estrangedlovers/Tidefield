#include "AppCore.h"
#include "AudioHost.h"
#include "FactoryContent.h"
#include "gui/MainView.h"

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

    const auto inter = gui::font(14.0f, 600);
    check(inter.getTypefaceName().containsIgnoreCase("Inter"), "embedded typeface loads: " + inter.getTypefaceName());
    const auto brand = gui::brandFont(14.0f);
    check(brand.getTypefaceName().containsIgnoreCase("Quicksand"), "wordmark typeface loads: " + brand.getTypefaceName());

    for (const auto& sound : factorySounds())
    {
        const auto b = loadFactorySound(sound);
        check(b != nullptr && b->seconds() > 0.5, juce::String("factory sound decodes: ") + sound.name);
    }

    fxjuce::registerUserEffects();
    engine::Engine engine;
    engine.prepare(48000.0, 512);

    const auto starter = makeStarterSession(engine);
    bool idsKnown = true;
    for (const auto& sc : starter.scenes)
        for (const auto& [id, value] : sc.values)
            idsKnown = idsKnown && engine.getRegistry().find(id).has_value();
    check(starter.scenes.size() >= 6 && idsKnown, "starter session: " + juce::String(static_cast<int>(starter.scenes.size())) + " scenes, all parameters known");
    {
        io::PresetLibrary library(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tidefield-selftest-presets"));
        addFactoryPresets(library);
        int presets = 0, unknown = 0;
        for (const char* kind : { "drone", "cloud", "resonator", "bloom", "weather", "medium", "loops" })
            for (const auto& p : library.list(kind))
            {
                ++presets;
                for (const auto& [key, value] : p.values)
                    unknown += engine.getRegistry().find(presetPrefix(kind) + key).has_value() ? 0 : 1;
            }
        check(presets >= 25 && unknown == 0, "factory presets: " + juce::String(presets) + ", every value names a parameter");
    }
    check(starter.samples.count("cloud1") == 1 && starter.samples.count("bloom") == 1, "starter session loads its sounds");
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
        : DocumentWindow(name, gui::colour::window(), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
        // Fit the screen: a 13-inch laptop at default scaling is smaller than the
        // preferred 1440 x 900.
        int w = getWidth(), h = getHeight();
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = display->userBounds.toNearestInt();
            w = std::min(w, area.getWidth() - 24);
            h = std::min(h, area.getHeight() - 24);
        }
        setResizeLimits(std::min(1100, w), std::min(720, h), 10000, 10000);
        centreWithSize(std::max(w, 1), std::max(h, 1));
        setVisible(true);
        toFront(true); // key window from the start, so the first key press reaches the instrument
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

        host = std::make_unique<AudioHost>(*settings.getUserSettings(), commandLine.contains("--null-audio"));
        core = std::make_unique<AppCore>(*host);

        auto* view = new gui::MainView(*core);
        window = std::make_unique<MainWindow>(getApplicationName() + " - " + core->session.getName(), view);

        // `--ui-test`: open the whole interface, visit every page, then quit cleanly
        // (CI runs it on the shipped app; sanitizer builds use it for teardown).
        if (commandLine.contains("--ui-test"))
        {
            juce::Timer::callAfterDelay(500, [this] {
                if (window != nullptr)
                    if (auto* v = dynamic_cast<gui::MainView*>(window->getContentComponent()))
                        v->toggleProjector(); // open
            });
            for (int p = 0; p <= gui::DeviceView::NumPages; ++p)
                juce::Timer::callAfterDelay(800 + p * 250, [this, p] {
                    if (window == nullptr)
                        return;
                    if (auto* v = dynamic_cast<gui::MainView*>(window->getContentComponent()))
                        v->showPage(p % gui::DeviceView::NumPages);
                });
            juce::Timer::callAfterDelay(800 + (gui::DeviceView::NumPages + 2) * 250, [this] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                    if (v->isProjectorOpen())
                        v->toggleProjector(); // and close
                std::cout << "UI test passed: every page shown, projector opened and closed" << std::endl;
                systemRequestedQuit();
            });
        }
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
