#include "AppCore.h"
#include "gui/Settings.h"
#include "AudioHost.h"
#include "ControllerTemplates.h"
#include "FactoryContent.h"
#include "gui/Lessons.h"
#include "KeyBindings.h"
#include "gui/MainView.h"

#include <AudioProcessorEffect.h>
#include <BinaryData.h>
#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/ModRouteManager.h>
#include <io/AudioFileIO.h>
#include <io/Session.h>

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
        int presets = 0, unknown = 0, outside = 0;
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
                        outside += value < spec.minValue || value > spec.maxValue ? 1 : 0;
                    }
                }
            }
        int effectPresets = 0;
        for (const auto& entry : dsp::ProcessorFactory::instance().entries())
            for (const auto& p : library.list("fx:" + std::string(entry.info->typeId)))
            {
                ++effectPresets;
                for (const auto& [key, value] : p.values)
                {
                    const bool known = key == "mix" || (key.size() == 2 && key[0] == 'p' && key[1] >= '1' && key[1] <= '6');
                    unknown += known ? 0 : 1;
                    outside += value < 0.0f || value > 1.0f ? 1 : 0;
                }
            }
        check(presets >= 25 && unknown == 0 && outside == 0,
              "factory presets: " + juce::String(presets) + " for instruments and " + juce::String(effectPresets)
                  + " for effects, every value names a parameter and sits in its range");
    }
    check(starter.samples.count("cloud1") == 1 && starter.samples.count("bloom") == 1, "starter session loads its sounds");
    {
        engine::ModRouteManager macros(engine);
        const auto warnings = io::applyMacrosJson(starter.macros, macros, engine.getRegistry());
        int mapped = 0;
        for (const auto& m : macros.getMacros())
            mapped += static_cast<int>(m.targets.size());
        check(warnings.empty() && mapped >= 12, "starter macros: " + juce::String(mapped) + " targets, all controls known");
    }
    {
        const auto problems = gui::lessonProblems(engine.getRegistry());
        int pages = 0;
        for (const auto& lesson : gui::lessons())
            pages += static_cast<int>(lesson.pages.size());
        for (const auto& p : problems)
            std::cout << "      " << p << std::endl;
        check(gui::lessons().size() >= 6 && problems.isEmpty(),
              "lessons: " + juce::String(static_cast<int>(gui::lessons().size())) + " lessons, " + juce::String(pages) + " pages, every page, tab and parameter known");
    }
    {
        const auto templates = factoryControllerTemplates(engine.getRegistry());
        int mapped = 0;
        std::vector<std::string> issues;
        for (const auto& t : templates)
        {
            const auto found = validateControllerTemplate(t, engine.getRegistry());
            issues.insert(issues.end(), found.begin(), found.end());
            mapped += countBindings(t.midi);
        }
        for (const auto& issue : issues)
            std::cout << "      " << issue << std::endl;
        check(templates.size() >= 4 && issues.empty(),
              "controller templates: " + juce::String(static_cast<int>(templates.size())) + " factory templates, " + juce::String(mapped)
                  + " mappings, every one names a control MIDI can move");
    }
    {
        KeyBindings keys;
        int clashes = 0, reserved = 0;
        for (const auto& info : keyActions())
        {
            clashes += static_cast<int>(keys.conflicts(info.action).size());
            reserved += KeyBindings::reservedUse(keys.get(info.action)).isNotEmpty() ? 1 : 0;
        }
        check(clashes == 0 && reserved == 0, "default shortcuts: " + juce::String(kNumKeyActions) + " actions, no clashes");
    }
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

class AppMenu final : public juce::MenuBarModel
{
public:
    AppMenu(AppCore& c, std::function<gui::MainView*()> v) : core(c), view(std::move(v)) {}

