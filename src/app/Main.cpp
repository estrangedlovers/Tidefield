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
        int presets = 0, unknown = 0, outOfRange = 0;
        for (const char* kind : { "drone", "cloud", "resonator", "bloom", "weather", "medium", "loops", "looper", "input" })
            for (const auto& p : library.list(kind))
            {
                ++presets;
                for (const auto& [key, value] : p.values)
                {
                    const auto index = engine.getRegistry().find(presetPrefix(kind) + key);
                    unknown += index.has_value() ? 0 : 1;
                    if (index.has_value())
                    {
                        const auto& spec = engine.getRegistry().spec(*index);
                        outOfRange += value >= spec.minValue && value <= spec.maxValue ? 0 : 1;
                    }
                }
            }
        check(presets >= 25 && unknown == 0 && outOfRange == 0,
              "factory presets: " + juce::String(presets) + ", every value names a parameter and lies in its range");
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
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, gui::colour::window(), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
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
        toFront(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class TidefieldApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

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

        if (const auto page = commandLine.fromFirstOccurrenceOf("--page=", false, false).upToFirstOccurrenceOf(" ", false, false); page.isNotEmpty())
            juce::Timer::callAfterDelay(300, [this, page] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                    for (int p = 0; p < gui::DeviceView::NumPages; ++p)
                        if (gui::DeviceView::pageName(p).equalsIgnoreCase(page))
                            v->showPage(p);
            });
        if (commandLine.contains("--ui-test"))
        {
            juce::Timer::callAfterDelay(500, [this] {
                if (window != nullptr)
                    if (auto* v = dynamic_cast<gui::MainView*>(window->getContentComponent()))
                        v->toggleProjector();
            });
            const auto original = gui::theme();
            for (std::size_t i = 0; i <= gui::kThemes.size(); ++i)
            {
                const auto t = i < gui::kThemes.size() ? gui::kThemes[i] : original;
                juce::Timer::callAfterDelay(800 + static_cast<int>(i) * 250 + 125, [this, t] {
                    if (core != nullptr)
                        gui::MainView::switchTheme(*core, t);
                });
            }
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
                        v->toggleProjector();
                std::cout << "UI test passed: every page shown, every theme, projector opened and closed" << std::endl;
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
}

START_JUCE_APPLICATION(tf::app::TidefieldApplication)