    enum Id
    {
        about = 1, settings, newSession, open, save, saveAs, savePerformance, renderPerformance, renderLoop, record, showRecordings, clearRecent,
        undo, redo, capture, release, projector, zoomIn, zoomOut, zoomReset, fullScreen, fade, panic, keys, take, catchNow, freeze, loop,
        cycles, path, manual, shortcuts, lessons, recentBase = 1000, pageBase = 2000, themeBase = 3000, sceneBase = 4000
    };

    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "View", "Play", "Help" }; }

    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override
    {
        juce::PopupMenu m;
        auto key = [](juce::PopupMenu& menu, int id, const juce::String& text, const juce::String& shortcut, bool enabled = true, bool ticked = false) {
            juce::PopupMenu::Item item(text);
            item.itemID = id;
            item.isEnabled = enabled;
            item.isTicked = ticked;
            item.shortcutKeyDescription = shortcut;
            menu.addItem(item);
        };
        auto label = [this](KeyAction a) { return core.keys.label(a); };
        auto* v = view();
        if (index == 0)
        {
            key(m, newSession, "New Session", label(KeyAction::NewSession));
            key(m, open, "Open...", label(KeyAction::Open));
            juce::PopupMenu recent;
            const auto list = core.recentSessions();
            for (int i = 0; i < list.size(); ++i)
                recent.addItem(recentBase + i, juce::File(list[i]).getFileNameWithoutExtension(), juce::File(list[i]).existsAsFile());
            if (! list.isEmpty())
            {
                recent.addSeparator();
                recent.addItem(clearRecent, "Clear Menu");
            }
            m.addSubMenu("Open Recent", recent, ! list.isEmpty());
            m.addSeparator();
            key(m, save, "Save", label(KeyAction::Save));
            key(m, saveAs, "Save As...", label(KeyAction::SaveAs));
            m.addSeparator();
            const bool hasPerformance = ! core.performance.get().empty();
            m.addItem(savePerformance, "Save Performance...", hasPerformance);
            m.addItem(renderPerformance, "Render Performance...", hasPerformance && ! core.performance.isRendering());
            m.addItem(renderLoop, "Render a 5-Minute Loop of the Sound...", ! core.performance.isRendering());
            m.addSeparator();
            const bool recording = core.recorder.getStatus().state == io::Recorder::State::Recording;
            key(m, record, recording ? "Stop Recording" : "Start Recording", label(KeyAction::Record));
            m.addItem(showRecordings, "Show Recordings");
        }
        else if (index == 1)
        {
            key(m, undo, core.undo.canUndo() ? "Undo " + core.undo.getUndoDescription() : juce::String("Undo"), label(KeyAction::Undo), core.undo.canUndo());
            key(m, redo, core.undo.canRedo() ? "Redo " + core.undo.getRedoDescription() : juce::String("Redo"), label(KeyAction::Redo), core.undo.canRedo());
            m.addSeparator();
            key(m, capture, "Capture Scene at Cursor", label(KeyAction::Capture));
            key(m, release, "Release Held Controls", label(KeyAction::Release));
        }
        else if (index == 2)
        {
            juce::PopupMenu pages;
            for (int p = 0; p < gui::DeviceView::NumPages; ++p)
                pages.addItem(pageBase + p, gui::DeviceView::pageName(p), true, v != nullptr && v->getPage() == p);
            m.addSubMenu("Device Tab", pages);
            juce::PopupMenu themes;
            for (bool light : { false, true })
            {
                themes.addSectionHeader(light ? "Light" : "Dark");
                for (std::size_t i = 0; i < gui::kThemes.size(); ++i)
                    if (gui::themeIsLight(gui::kThemes[i]) == light)
                        themes.addItem(themeBase + static_cast<int>(i), gui::themeName(gui::kThemes[i]), true, gui::theme() == gui::kThemes[i]);
            }
            m.addSubMenu("Theme", themes);
            m.addSeparator();
            key(m, zoomIn, "Zoom In", label(KeyAction::ZoomIn));
            key(m, zoomOut, "Zoom Out", label(KeyAction::ZoomOut));
            key(m, zoomReset, "Actual Size", label(KeyAction::ZoomReset));
            m.addSeparator();
            key(m, projector, "Projector Window", label(KeyAction::Projector), v != nullptr, v != nullptr && v->isProjectorOpen());
            m.addItem(fullScreen, "Full Screen");
        }
        else if (index == 3)
        {
            const auto& f = core.latest();
            key(m, fade, f.fadeState == engine::FadeState::Silent || f.fadeState == engine::FadeState::FadingOut ? "Fade In" : "Fade Out", label(KeyAction::Fade));
            key(m, panic, f.panicActive ? "Resume from Panic" : "Panic", label(KeyAction::Panic));
            m.addSeparator();
            juce::PopupMenu scenes;
            const auto& list = core.scenes.getScenes();
            for (std::size_t i = 0; i < list.size(); ++i)
                scenes.addItem(sceneBase + static_cast<int>(i), juce::String(list[i].name) + (i < 9 ? "    " + juce::String(static_cast<int>(i) + 1) : juce::String()));
            m.addSubMenu("Glide to Scene", scenes, ! list.empty());
            m.addSeparator();
            key(m, keys, "Computer Keyboard Plays Bloom", label(KeyAction::NoteMode), true, v != nullptr && v->noteMode);
            key(m, take, "Take: Record, Stop, Play", label(KeyAction::Take));
            key(m, catchNow, "Catch the Last Seconds", label(KeyAction::Catch));
            key(m, freeze, "Freeze All", label(KeyAction::Freeze));
            key(m, loop, "Tape Loop: Record, Close, Overdub", label(KeyAction::LoopRecord));
            key(m, cycles, "Cycles On or Off", label(KeyAction::Cycles));
            key(m, path, "Draw a Path", label(KeyAction::DrawPath));
        }
        else if (index == 4)
        {
            m.addItem(lessons, "Lessons", v != nullptr, v != nullptr && v->areLessonsOpen());
            m.addItem(manual, "Tidefield Manual");
            m.addItem(shortcuts, "Keyboard Shortcuts...");
            m.addSeparator();
            m.addItem(about, "About Tidefield");
        }
        return m;
    }

    void menuItemSelected(int id, int) override
    {
        auto* v = view();
        auto press = [v](KeyAction action) {
            if (v != nullptr)
                v->performAction(action);
        };
        auto& s = core.session;
        if (id >= themeBase && id < themeBase + static_cast<int>(gui::kThemes.size()))
            return gui::MainView::switchTheme(core, gui::kThemes[static_cast<std::size_t>(id - themeBase)]);
        if (id >= pageBase && id < pageBase + static_cast<int>(gui::DeviceView::NumPages))
            return v != nullptr ? v->showPage(id - pageBase) : void();
        if (id >= sceneBase && id < sceneBase + 1000)
            return v != nullptr ? v->glideTo(id - sceneBase) : void();
        if (id >= recentBase && id < recentBase + 100)
        {
            const auto list = core.recentSessions();
            if (id - recentBase < list.size())
                s.openFile(juce::File(list[id - recentBase]));
            return;
        }
        switch (id)
        {
            case about: if (v != nullptr) v->openSettings(gui::SettingsTab::About); break;
            case settings: if (v != nullptr) v->openSettings(); break;
            case newSession: s.newSession(); break;
            case open: s.open(); break;
            case save: s.save(); break;
            case saveAs: s.saveAs(); break;
            case savePerformance: core.performance.save(); break;
            case renderPerformance: core.performance.render(core.getRecordStems(), 0.0); break;
            case renderLoop: core.performance.renderSoundAsLoop(300.0, 8.0); break;
            case record: core.toggleRecording(); break;
            case showRecordings:
                core.getRecordingsFolder().createDirectory();
                core.getRecordingsFolder().revealToUser();
                break;
            case clearRecent: core.clearRecentSessions(); break;
            case undo: core.undo.undo(); break;
            case redo: core.undo.redo(); break;
            case capture: press(KeyAction::Capture); break;
            case release: press(KeyAction::Release); break;
            case zoomIn: gui::setInterfaceScale(core, gui::interfaceScale(core) + 0.1f); break;
            case zoomOut: gui::setInterfaceScale(core, gui::interfaceScale(core) - 0.1f); break;
            case zoomReset: gui::setInterfaceScale(core, 1.0f); break;
            case projector: if (v != nullptr) v->toggleProjector(); break;
            case fullScreen:
                if (v != nullptr)
                    if (auto* w = dynamic_cast<juce::ResizableWindow*>(v->getTopLevelComponent()))
                        w->setFullScreen(! w->isFullScreen());
                break;
            case fade: press(KeyAction::Fade); break;
            case panic: press(KeyAction::Panic); break;
            case keys: press(KeyAction::NoteMode); break;
            case take: press(KeyAction::Take); break;
            case catchNow: press(KeyAction::Catch); break;
            case freeze: press(KeyAction::Freeze); break;
            case loop: press(KeyAction::LoopRecord); break;
            case cycles: press(KeyAction::Cycles); break;
            case path: press(KeyAction::DrawPath); break;
            case lessons: if (v != nullptr) v->openLessons(); break;
            case manual: juce::URL("https://github.com/estrangedlovers/Tidefield/blob/main/docs/MANUAL.md").launchInDefaultBrowser(); break;
            case shortcuts: if (v != nullptr) v->openSettings(gui::SettingsTab::Keys); break;
            default: break;
        }
    }

    juce::PopupMenu appleMenu()
    {
        juce::PopupMenu m;
        m.addItem(about, "About Tidefield");
        m.addSeparator();
        juce::PopupMenu::Item item("Settings...");
        item.itemID = settings;
        item.shortcutKeyDescription = core.keys.label(KeyAction::Settings);
        m.addItem(item);
        return m;
    }

private:
    AppCore& core;
    std::function<gui::MainView*()> view;
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

        gui::setInterfaceScale(*core, gui::interfaceScale(*core));
        auto* view = new gui::MainView(*core);
        window = std::make_unique<MainWindow>(getApplicationName() + " - " + core->session.getName(), view);
        openProjectsIn(commandLine);
        if (! commandLine.contains("--ui-test"))
            view->offerLessons();
        if (core->recovery != nullptr && ! commandLine.contains("--ui-test"))
            juce::Timer::callAfterDelay(600, [this] {
                if (core != nullptr && core->recovery != nullptr)
                    core->recovery->offerRestore();
            });
        menu = std::make_unique<AppMenu>(*core, [this]() -> gui::MainView* {
            return window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr;
        });
#if JUCE_MAC
        appleMenu = menu->appleMenu();
        juce::MenuBarModel::setMacMainMenu(menu.get(), &appleMenu);
#endif

        if (const auto page = commandLine.fromFirstOccurrenceOf("--page=", false, false).upToFirstOccurrenceOf(" ", false, false); page.isNotEmpty())
            juce::Timer::callAfterDelay(300, [this, page] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                    for (int p = 0; p < gui::DeviceView::NumPages; ++p)
                        if (gui::DeviceView::pageName(p).equalsIgnoreCase(page))
                            v->showPage(p);
            });
        if (const auto tab = commandLine.fromFirstOccurrenceOf("--settings=", false, false).upToFirstOccurrenceOf(" ", false, false); tab.isNotEmpty())
            juce::Timer::callAfterDelay(400, [this, tab] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                    v->openSettings(static_cast<gui::SettingsTab>(juce::jlimit(0, static_cast<int>(gui::SettingsTab::Count) - 1, tab.getIntValue())));
            });
        if (const auto lesson = commandLine.fromFirstOccurrenceOf("--lesson=", false, false).upToFirstOccurrenceOf(" ", false, false); lesson.isNotEmpty())
            juce::Timer::callAfterDelay(500, [this, lesson] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                    v->showLesson(lesson.upToFirstOccurrenceOf(".", false, false).getIntValue() - 1, lesson.fromFirstOccurrenceOf(".", false, false).getIntValue() - 1);
            });
        if (commandLine.contains("--ui-test"))
        {
            for (int t = 0; t < static_cast<int>(gui::SettingsTab::Count); ++t)
                juce::Timer::callAfterDelay(300 + t * 60, [this, t] {
                    if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                        v->openSettings(static_cast<gui::SettingsTab>(t));
                });
            juce::Timer::callAfterDelay(300 + static_cast<int>(gui::SettingsTab::Count) * 60 + 40, [] { gui::closeSettings(); });
            juce::Timer::callAfterDelay(500, [this] {
                if (window != nullptr)
                    if (auto* v = dynamic_cast<gui::MainView*>(window->getContentComponent()))
                        v->toggleProjector();
            });
            juce::Timer::callAfterDelay(650, [this] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                {
                    const juce::StringArray dragged { juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tidefield-ui-test.wav").getFullPathName() };
                    for (const auto& p : { juce::Point<int>(v->getWidth() / 2, v->getHeight() / 3), juce::Point<int>(v->getWidth() / 2, v->getHeight() - 120),
                                           juce::Point<int>(100, v->getHeight() / 2) })
                        v->fileDragMove(dragged, p.x, p.y);
                    v->fileDragExit(dragged);
                }
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
            const int lessonsStart = 800 + (gui::DeviceView::NumPages + 2) * 250;
            const auto savedLesson = gui::savedLessonPosition(host->getSettings());
            int step = 1;
            juce::Timer::callAfterDelay(lessonsStart, [this] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                    v->showLesson(-1, 0);
            });
            for (int l = 0; l < static_cast<int>(gui::lessons().size()); ++l)
                for (int p = 0; p < static_cast<int>(gui::lessons()[static_cast<std::size_t>(l)].pages.size()); ++p)
                    juce::Timer::callAfterDelay(lessonsStart + 60 * step++, [this, l, p] {
                        if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                        {
                            v->showLesson(l, p);
                            if (const auto missing = v->missingLessonTarget(); missing.isNotEmpty())
                            {
                                std::cout << "FAIL  lesson " << l + 1 << " page " << p + 1 << " highlights " << missing << ", which is not on screen" << std::endl;
                                setApplicationReturnValue(1);
                            }
                        }
                    });
            juce::Timer::callAfterDelay(lessonsStart + 60 * step + 100, [this, savedLesson, step] {
                if (auto* v = window != nullptr ? dynamic_cast<gui::MainView*>(window->getContentComponent()) : nullptr)
                {
                    v->closeLessons();
                    if (v->isProjectorOpen())
                        v->toggleProjector();
                }
                gui::saveLessonPosition(host->getSettings(), savedLesson);
                const bool ok = getApplicationReturnValue() == 0;
                std::cout << (ok ? "UI test passed" : "UI test FAILED") << ": every page and settings tab shown, every theme, projector opened and closed, "
                          << step - 1 << " lesson pages stepped through" << std::endl;
                quit();
            });
        }
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        if (window != nullptr)
        {
            window->setMinimised(false);
            window->toFront(true);
        }
        openProjectsIn(commandLine);
    }

    void openProjectsIn(const juce::String& commandLine)
    {
        juce::StringArray tokens;
        tokens.addTokens(commandLine, true);
        for (auto token : tokens)
        {
            token = token.unquoted().trim();
            if (! juce::File::isAbsolutePath(token))
                continue;
            const juce::File file(token);
            if (file.existsAsFile() && io::isSessionFile(file))
                openWhenFree(file, 0);
        }
    }

    void openWhenFree(const juce::File& file, int attempts)
    {
        if (core == nullptr)
            return;
        if (core->session.isBusy() && attempts < 50)
        {
            juce::Timer::callAfterDelay(200, [this, file, attempts] { openWhenFree(file, attempts + 1); });
            return;
        }
        core->session.openFile(file);
    }

    void shutdown() override
    {
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
        menu.reset();
        window.reset();
        core.reset();
        host.reset();
        settings.closeFiles();
    }

    void systemRequestedQuit() override
    {
        if (core == nullptr)
        {
            quit();
            return;
        }
        core->session.whenSafeToDiscard("quitting", [] { juce::JUCEApplication::quit(); });
    }

private:
    juce::ApplicationProperties settings;
    std::unique_ptr<AudioHost> host;
    std::unique_ptr<AppCore> core;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<AppMenu> menu;
    juce::PopupMenu appleMenu;
};
}

START_JUCE_APPLICATION(tf::app::TidefieldApplication)
