#include "MainView.h"

#include "Places.h"

#include "../FactoryContent.h"

#include <dsp/analysis/PitchDetect.h>
#include <io/AudioFileIO.h>
#include <io/AudioFolder.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string_view>
#include <thread>

namespace tf::app::gui {
using engine::P;

namespace {
constexpr int kTopH = 42;
constexpr int kStatusH = 24;
constexpr int kBrowserW = 214;
constexpr int kMacroW = 244;
constexpr int kPadsH = 78;
std::vector<MainView*> openViews;
constexpr const char* kThemeKey = "theme";
constexpr int kThemeMenuBase = 100;

void gestureToggle(AppCore& core, bool recordNew)
{
    const auto state = core.latest().gestureState;
    if (state != engine::GestureState::Idle && ! recordNew)
        core.gestures.stop();
    else if (recordNew || ! core.gestures.hasTake())
    {
        core.gestures.record();
        core.status("Recording a take: play, move, turn. Press G again to stop.");
    }
    else
        core.gestures.play();
}

void showGestureMenu(AppCore& core, juce::Component* owner)
{
    juce::PopupMenu m;
    const auto state = core.latest().gestureState;
    m.addSectionHeader("Take");
    m.addItem(1, "Record a new take");
    m.addItem(2, "Play", core.gestures.hasTake() && state == engine::GestureState::Idle);
    m.addItem(3, "Stop", state != engine::GestureState::Idle);
    m.addItem(4, "Loop", core.gestures.hasTake(), core.gestures.isLooping());
    m.addSeparator();
    m.addItem(5, "Clear the take", core.gestures.hasTake());
    showMenu(m, owner, [&core, state](int r) {
        if (r == 1) gestureToggle(core, true);
        else if (r == 2) core.gestures.play();
        else if (r == 3) core.gestures.stop();
        else if (r == 4) core.gestures.setLoop(! core.gestures.isLooping(), state == engine::GestureState::Playing);
        else if (r == 5) core.gestures.clear();
    });
}

juce::String clock(double seconds)
{
    const int s = std::max(0, static_cast<int>(seconds));
    const int h = s / 3600, m = (s % 3600) / 60;
    return h > 0 ? juce::String::formatted("%d:%02d:%02d", h, m, s % 60) : juce::String::formatted("%d:%02d", m, s % 60);
}

class MenuBox final : public ParamComponent
{
public:
    using ParamComponent::ParamComponent;
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(isMouseOver() ? colour::lift(colour::panelHi(), 0.07f) : colour::panelHi());
        g.fillRoundedRectangle(r, metric::radius);
        g.setColour(valueColour() == colour::accent() ? colour::text() : valueColour());
        g.setFont(font(12.0f, 600));
        g.drawText(valueText(), r.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);
        juce::Path arrow;
        const float cx = r.getRight() - 11.0f, cy = r.getCentreY();
        arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(colour::textDim());
        g.fillPath(arrow);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return model.showParamMenu(param, this);
        juce::PopupMenu m;
        const auto items = model.choices(param);
        const int current = juce::roundToInt(model.value(param));
        for (int i = 0; i < items.size(); ++i)
            m.addItem(i + 1, items[i], true, i == current);
        showMenu(m, this, [this](int r) {
            if (r > 0)
                model.set(param, static_cast<float>(r - 1));
        });
    }
};

class RecordButton final : public juce::Component, public Animated
{
public:
    explicit RecordButton(Model& m) : model(m) { model.add(this); }
    ~RecordButton() override { model.remove(this); }
    void tick() override
    {
        const auto st = model.core.recorder.getStatus();
        const juce::String text = st.state == io::Recorder::State::Recording ? clock(st.seconds)
                                  : st.state == io::Recorder::State::Finishing ? juce::String("Saving")
                                                                                : juce::String("Rec");
        const bool on = st.state == io::Recorder::State::Recording;
        if (text != shown || on != shownOn)
        {
            shown = text;
            shownOn = on;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(shownOn ? colour::warn() : (isMouseOver() ? colour::lift(colour::panelHi(), 0.07f) : colour::panelHi()));
        g.fillRoundedRectangle(r, metric::radius);
        g.setColour(shownOn ? colour::text() : colour::warn());
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ r.getX() + 14.0f, r.getCentreY() }));
        g.setColour(colour::text());
        g.setFont(font(12.0f, 600));
        g.drawText(shown + (model.core.getRecordStems() ? "  stems" : ""), r.withTrimmedLeft(24.0f), juce::Justification::centredLeft, true);
    }
    void mouseEnter(const juce::MouseEvent&) override
    {
        repaint();
        if (model.onHover)
            model.onHover("Record what you hear to " + model.core.getRecordingsFolder().getFullPathName()
                          + " (Shift+R). Right-click for stems and the folder.");
    }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            return model.core.toggleRecording();
        const bool idle = model.core.recorder.getStatus().state == io::Recorder::State::Idle;
        juce::PopupMenu m;
        m.addItem(1, "Record stems too", idle, model.core.getRecordStems());
        m.addItem(2, "Recordings folder...");
        m.addItem(3, "Show recordings");
        showMenu(m, this, [this](int r) {
            if (r == 1)
                model.core.setRecordStems(! model.core.getRecordStems());
            else if (r == 2)
            {
                chooser = std::make_unique<juce::FileChooser>("Where should recordings go?", model.core.getRecordingsFolder());
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                     [this](const juce::FileChooser& fc) {
                                         if (fc.getResult() != juce::File())
                                             model.core.setRecordingsFolder(fc.getResult());
                                     });
            }
            else if (r == 3)
            {
                const auto last = model.core.getLastRecording();
                (last.exists() ? last : model.core.getRecordingsFolder()).revealToUser();
            }
            repaint();
        });
    }

private:
    Model& model;
    juce::String shown = "Rec";
    bool shownOn = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class TempoWidget final : public juce::Component, public Animated
{
public:
    explicit TempoWidget(Model& m) : model(m) { model.add(this); }
    ~TempoWidget() override { model.remove(this); }

    void tick() override
    {
        const auto& f = model.frame();
        const bool on = model.value(P::SyncOn) > 0.5f;
        const float bpm = f.hostTempo ? f.bpm : model.value(P::SyncBpm);
        const float beat = on ? std::pow(1.0f - f.beatPhase, 4.0f) : 0.0f;
        if (on != shownOn || std::abs(bpm - shownBpm) > 0.05f || std::abs(beat - shownBeat) > 0.02f || f.hostTempo != shownHost)
        {
            shownOn = on;
            shownBpm = bpm;
            shownBeat = beat;
            shownHost = f.hostTempo;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        auto syncArea = r.removeFromLeft(54.0f);
        auto tapArea = r.removeFromRight(38.0f);
        g.setColour(shownOn ? colour::tide() : colour::panelHi());
        g.fillRoundedRectangle(syncArea, metric::radius);
        g.setColour(shownOn ? colour::well() : colour::text());
        g.setFont(font(12.0f, 600));
        g.drawText("Sync", syncArea.withTrimmedRight(10.0f), juce::Justification::centred);
        g.setColour((shownOn ? colour::well() : colour::textFaint()).withAlpha(0.35f + 0.65f * shownBeat));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ syncArea.getRight() - 9.0f, syncArea.getCentreY() }));

        auto field = r.reduced(3.0f, 0.0f);
        drawWell(g, field);
        g.setColour(shownHost ? display::tide() : display::text());
        g.setFont(font(13.0f, 600));
        g.drawText(juce::String(shownBpm, 1), field, juce::Justification::centred);

        g.setColour(isMouseOver() && tapArea.contains(getMouseXYRelative().toFloat()) ? colour::lift(colour::panelHi(), 0.08f) : colour::panelHi());
        g.fillRoundedRectangle(tapArea, metric::radius);
        g.setColour(colour::text());
        g.setFont(font(11.5f, 600));
        g.drawText("Tap", tapArea, juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (model.onHover)
            model.onHover(shownHost ? juce::String("Tempo: following the DAW. Sync locks the loops to the beat and the delays to note lengths.")
                                    : juce::String("Tempo: Sync locks the loops to the beat and the delays to note lengths; drag the number to change it, or Tap."));
    }
    void mouseMove(const juce::MouseEvent&) override { repaint(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return model.showParamMenu(e.x < 54 ? P::SyncOn : P::SyncBpm, this);
        if (e.x < 54)
            return model.toggle(P::SyncOn);
        if (e.x > getWidth() - 38)
            return tap();
        dragStart = model.value(P::SyncBpm);
        model.beginTouch(P::SyncBpm);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.x < 54 || e.mouseDownPosition.x > static_cast<float>(getWidth() - 38))
            return;
        const float step = e.mods.isShiftDown() ? 0.05f : 0.5f;
        model.set(P::SyncBpm, std::round((dragStart - static_cast<float>(e.getDistanceFromDragStartY()) * step) * 10.0f) / 10.0f);
    }
    void mouseUp(const juce::MouseEvent&) override { model.endTouch(P::SyncBpm); }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.x >= 54 && e.x <= getWidth() - 38)
            model.resetToDefault(P::SyncBpm);
    }

private:
    void tap()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0)
            taps.clear();
        taps.push_back(now);
        if (taps.size() > 5)
            taps.erase(taps.begin());
        if (taps.size() >= 2)
        {
            const double beatMs = (taps.back() - taps.front()) / static_cast<double>(taps.size() - 1);
            model.set(P::SyncBpm, static_cast<float>(std::round(60000.0 / beatMs * 10.0) / 10.0));
        }
    }

    Model& model;
    std::vector<double> taps;
    float dragStart = 90.0f, shownBpm = 0.0f, shownBeat = 0.0f;
    bool shownOn = false, shownHost = false;
};
}

class ProjectorWindow final : public juce::DocumentWindow
{
public:
    ProjectorWindow(Model& m, juce::Component* mainWindow, std::function<void()> closed)
        : juce::DocumentWindow("Tidefield - Projector", colour::well(), juce::DocumentWindow::allButtons), view(m, true), onClosed(std::move(closed))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&view, false);
        setResizable(true, false);
        view.onDoubleClick = [this] { setFullScreen(! isFullScreen()); };

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto mainCentre = mainWindow != nullptr ? mainWindow->getScreenBounds().getCentre() : juce::Point<int>();
        const juce::Displays::Display* target = displays.getPrimaryDisplay();
        for (const auto& d : displays.displays)
            if (! d.logicalBounds.toNearestInt().contains(mainCentre))
                target = &d;
        const auto area = target != nullptr ? target->userBounds.toNearestInt() : juce::Rectangle<int>(0, 0, 1280, 800);
        setBounds(area.withSizeKeepingCentre(std::min(1280, area.getWidth() - 80), std::min(800, area.getHeight() - 80)));
        setVisible(true);
        if (target != nullptr && ! target->logicalBounds.toNearestInt().contains(mainCentre))
            setFullScreen(true);
    }

    void closeButtonPressed() override { juce::MessageManager::callAsync(onClosed); }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (isFullScreen())
                setFullScreen(false);
            else
                closeButtonPressed();
            return true;
        }
        if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
        {
            setFullScreen(! isFullScreen());
            return true;
        }
        return false;
    }

private:
    TerrainView view;
    std::function<void()> onClosed;
};

class TopBar final : public juce::Component, public Animated
{
public:
    TopBar(Model& m, MainView& v) : model(m), view(v), meter(m, -1, true), rec(m), autoMaster(m, P::MasterAuto, "Auto master", {}, colour::good()), tempo(m)
    {
        model.add(this);
        sessionButton.setHelp(&model, shortcut("new, open, save (Cmd+N, Cmd+O, Cmd+S)"));
        sessionButton.onClick = [this] {
            juce::PopupMenu menu;
            menu.addItem(1, "New session");
            menu.addItem(2, "Open...");
            menu.addSeparator();
            menu.addItem(3, "Save");
            menu.addItem(4, "Save as...");
            menu.addSeparator();
            menu.addItem(5, shortcut("Projector window (Cmd+P)"), true, view.isProjectorOpen());
            menu.addItem(6, shortcut("Settings...  (Cmd+,)"));
            menu.addSeparator();
            menu.addItem(8, "Undo " + model.core.undo.getUndoDescription() + shortcut("  (Cmd+Z)"), model.core.undo.canUndo());
            menu.addItem(9, "Redo " + model.core.undo.getRedoDescription() + shortcut("  (Shift+Cmd+Z)"), model.core.undo.canRedo());
            juce::PopupMenu appearance;
            for (bool light : { false, true })
            {
                appearance.addSectionHeader(light ? "Light" : "Dark");
                for (std::size_t i = 0; i < kThemes.size(); ++i)
                    if (themeIsLight(kThemes[i]) == light)
                        appearance.addItem(kThemeMenuBase + static_cast<int>(i), themeName(kThemes[i]), true, theme() == kThemes[i]);
            }
            menu.addSubMenu("Appearance", appearance);
            showMenu(menu, this, [this](int r) {
                auto& s = model.core.session;
                if (r == 1) s.newSession();
                else if (r == 2) s.open();
                else if (r == 3) s.save();
                else if (r == 4) s.saveAs();
                else if (r == 5) view.toggleProjector();
                else if (r == 6) view.openSettings();
                else if (r == 8) model.core.undo.undo();
                else if (r == 9) model.core.undo.redo();
                else if (r >= kThemeMenuBase && r < kThemeMenuBase + static_cast<int>(kThemes.size()))
                    MainView::switchTheme(model.core, kThemes[static_cast<std::size_t>(r - kThemeMenuBase)]);
            });
        };
        fade.setHelp(&model, "fade the whole instrument in or out over the fade length (Space)");
        fade.onClick = [this] {
            const auto st = model.frame().fadeState;
            model.engine.command(st == engine::FadeState::Silent || st == engine::FadeState::FadingOut ? engine::Command::FadeIn : engine::Command::FadeOut);
        };
        panic.setHelp(&model, "silence everything at once and clear every tail; press again to resume (Esc)");
        panic.onClick = [this] { model.engine.command(model.frame().panicActive ? engine::Command::ResumeFromPanic : engine::Command::Panic); };
        keys.setHelp(&model, "play Bloom from the computer keyboard: A W S E D F T G Y H U J K, Z/X octave, C/V velocity (M)");
        keys.onClick = [this] { view.noteMode = ! view.noteMode; };
        audio.setHelp(&model, shortcut("theme, zoom, audio device, MIDI and sync, plug-in folders, files, recording and rendering (Cmd+,)"));
        audio.onClick = [this] { view.openSettings(); };
        for (auto* c : std::initializer_list<juce::Component*> { &sessionButton, &fade, &panic, &keys, &audio, &meter, &rec, &autoMaster, &tempo })
            addAndMakeVisible(c);
    }
    ~TopBar() override { model.remove(this); }

    void tick() override
    {
        const auto& f = model.frame();
        static const char* fadeText[] = { "Fade in", "Fading in", "Playing", "Fading out" };
        const auto ft = juce::String(fadeText[static_cast<int>(f.fadeState)]);
        if (fade.getButtonText() != ft)
            fade.setButtonText(ft);
        fade.setToggleState(f.fadeState == engine::FadeState::Open || f.fadeState == engine::FadeState::FadingIn, juce::dontSendNotification);
        panic.setToggleState(f.panicActive, juce::dontSendNotification);
        panic.setButtonText(f.panicActive ? "Resume" : "Panic");
        keys.setToggleState(view.noteMode, juce::dontSendNotification);
        keys.setButtonText(view.noteMode ? "Keys " + Model::noteName(static_cast<float>(view.octave * 12 + 12)) : juce::String("Keys"));
        const auto name = model.core.session.getName() + juce::String::fromUTF8("  \xe2\x96\xbe");
        if (sessionButton.getButtonText() != name)
            sessionButton.setButtonText(name);
        if (++slow % 15 == 0)
            repaint(cpuArea);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(8, 7);
        r.removeFromLeft(112);
        sessionButton.setBounds(r.removeFromLeft(150));
        r.removeFromLeft(14);
        fade.setBounds(r.removeFromLeft(96));
        r.removeFromLeft(4);
        panic.setBounds(r.removeFromLeft(74));
        r.removeFromLeft(4);
        rec.setBounds(r.removeFromLeft(96));
        r.removeFromLeft(14);
        autoMaster.setBounds(r.removeFromLeft(104));
        r.removeFromLeft(4);
        keys.setBounds(r.removeFromLeft(74));
        r.removeFromLeft(14);
        tempo.setBounds(r.removeFromLeft(156));

        audio.setBounds(r.removeFromRight(72));
        r.removeFromRight(8);
        meter.setBounds(r.removeFromRight(150).reduced(0, 4));
        r.removeFromRight(10);
        cpuArea = r.removeFromRight(std::min(190, r.getWidth()));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colour::header());
        g.setColour(colour::line());
        g.fillRect(getLocalBounds().removeFromBottom(1));

        drawWordmark(g, getLocalBounds().reduced(12, 0).removeFromLeft(110).toFloat(), 18.0f, colour::text());

        const auto& f = model.frame();
        const float load = f.dspLoad > 0.0f ? f.dspLoad : static_cast<float>(model.core.host.getCpuLoad());
        g.setFont(font(11.5f, 500));
        auto a = cpuArea.toFloat();
        const auto output = model.core.host.describeOutput();
        if (output.isEmpty())
        {
            g.setColour(colour::warn());
            g.drawText("No audio output", a, juce::Justification::centredRight);
            return;
        }
        const int pct = juce::roundToInt(load * 100.0f);
        g.setColour(pct > 70 ? colour::warn() : colour::textDim());
        const auto cpu = juce::String(pct) + "% CPU" + (f.guardLevel > 0 ? "  lite " + juce::String(f.guardLevel) : juce::String());
        g.drawText(cpu, a.removeFromRight(90.0f), juce::Justification::centredRight);
        g.setColour(colour::textFaint());
        g.drawText(output, a, juce::Justification::centredRight, true);
    }

private:
    Model& model;
    MainView& view;
    FlatButton sessionButton { "Untitled" }, fade { "Fade in", colour::good() }, panic { "Panic", colour::warn() }, keys { "Keys", colour::tide() },
        audio { "Settings" };
    Meter meter;
    RecordButton rec;
    Toggle autoMaster;
    TempoWidget tempo;
    juce::Rectangle<int> cpuArea;
    int slow = 0;
};

class Browser final : public juce::Component, public Animated
{
public:
    Browser(Model& m, MainView& v) : model(m), view(v)
    {
        model.add(this);
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        list.owner = this;
        search.setTextToShowWhenEmpty("Search sounds and places", colour::textFaint());
        search.setFont(font(12.0f));
        search.setColour(juce::TextEditor::backgroundColourId, colour::panelHi());
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour(juce::TextEditor::textColourId, colour::text());
        search.setIndents(8, 5);
        search.onTextChange = [this] { layout(); };
        search.onEscapeKey = [this] {
            search.clear();
            layout();
            unfocusAllComponents();
        };
        addAndMakeVisible(search);
        auto& settings = model.core.host.getSettings();
        favourites.addTokens(settings.getValue(kFavouritesKey), "\n", {});
        favourites.removeEmptyStrings();
        openPlaces.addTokens(settings.getValue(places::kOpenKey), "\n", {});
        openPlaces.removeEmptyStrings();
        reloadPlaces();
    }
    ~Browser() override
    {
        cancelScans->store(true);
        model.remove(this);
    }

    void tick() override
    {
        if (places::version() != shownPlacesVersion)
        {
            reloadPlaces();
            layout();
        }
        const auto& scenes = model.core.scenes.getScenes();
        const auto& f = model.frame();
        bool weightsMoved = false;
        for (std::size_t k = 0; k < scenes.size() && k < shownWeights.size(); ++k)
            weightsMoved = weightsMoved || std::abs(shownWeights[k] - f.sceneWeights[k]) > 0.01f;
        if (model.core.scenes.getVersion() != shownVersion)
        {
            shownVersion = model.core.scenes.getVersion();
            layout();
        }
        if (weightsMoved || shownWeights.size() != scenes.size())
        {
            shownWeights.assign(f.sceneWeights.begin(), f.sceneWeights.begin() + static_cast<long>(scenes.size()));
            list.repaint();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop(metric::header).reduced(2, 4);
        search.setBounds(r.removeFromTop(24).reduced(4, 0));
        r.removeFromTop(4);
        viewport.setBounds(r);
        layout();
    }

    void paint(juce::Graphics& g) override { drawPanel(g, getLocalBounds().toFloat(), "Browser"); }

    void addPlace(const juce::File& folder)
    {
        openPlaces.addIfNotAlreadyThere(folder.getFullPathName());
        saveOpenPlaces();
        if (places::add(model.core.host.getSettings(), folder))
            model.core.status("Added " + folder.getFileName() + " to Places");
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser>("Add a folder to Places", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        juce::Component::SafePointer<Browser> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe](const juce::FileChooser& fc) {
            if (safe != nullptr && fc.getResult().isDirectory())
                safe->addPlace(fc.getResult());
        });
    }

private:
    struct Row
    {
        enum Kind { Header, Scene, Capture, Sound, Disk, Place, AddPlace, File, Note } kind;
        int index = -1;
        juce::String text, detail;
        juce::File file;
        int depth = 0;
    };

    struct List final : public juce::Component
    {
        Browser* owner = nullptr;
        int hover = -1;
        bool dragging = false;
        void paint(juce::Graphics& g) override { owner->paintRows(g); }
        void mouseMove(const juce::MouseEvent& e) override { setHover(e.y / kRowH); }
        void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
        void mouseDown(const juce::MouseEvent& e) override
        {
            dragging = false;
            if (e.mods.isPopupMenu())
                owner->clicked(e.y / kRowH, e);
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (! dragging && ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() > 5)
                dragging = owner->startDrag(e.getMouseDownY() / kRowH);
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            if (! dragging && ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() <= 5)
                owner->clicked(e.getMouseDownY() / kRowH, e);
            dragging = false;
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override { owner->doubleClicked(e.y / kRowH); }
        void setHover(int h)
        {
            if (h != hover)
            {
                hover = h;
                repaint();
                owner->hovered(h);
            }
        }
    };
    static constexpr int kRowH = 22;
    static constexpr int kMaxPlaceFiles = 500;
    static constexpr int kScanLimit = 20000;
    static constexpr int kMaxSearchFiles = 200;

    static bool interactive(Row::Kind k) { return k != Row::Header && k != Row::Note; }

    void reloadPlaces()
    {
        shownPlacesVersion = places::version();
        placeList = places::get(model.core.host.getSettings());
        for (auto it = listings.begin(); it != listings.end();)
            it = placeList.contains(it->first) ? std::next(it) : listings.erase(it);
        for (const auto& p : placeList)
            if (openPlaces.contains(p))
                requestScan(p, false);
    }

    void saveOpenPlaces() { model.core.host.getSettings().setValue(places::kOpenKey, openPlaces.joinIntoString("\n")); }

    void requestScan(const juce::String& path, bool force)
    {
        if (scanning.contains(path) || (! force && listings.count(path) > 0))
            return;
        scanning.add(path);
        juce::Component::SafePointer<Browser> safe(this);
        model.core.workers.addJob([safe, path, cancel = cancelScans] {
            auto listing = std::make_shared<const io::AudioFolderListing>(io::listAudioFiles(juce::File(path), kMaxPlaceFiles, kScanLimit, cancel.get()));
            if (cancel->load())
                return;
            juce::MessageManager::callAsync([safe, path, listing] {
                if (safe == nullptr)
                    return;
                safe->scanning.removeString(path);
                if (! safe->placeList.contains(path))
                    return;
                safe->listings[path] = listing;
                safe->layout();
            });
        });
    }

    const io::AudioFolderListing* listingFor(const juce::String& path) const
    {
        const auto it = listings.find(path);
        return it != listings.end() ? it->second.get() : nullptr;
    }

    juce::String countText(const juce::String& path) const
    {
        if (const auto* l = listingFor(path))
            return juce::String(l->found) + (l->complete ? "" : "+");
        return scanning.contains(path) ? juce::String::fromUTF8("\xe2\x80\xa6") : juce::String();
    }

    void addPlaceRows()
    {
        rows.push_back({ Row::Header, -1, "PLACES", {} });
        for (int i = 0; i < placeList.size(); ++i)
        {
            const juce::File folder(placeList[i]);
            const bool open = openPlaces.contains(placeList[i]);
            rows.push_back({ Row::Place, i, folder.getFileName().isNotEmpty() ? folder.getFileName() : folder.getFullPathName(), countText(placeList[i]), folder });
            if (! open)
                continue;
            const auto* l = listingFor(placeList[i]);
            if (l == nullptr)
            {
                rows.push_back({ Row::Note, -1, folder.isDirectory() ? "Scanning..." : "Folder not found", {}, {}, 1 });
                continue;
            }
            for (const auto& e : l->entries)
                rows.push_back({ Row::File, -1, e.relativePath, {}, e.file, 1 });
            if (l->entries.empty())
                rows.push_back({ Row::Note, -1, folder.isDirectory() ? "No audio files here" : "Folder not found", {}, {}, 1 });
            else if (l->truncated())
                rows.push_back({ Row::Note, -1, "Showing " + juce::String(static_cast<int>(l->entries.size())) + " of " + juce::String(l->found) + (l->complete ? "" : "+") + " files",
                                 {}, {}, 1 });
        }
        rows.push_back({ Row::AddPlace, -1, "+ Add folder...", {} });
    }

    void layout()
    {
        rows.clear();
        rows.push_back({ Row::Header, -1, "SCENES", "double-click the terrain to add" });
        const auto& scenes = model.core.scenes.getScenes();
        for (std::size_t k = 0; k < scenes.size(); ++k)
            rows.push_back({ Row::Scene, static_cast<int>(k), juce::String(scenes[k].name), k < 9 ? juce::String(static_cast<int>(k) + 1) : juce::String() });
        rows.push_back({ Row::Capture, -1, "+ Capture what you hear", "C" });
        const auto& sounds = factorySounds();
        const auto query = search.getText().trim();
        auto soundRow = [&](std::size_t k) {
            return Row { Row::Sound, static_cast<int>(k), sounds[k].name,
                         sounds[k].rootNote >= 0 ? Model::noteName(static_cast<float>(sounds[k].rootNote)) : juce::String() };
        };
        if (query.isNotEmpty())
        {
            rows.push_back({ Row::Header, -1, "MATCHING SOUNDS", {} });
            int found = 0;
            for (std::size_t k = 0; k < sounds.size(); ++k)
                if (juce::String(sounds[k].name).containsIgnoreCase(query) || juce::String(sounds[k].category).containsIgnoreCase(query))
                {
                    rows.push_back(soundRow(k));
                    ++found;
                }
            int files = 0;
            bool pending = false;
            for (const auto& p : placeList)
            {
                requestScan(p, false);
                pending = pending || scanning.contains(p);
                if (const auto* l = listingFor(p))
                    for (const auto* e : io::matchAudioFiles(*l, query, kMaxSearchFiles - files))
                    {
                        if (files == 0)
                            rows.push_back({ Row::Header, -1, "MATCHING FILES", {} });
                        rows.push_back({ Row::File, -1, e->file.getFileName(), {}, e->file });
                        ++files;
                    }
            }
            if (pending)
                rows.push_back({ Row::Note, -1, "Searching places...", {} });
            else if (files >= kMaxSearchFiles)
                rows.push_back({ Row::Note, -1, "First " + juce::String(kMaxSearchFiles) + " matching files", {} });
            if (found + files == 0 && ! pending)
                rows.push_back({ Row::Header, -1, "NOTHING MATCHES", {} });
        }
        else
        {
            bool anyFavourite = false;
            auto favouriteHeader = [&] {
                if (! anyFavourite)
                    rows.push_back({ Row::Header, -1, "FAVOURITES", {} });
                anyFavourite = true;
            };
            for (std::size_t k = 0; k < sounds.size(); ++k)
                if (favourites.contains(sounds[k].name))
                {
                    favouriteHeader();
                    rows.push_back(soundRow(k));
                }
            for (const auto& f : favourites)
                if (juce::File::isAbsolutePath(f))
                {
                    favouriteHeader();
                    rows.push_back({ Row::File, -1, juce::File(f).getFileName(), {}, juce::File(f) });
                }
            addPlaceRows();
            const char* category = "";
            for (std::size_t k = 0; k < sounds.size(); ++k)
            {
                if (juce::String(sounds[k].category) != category)
                {
                    category = sounds[k].category;
                    rows.push_back({ Row::Header, -1, juce::String(category).toUpperCase() + " SOUNDS", {} });
                }
                rows.push_back(soundRow(k));
            }
        }
        rows.push_back({ Row::Header, -1, "YOUR SOUNDS", {} });
        rows.push_back({ Row::Disk, -1, "Load from disk...", {} });
        list.setSize(std::max(10, viewport.getWidth() - (viewport.getVerticalScrollBar().isVisible() ? 8 : 0)), static_cast<int>(rows.size()) * kRowH + 6);
        list.repaint();
    }

    bool isFavourite(const Row& row) const
    {
        return row.kind == Row::File ? favourites.contains(row.file.getFullPathName()) : favourites.contains(row.text);
    }

    void drawPlay(juce::Graphics& g, juce::Rectangle<float> play, bool playing)
    {
        juce::Path p;
        const auto box = play.withSizeKeepingCentre(9.0f, 9.0f);
        if (playing)
            p.addRectangle(box);
        else
            p.addTriangle(box.getX(), box.getY(), box.getX(), box.getBottom(), box.getRight(), box.getCentreY());
        g.setColour(playing ? colour::accent() : colour::textDim());
        g.fillPath(p);
    }

    void paintRows(juce::Graphics& g)
    {
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto& row = rows[i];
            auto r = juce::Rectangle<float>(0.0f, static_cast<float>(i) * kRowH, static_cast<float>(list.getWidth()), static_cast<float>(kRowH));
            const bool hover = static_cast<int>(i) == list.hover && interactive(row.kind);
            if (hover)
            {
                g.setColour(colour::panelHi());
                g.fillRoundedRectangle(r.reduced(2.0f, 1.0f), metric::radius);
            }
            r = r.reduced(8.0f, 0.0f).withTrimmedLeft(12.0f * static_cast<float>(row.depth));
            switch (row.kind)
            {
                case Row::Header:
                    g.setFont(caps(10.0f));
                    g.setColour(colour::textFaint());
                    g.drawText(row.text, r.withTrimmedTop(6.0f), juce::Justification::centredLeft, true);
                    break;
                case Row::Note:
                    g.setFont(font(11.0f));
                    g.setColour(colour::textFaint());
                    g.drawText(row.text, r.withTrimmedLeft(18.0f), juce::Justification::centredLeft, true);
                    break;
                case Row::Scene:
                {
                    const auto c = colour::forScene(row.index);
                    g.setColour(c);
                    g.fillRoundedRectangle(r.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 10.0f), 2.0f);
                    r.removeFromLeft(8.0f);
                    const float w = static_cast<std::size_t>(row.index) < shownWeights.size() ? shownWeights[static_cast<std::size_t>(row.index)] : 0.0f;
                    auto bar = r.removeFromRight(34.0f).withSizeKeepingCentre(34.0f, 4.0f);
                    g.setColour(colour::track());
                    g.fillRoundedRectangle(bar, 2.0f);
                    g.setColour(c);
                    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, w)), 2.0f);
                    g.setColour(colour::textFaint());
                    g.setFont(font(10.5f, 500));
                    g.drawText(row.detail, r.removeFromRight(16.0f), juce::Justification::centred);
                    g.setColour(w > 0.3f ? colour::text() : colour::textDim());
                    g.setFont(font(12.5f, w > 0.3f ? 600 : 500));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    break;
                }
                case Row::Capture:
                case Row::Disk:
                case Row::AddPlace:
                    g.setColour(hover ? colour::accent() : colour::textDim());
                    g.setFont(font(12.0f, 500));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    g.setColour(colour::textFaint());
                    g.drawText(row.detail, r, juce::Justification::centredRight);
                    break;
                case Row::Place:
                {
                    const bool open = openPlaces.contains(row.file.getFullPathName());
                    const auto box = r.removeFromLeft(10.0f).withSizeKeepingCentre(8.0f, 8.0f);
                    juce::Path p;
                    if (open)
                        p.addTriangle(box.getX(), box.getY() + 1.0f, box.getRight(), box.getY() + 1.0f, box.getCentreX(), box.getBottom() - 0.5f);
                    else
                        p.addTriangle(box.getX() + 1.0f, box.getY(), box.getX() + 1.0f, box.getBottom(), box.getRight(), box.getCentreY());
                    g.setColour(hover ? colour::lift(colour::textDim(), 0.4f) : colour::textDim());
                    g.fillPath(p);
                    r.removeFromLeft(8.0f);
                    g.setColour(colour::textFaint());
                    g.setFont(font(10.5f, 500));
                    g.drawText(row.detail, r.removeFromRight(40.0f), juce::Justification::centredRight);
                    g.setColour(row.file.isDirectory() ? colour::text() : colour::textFaint());
                    g.setFont(font(12.5f, 600));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    break;
                }
                case Row::Sound:
                case Row::File:
                {
                    const bool isFile = row.kind == Row::File;
                    const auto icon = r.removeFromLeft(10.0f).withSizeKeepingCentre(8.0f, 8.0f);
                    g.setColour(colour::tide().withAlpha(0.8f));
                    if (isFile)
                        g.drawEllipse(icon.reduced(0.5f), 1.2f);
                    else
                        g.fillEllipse(icon);
                    r.removeFromLeft(8.0f);
                    const bool playing = previewKey.isNotEmpty() && previewKey == keyFor(row);
                    auto play = r.removeFromRight(kPlayW);
                    if (hover || playing)
                        drawPlay(g, play, playing);
                    if (! isFile)
                    {
                        g.setColour(colour::textFaint());
                        g.setFont(font(10.5f, 500));
                        g.drawText(row.detail, r.removeFromRight(30.0f), juce::Justification::centredRight);
                    }
                    if (isFavourite(row))
                    {
                        g.setColour(colour::accent());
                        g.setFont(font(12.0f, 500));
                        g.drawText(juce::String::fromUTF8("\xe2\x98\x85"), r.removeFromRight(14.0f), juce::Justification::centred);
                    }
                    g.setColour(colour::text());
                    g.setFont(font(isFile ? 12.0f : 12.5f, 500));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    break;
                }
            }
        }
    }

    static juce::String keyFor(const Row& row)
    {
        if (row.kind == Row::Sound)
            return "sound:" + juce::String(row.index);
        if (row.kind == Row::File)
            return "file:" + row.file.getFullPathName();
        return {};
    }

    const Row* rowAt(int i) const { return i >= 0 && i < static_cast<int>(rows.size()) ? &rows[static_cast<std::size_t>(i)] : nullptr; }

    void hovered(int i)
    {
        const auto* row = rowAt(i);
        if (model.onHover == nullptr || row == nullptr)
            return;
        if (row->kind == Row::Scene)
            model.onHover("Scene \"" + row->text + "\": click to glide there (or press " + row->detail + "), double-click to rename, right-click for more");
        else if (row->kind == Row::Sound)
            model.onHover(row->text + ": click to load it into a cloud or Bloom, drag it onto the terrain or a device, the triangle to preview, right-click to favourite"
                          + (row->detail.isNotEmpty() ? " (sounds at " + row->detail + ")" : juce::String()));
        else if (row->kind == Row::File)
            model.onHover(row->file.getFullPathName() + ": click to load, drag onto the terrain or a device, the triangle to preview, right-click for more");
        else if (row->kind == Row::Place)
            model.onHover(row->file.getFullPathName() + ": click to open or close, right-click to rescan, show or remove");
        else if (row->kind == Row::AddPlace)
            model.onHover("Add a folder of your own sounds to Places (or drop a folder from Finder onto the browser)");
        else if (row->kind == Row::Capture)
            model.onHover("Capture: store everything you hear now as a scene at the cursor (C)");
    }

    void toggleFavourite(const juce::String& key)
    {
        favourites.contains(key) ? favourites.removeString(key) : favourites.add(key);
        model.core.host.getSettings().setValue(kFavouritesKey, favourites.joinIntoString("\n"));
        layout();
    }

    void showFileMenu(const Row& row)
    {
        juce::PopupMenu m;
        m.addSectionHeader(row.file.getFileName());
        const auto key = row.file.getFullPathName();
        const bool fav = favourites.contains(key);
        m.addItem(1, fav ? "Remove from favourites" : "Add to favourites");
        m.addItem(2, previewKey == keyFor(row) ? "Stop preview" : "Preview");
        m.addItem(3, "Add to Bloom's keyboard", model.engine.getBloomSample() != nullptr);
        m.addSeparator();
        m.addItem(4, places::revealName());
        juce::Component::SafePointer<Browser> safe(this);
        showMenu(m, this, [safe, row, key](int r) {
            if (r == 1)
                safe->toggleFavourite(key);
            else if (r == 2)
                safe->togglePreview(row);
            else if (r == 3)
                safe->view.loadFiles({ row.file }, engine::kNumClouds + 1);
            else if (r == 4)
                row.file.revealToUser();
        });
    }

    void showPlaceMenu(const Row& row)
    {
        juce::PopupMenu m;
        const auto path = row.file.getFullPathName();
        m.addSectionHeader(path);
        m.addItem(1, openPlaces.contains(path) ? "Close" : "Open");
        m.addItem(2, "Rescan", row.file.isDirectory());
        m.addItem(3, places::revealName(), row.file.isDirectory());
        m.addSeparator();
        m.addItem(4, "Remove place");
        juce::Component::SafePointer<Browser> safe(this);
        showMenu(m, this, [safe, row, path](int r) {
            if (r == 1)
                safe->togglePlace(path);
            else if (r == 2)
            {
                safe->requestScan(path, true);
                safe->layout();
            }
            else if (r == 3)
                row.file.revealToUser();
            else if (r == 4)
            {
                safe->openPlaces.removeString(path);
                safe->saveOpenPlaces();
                places::remove(safe->model.core.host.getSettings(), path);
            }
        });
    }

    void togglePlace(const juce::String& path)
    {
        if (openPlaces.contains(path))
            openPlaces.removeString(path);
        else
        {
            openPlaces.add(path);
            requestScan(path, true);
        }
        saveOpenPlaces();
        layout();
    }

    void clicked(int i, const juce::MouseEvent& e)
    {
        const auto* found = rowAt(i);
        if (found == nullptr)
            return;
        const auto row = *found;
        const bool popup = e.mods.isPopupMenu();
        const bool onPlay = e.getMouseDownX() >= list.getWidth() - 8 - static_cast<int>(kPlayW);
        auto& v = view;
        if (row.kind == Row::Scene)
        {
            if (popup)
                return showSceneMenu(model, row.index, this);
            const auto p = model.core.scenes.getScenes()[static_cast<std::size_t>(row.index)].position;
            model.set(P::TerrainX, p.x);
            model.set(P::TerrainY, p.y);
        }
        else if (row.kind == Row::Capture && ! popup)
            model.core.captureSceneAtCursor();
        else if (row.kind == Row::Place)
            popup ? showPlaceMenu(row) : togglePlace(row.file.getFullPathName());
        else if (row.kind == Row::AddPlace && ! popup)
            chooseFolder();
        else if (row.kind == Row::File && popup)
            showFileMenu(row);
        else if (row.kind == Row::Sound && popup)
        {
            juce::PopupMenu m;
            m.addSectionHeader(row.text);
            const bool fav = favourites.contains(row.text);
            m.addItem(1, fav ? "Remove from favourites" : "Add to favourites");
            m.addItem(2, previewKey == keyFor(row) ? "Stop preview" : "Preview");
            juce::Component::SafePointer<Browser> safe(this);
            showMenu(m, this, [safe, row](int r) {
                if (r == 1)
                    safe->toggleFavourite(row.text);
                else if (r == 2)
                    safe->togglePreview(row);
            });
        }
        else if ((row.kind == Row::Sound || row.kind == Row::File) && onPlay)
            togglePreview(row);
        else if (row.kind == Row::Sound)
            v.showLoadMenu("Load " + row.text + " into", factorySounds()[static_cast<std::size_t>(row.index)].rootNote >= 0,
                           [&v, index = row.index](int slot) { v.loadFactory(index, slot); });
        else if (row.kind == Row::File)
            v.showLoadMenu("Load " + row.file.getFileName() + " into", true, [&v, file = row.file](int slot) { v.loadFiles({ file }, slot); });
        else if (row.kind == Row::Disk && ! popup)
            v.showLoadMenu("Load a sound from disk into", false, [&v](int slot) { v.chooseSample(slot); });
    }

    bool startDrag(int i)
    {
        const auto* row = rowAt(i);
        if (row == nullptr || (row->kind != Row::Sound && row->kind != Row::File))
            return false;
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container == nullptr)
            return false;
        const auto description = row->kind == Row::Sound ? MainView::dragFactorySound(row->index) : MainView::dragFile(row->file);
        const auto name = row->kind == Row::Sound ? row->text : row->file.getFileName();
        const float scale = 2.0f;
        const int w = juce::jlimit(60, 220, juce::GlyphArrangement::getStringWidthInt(font(12.5f, 500), name) + 34);
        juce::Image image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(w) * scale), juce::roundToInt(static_cast<float>(kRowH) * scale), true);
        {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));
            const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(kRowH)).reduced(1.0f);
            g.setColour(colour::panelHi().withAlpha(0.95f));
            g.fillRoundedRectangle(r, metric::radius);
            g.setColour(colour::accent());
            g.drawRoundedRectangle(r, metric::radius, 1.0f);
            g.setColour(colour::tide());
            g.fillEllipse(r.getX() + 8.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour(colour::text());
            g.setFont(font(12.5f, 500));
            g.drawText(name, r.withTrimmedLeft(24.0f).withTrimmedRight(6.0f), juce::Justification::centredLeft, true);
        }
        container->startDragging(description, &list, juce::ScaledImage(image, scale), row->kind == Row::File);
        return true;
    }

    void doubleClicked(int i)
    {
        if (const auto* row = rowAt(i); row != nullptr && row->kind == Row::Scene)
            showSceneMenu(model, row->index, this);
    }

    void togglePreview(const Row& row)
    {
        const auto key = keyFor(row);
        if (key == previewKey)
        {
            model.engine.previewSample(nullptr);
            previewKey = {};
            ++previewToken;
            list.repaint();
            return;
        }
        previewKey = key;
        const int token = ++previewToken;
        list.repaint();
        juce::Component::SafePointer<Browser> safe(this);
        std::function<std::shared_ptr<const dsp::SampleBuffer>()> load;
        if (row.kind == Row::Sound)
            load = [sound = factorySounds()[static_cast<std::size_t>(row.index)]] { return loadFactorySound(sound); };
        else
            load = [file = row.file]() -> std::shared_ptr<const dsp::SampleBuffer> {
                juce::String error;
                return std::shared_ptr<const dsp::SampleBuffer>(io::loadSample(file, error, 60.0));
            };
        model.core.workers.addJob([safe, load, token] {
            auto buffer = load();
            juce::MessageManager::callAsync([safe, buffer, token] {
                if (safe == nullptr || token != safe->previewToken)
                    return;
                if (buffer == nullptr)
                {
                    safe->previewKey = {};
                    safe->list.repaint();
                    return safe->model.core.status("Could not read that file", true);
                }
                safe->model.engine.previewSample(buffer);
                juce::Timer::callAfterDelay(static_cast<int>(buffer->seconds() * 1000.0) + 50, [safe, token] {
                    if (safe != nullptr && token == safe->previewToken)
                    {
                        safe->previewKey = {};
                        safe->list.repaint();
                    }
                });
            });
        });
    }

    static constexpr float kPlayW = 18.0f;
    static constexpr const char* kFavouritesKey = "favouriteSounds";

    Model& model;
    MainView& view;
    juce::TextEditor search;
    juce::StringArray favourites;
    juce::StringArray placeList, openPlaces, scanning;
    std::map<juce::String, std::shared_ptr<const io::AudioFolderListing>> listings;
    std::shared_ptr<std::atomic<bool>> cancelScans = std::make_shared<std::atomic<bool>>(false);
    int shownPlacesVersion = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String previewKey;
    int previewToken = 0;
    juce::Viewport viewport;
    List list;
    std::vector<Row> rows;
    std::uint64_t shownVersion = ~std::uint64_t { 0 };
    std::vector<float> shownWeights;
};

class MacroPanel final : public juce::Component
{
public:
    explicit MacroPanel(Model& m)
        : tide(m, P::TideRate, "the speed of everything that moves: drift, rain, wander, loops"),
          wander(m, P::TerrainWander, "how far the sound strays from your cursor on its own"),
          gravity(m, P::HarmonyGravity, "how strongly every source is pulled into the key"),
          glide(m, P::TerrainGlide, "how long the sound takes to arrive where you point"),
          root(m, P::HarmonyRoot, 6, "the key: every source retunes to it over the key-morph time"),
          scale(m, P::HarmonyScale, "the scale every source plays in"),
          medium(m, P::MediumType, 4, "what the whole piece sounds recorded on"),
          style(m, P::TerrainWanderStyle, 3, "how the sound wanders: drift, orbit, tide pool, a journey between scenes, or along a path you draw")
    {
        tide.setLabel("Tide");
        wander.setLabel("Wander");
        gravity.setLabel("Gravity");
        glide.setLabel("Glide");
        for (auto* c : std::initializer_list<juce::Component*> { &tide, &wander, &gravity, &glide, &root, &scale, &medium, &style })
            addAndMakeVisible(c);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop(metric::header).reduced(metric::pad, 8);
        auto faders = r.removeFromTop(juce::jlimit(60, 170, r.getHeight() - 240));
        const int w = (faders.getWidth() - 3 * 6) / 4;
        for (auto* f : { &tide, &wander, &gravity, &glide })
        {
            f->setBounds(faders.removeFromLeft(w));
            faders.removeFromLeft(6);
        }
        r.removeFromTop(8);
        labels.clear();
        const int labelH = r.getHeight() >= 4 * 16 + 48 + 24 + 48 + 24 + 4 * 6 ? 16 : 0;
        auto place = [&](juce::Component& c, const juce::String& label, int h) {
            if (labelH > 0)
                labels.push_back({ r.removeFromTop(labelH), label });
            c.setBounds(r.removeFromTop(h));
            r.removeFromTop(6);
        };
        place(root, "Key", 48);
        place(scale, "Scale", 24);
        place(style, "Wander", 48);
        place(medium, "Recorded on", 24);
    }

    void paint(juce::Graphics& g) override
    {
        drawPanel(g, getLocalBounds().toFloat(), "Performance");
        g.setFont(font(11.0f, 500));
        g.setColour(colour::textDim());
        for (const auto& [r, text] : labels)
            g.drawText(text, r, juce::Justification::bottomLeft);
    }

private:
    Fader tide, wander, gravity, glide;
    Choice root;
    MenuBox scale;
    Choice medium, style;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};

class PadRow final : public juce::Component
{
public:
    explicit PadRow(Model& m) : model(m), shape(m)
    {
        auto hold = [this](const char* title, const char* key, const char* sub, juce::Colour c, const char* help, P p, std::function<float()> level) {
            auto pad = std::make_unique<Pad>(model, title, sub, c, help);
            pad->keyCap = key;
            pad->onPress = [this, p] { model.set(p, 1.0f); };
            pad->onRelease = [this, p] { model.set(p, 0.0f); };
            pad->level = std::move(level);
            pad->lit = [this, p] { return model.value(p) > 0.5f; };
            pads.push_back(std::move(pad));
        };
        auto toggle = [this](const char* title, const char* key, const char* sub, juce::Colour c, const char* help, P p, std::function<float()> level) {
            auto pad = std::make_unique<Pad>(model, title, sub, c, help);
            pad->keyCap = key;
            pad->onPress = [this, p] { model.toggle(p); };
            pad->level = std::move(level);
            pad->lit = [this, p] { return model.value(p) > 0.5f; };
            pads.push_back(std::move(pad));
        };
        const auto& f = [this]() -> const engine::TelemetryFrame& { return model.frame(); };
        hold("Swell", "S", "sends bloom", colour::accent(), "hold to swell: sends bloom and filters open; let go and it ebbs back", P::SwellHold, [f] { return f().swell; });
        hold("Hush", "H", "sources sink", colour::forScene(4), "hold to hush: every source sinks while the reverb and delay ring on", P::HushHold, [f] { return f().hush; });
        hold("Slow", "T", "quarter time", colour::forScene(2), "hold to slow time to a quarter: every drift and cycle with it", P::SlowHold, [f] { return f().slow; });
        toggle("Freeze all", "F", "the moment", colour::tide(), "hold the last two seconds as a cloud while the rest steps back", P::FreezeOn,
               [f] { return f().freezeGain; });
        toggle("Hold input", "I", "spectral pad", colour::forScene(9), "hold the live input's sound forever as a spectral pad", P::InputFreeze,
               [f] { return f().inputFreeze; });

        auto loop = std::make_unique<Pad>(model, "Loop", "", colour::warn(), "record, close the loop, overdub; every pass wears the tape a little more");
        loop->keyCap = "L";
        loop->onPress = [this] {
            if (juce::ModifierKeys::currentModifiers.isShiftDown())
                model.engine.command(engine::Command::LoopClear);
            else
                model.engine.command(engine::Command::LoopRecord);
        };
        loop->level = [f] { return f().loopState >= 2 ? f().loopPosition : (f().loopState == 1 ? 1.0f : 0.0f); };
        loop->lit = [f] { return f().loopState == 1 || f().loopState == 3; };
        loop->subText = [f] {
            static const char* s[] = { "empty", "recording", "looping", "overdubbing", "clearing" };
            return juce::String(s[juce::jlimit(0, 4, f().loopState)]);
        };
        pads.push_back(std::move(loop));

        toggle("Cycles", "E", "never align", colour::forScene(7), "notes on long cycles that never line up, always in the key", P::LoopsOn, [f] {
            float m = 0.0f;
            for (float v : f().loopFlash)
                m = std::max(m, v);
            return m * 0.8f;
        });

        auto gesture = std::make_unique<Pad>(model, "Take", "", colour::learn(),
                                             "record your moves (knobs, terrain, notes) and play them back, looped; right-click for more");
        gesture->keyCap = "G";
        gesture->onPress = [this] { gestureToggle(model.core, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        gesture->onMenu = [this] { showGestureMenu(model.core, this); };
        gesture->level = [f] {
            const auto& fr = f();
            if (fr.gestureState == engine::GestureState::Playing && fr.gestureLength > 0.0f)
                return std::fmod(fr.gestureSeconds, fr.gestureLength) / fr.gestureLength;
            return fr.gestureState == engine::GestureState::Recording ? 1.0f : 0.0f;
        };
        gesture->lit = [f] { return f().gestureState != engine::GestureState::Idle; };
        gesture->subText = [this, f] {
            const auto& fr = f();
            if (fr.gestureState == engine::GestureState::Recording)
                return "recording " + juce::String(fr.gestureSeconds, 1) + " s";
            if (fr.gestureState == engine::GestureState::Playing)
                return "playing " + juce::String(std::fmod(fr.gestureSeconds, std::max(0.01f, fr.gestureLength)), 1) + " / " + juce::String(fr.gestureLength, 1) + " s";
            return model.core.gestures.hasTake() ? "ready" : juce::String("empty");
        };
        pads.push_back(std::move(gesture));

        auto catchPad = std::make_unique<Pad>(model, "Catch", "last seconds", colour::live(), "grab what just happened into a cloud, where it keeps playing");
        catchPad->keyCap = "K";
        catchPad->onPress = [this] { model.engine.command(engine::Command::Catch); };
        pads.push_back(std::move(catchPad));

        for (auto& p : pads)
            addAndMakeVisible(*p);
        addAndMakeVisible(shape);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = static_cast<int>(pads.size()) + 1;
        const int unit = (r.getWidth() - (n - 1) * metric::gap) / (n + 1);
        for (std::size_t k = 0; k < pads.size(); ++k)
        {
            if (k == 3)
            {
                shape.setBounds(r.removeFromLeft(unit * 2));
                r.removeFromLeft(metric::gap);
            }
            pads[k]->setBounds(r.removeFromLeft(unit));
            r.removeFromLeft(metric::gap);
        }
    }

private:
    Model& model;
    std::vector<std::unique_ptr<Pad>> pads;
    ShapePad shape;
};

class StatusBar final : public juce::Component, public Animated
{
public:
    StatusBar(Model& m, MainView& v) : model(m), view(v) { model.add(this); }
    ~StatusBar() override { model.remove(this); }

    void setHelp(const juce::String& h)
    {
        if (h != help)
        {
            help = h;
            repaint();
        }
    }
    void showMessage(const juce::String& m, bool warning)
    {
        message = m;
        warn = warning;
        messageTime = juce::Time::getMillisecondCounterHiRes();
        repaint();
    }

    void tick() override
    {
        const double age = (juce::Time::getMillisecondCounterHiRes() - messageTime) / 1000.0;
        const float a = message.isEmpty() ? 0.0f : static_cast<float>(juce::jlimit(0.0, 1.0, 7.0 - age));
        const juce::String keys = view.noteMode ? "Keys: octave " + juce::String(view.octave) + ", velocity " + juce::String(juce::roundToInt(view.velocity * 100.0f)) : juce::String();
        if (std::abs(a - alpha) > 0.01f || keys != keysText)
        {
            alpha = a;
            keysText = keys;
            if (a <= 0.0f)
                message.clear();
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colour::header());
        g.setColour(colour::line());
        g.fillRect(getLocalBounds().removeFromTop(1));
        auto r = getLocalBounds().reduced(10, 0).toFloat();
        g.setFont(font(11.5f, 500));
        if (keysText.isNotEmpty())
        {
            g.setColour(colour::tide());
            g.drawText(keysText, r.removeFromRight(190.0f), juce::Justification::centredRight);
        }
        if (message.isNotEmpty() && alpha > 0.0f)
        {
            g.setColour((warn ? colour::warn() : colour::accent()).withAlpha(alpha));
            const float w = std::min(r.getWidth() * 0.5f, juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), message) + 20.0f);
            g.drawText(message, r.removeFromRight(w), juce::Justification::centredRight, true);
        }
        g.setColour(colour::textDim());
        g.drawText(help.isNotEmpty() ? help
                                     : juce::String("Space fade   Esc panic   drag the terrain to move   1-9 scenes   C capture   S/H/T hold gestures   G record a take   P draw a path   M keys   Tab pages"),
                   r, juce::Justification::centredLeft, true);
    }

private:
    Model& model;
    MainView& view;
    juce::String help, message, keysText;
    bool warn = false;
    double messageTime = 0.0;
    float alpha = 0.0f;
};

MainView::MainView(AppCore& c) : core(c), model(c)
{
    if (openViews.empty())
    {
        setTheme(themeFromId(core.host.getSettings().getValue(kThemeKey)));
        lookAndFeel->applyPalette();
    }
    openViews.push_back(this);
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel.get());
    setOpaque(true);
    setWantsKeyboardFocus(true);

    buildInterface();

    model.onHover = [this](const juce::String& h) {
        if (hoverHelpEnabled(core))
            status->setHelp(h);
    };
    core.onStatus = [this](const juce::String& m, bool warning) { status->showMessage(m, warning); };
    core.onSessionChanged = [this] {
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName("Tidefield - " + core.session.getName());
    };

    if (core.osc != nullptr)
    {
        oscSceneFallback = core.osc->onScene;
        core.osc->onScene = [this](int index, bool jump) { glideToScene(index, jump); };
    }
        vblank = std::make_unique<juce::VBlankAttachment>(this, [this] { frame(); });
    startTimerHz(4);
    setSize(1440, 900);
}

MainView::~MainView()
{
    closeSettings();
    stopTimer();
    projector.reset();
    vblank.reset();
    releaseHolds();
    core.onStatus = nullptr;
    core.onSessionChanged = nullptr;
    if (core.osc != nullptr)
        core.osc->onScene = oscSceneFallback;
    model.onHover = nullptr;
    teardownInterface();
    openViews.erase(std::remove(openViews.begin(), openViews.end(), this), openViews.end());
    if (openViews.empty())
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainView::buildInterface()
{
    topBar = std::make_unique<TopBar>(model, *this);
    browser = std::make_unique<Browser>(model, *this);
    terrain = std::make_unique<TerrainView>(model);
    macros = std::make_unique<MacroPanel>(model);
    pads = std::make_unique<PadRow>(model);
    devices = std::make_unique<DeviceView>(model);
    status = std::make_unique<StatusBar>(model, *this);
    devices->onLoadSample = [this](int slot) { chooseSample(slot); };
    for (auto* comp : std::initializer_list<juce::Component*> { topBar.get(), browser.get(), terrain.get(), macros.get(), pads.get(), devices.get(), status.get() })
        addAndMakeVisible(comp);
}

void MainView::teardownInterface()
{
    status.reset();
    devices.reset();
    pads.reset();
    macros.reset();
    terrain.reset();
    browser.reset();
    topBar.reset();
}

void MainView::rebuildInterface()
{
    const int page = devices->getPage();
    const int chain = devices->getEffectsChain();
    const bool projecting = projector != nullptr;
    projector.reset();
    teardownInterface();
    buildInterface();
    if (page == DeviceView::Effects)
        devices->showEffectsFor(chain);
    else
        devices->show(page);
    if (projecting)
        toggleProjector();
    resized();
    repaint();
}

void MainView::switchTheme(AppCore& core, Theme t)
{
    if (t == theme())
        return;
    core.host.getSettings().setValue(kThemeKey, themeId(t));
    core.host.getSettings().saveIfNeeded();
    setTheme(t);
    if (openViews.empty())
        return;
    auto* first = openViews.front();
    first->lookAndFeel->applyPalette();
    for (auto* v : openViews)
        later(v, [v] { v->rebuildInterface(); });
}

void MainView::toggleProjector()
{
    if (projector != nullptr)
    {
        projector.reset();
        return;
    }
    juce::Component::SafePointer<MainView> safe(this);
    projector = std::make_unique<ProjectorWindow>(model, getTopLevelComponent(), [safe] {
        if (safe != nullptr)
            safe->projector.reset();
    });
}

void MainView::frame()
{
    model.tick();
}

void MainView::timerCallback()
{
    if (! juce::Process::isForegroundProcess())
    {
        releaseHolds();
        return;
    }
    if (! core.host.isPlugin() && juce::Component::getCurrentlyFocusedComponent() == nullptr && isShowing()
        && ! juce::ModalComponentManager::getInstance()->getNumModalComponents())
        grabKeyboardFocus();
}

void MainView::parentHierarchyChanged()
{
    juce::Component::SafePointer<MainView> safe(this);
    juce::MessageManager::callAsync([safe] {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

void MainView::paint(juce::Graphics& g) { g.fillAll(colour::window()); }

void MainView::resized()
{
    auto r = getLocalBounds();
    topBar->setBounds(r.removeFromTop(kTopH));
    status->setBounds(r.removeFromBottom(kStatusH));
    r.reduce(metric::gap, metric::gap);
    const int deviceH = juce::jlimit(214, 300, getHeight() * 30 / 100);
    devices->setBounds(r.removeFromBottom(deviceH));
    r.removeFromBottom(metric::gap);
    browser->setBounds(r.removeFromLeft(kBrowserW));
    r.removeFromLeft(metric::gap);
    pads->setBounds(r.removeFromBottom(kPadsH));
    r.removeFromBottom(metric::gap);
    macros->setBounds(r.removeFromRight(kMacroW));
    r.removeFromRight(metric::gap);
    terrain->setBounds(r);
}

void MainView::releaseHolds()
{
    for (auto& h : holds)
        if (h.down)
        {
            h.down = false;
            model.set(h.param, 0.0f);
        }
    for (std::size_t k = 0; k < keyNote.size(); ++k)
        if (keyNote[k] != 0)
        {
            core.engine.noteOff(keyNote[k] - 1);
            keyNote[k] = 0;
        }
}

void MainView::focusLost(FocusChangeType) { releaseHolds(); }

bool MainView::keyStateChanged(bool)
{
    for (auto& h : holds)
        if (h.down && ! juce::KeyPress::isKeyCurrentlyDown(h.keyCode))
        {
            h.down = false;
            model.set(h.param, 0.0f);
        }
    for (std::size_t k = 0; k < keyNote.size(); ++k)
        if (keyNote[k] != 0 && ! juce::KeyPress::isKeyCurrentlyDown(static_cast<int>(k)))
        {
            core.engine.noteOff(keyNote[k] - 1);
            keyNote[k] = 0;
        }
    return true;
}

bool MainView::handleNoteKey(const juce::KeyPress& key)
{
    static const juce::String row = "AWSEDFTGYHUJKOLP;'";
    const int code = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    const int semitone = row.indexOfChar(static_cast<juce::juce_wchar>(code));
    if (semitone >= 0 && code < static_cast<int>(keyNote.size()))
    {
        if (keyNote[static_cast<std::size_t>(code)] == 0)
        {
            const int note = juce::jlimit(0, 127, (octave + 1) * 12 + semitone);
            core.engine.noteOn(note, velocity);
            keyNote[static_cast<std::size_t>(code)] = note + 1;
        }
        return true;
    }
    if (code == 'Z' || code == 'X')
    {
        octave = juce::jlimit(0, 8, octave + (code == 'X' ? 1 : -1));
        return true;
    }
    if (code == 'C' || code == 'V')
    {
        velocity = juce::jlimit(0.1f, 1.0f, velocity + (code == 'V' ? 0.1f : -0.1f));
        return true;
    }
    return false;
}

void MainView::glideToScene(int index, bool jump)
{
    const auto& scenes = core.scenes.getScenes();
    if (index < 0 || index >= static_cast<int>(scenes.size()))
        return;
    const auto p = scenes[static_cast<std::size_t>(index)].position;
    if (jump)
    {
        model.jumpTerrain(p);
    }
    else
    {
        model.set(P::TerrainX, p.x);
        model.set(P::TerrainY, p.y);
    }
    core.status("Gliding to " + juce::String(scenes[static_cast<std::size_t>(index)].name));
}

bool MainView::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode()));

    if (mods.isCommandDown())
    {
        if (code == 'Z')
            mods.isShiftDown() ? core.undo.redo() : core.undo.undo();
        else if (code == 'S')
            mods.isShiftDown() ? core.session.saveAs() : core.session.save();
        else if (code == 'O')
            core.session.open();
        else if (code == 'N')
            core.session.newSession();
        else if (code == ',')
            openSettings();
        else if (code == '=' || code == '+' || code == '-' || code == '0')
            setInterfaceScale(core, code == '0' ? 1.0f : interfaceScale(core) + (code == '-' ? -0.1f : 0.1f));
        else if (code == 'P')
            toggleProjector();
        else
            return false;
        return true;
    }

    if (key == juce::KeyPress::spaceKey && ! core.host.isPlugin())
    {
        const auto st = model.frame().fadeState;
        core.engine.command(st == engine::FadeState::Silent || st == engine::FadeState::FadingOut ? engine::Command::FadeIn : engine::Command::FadeOut);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && terrain->isDrawMode())
    {
        terrain->setDrawMode(false);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        core.engine.command(model.frame().panicActive ? engine::Command::ResumeFromPanic : engine::Command::Panic);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        devices->show((devices->getPage() + (mods.isShiftDown() ? DeviceView::NumPages - 1 : 1)) % DeviceView::NumPages);
        return true;
    }
    const bool arrow = key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey
                       || key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey;
    if (arrow)
    {
        const float step = mods.isShiftDown() ? 0.01f : 0.05f;
        const auto c = model.frame().cursor;
        if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
            model.set(P::TerrainX, juce::jlimit(0.0f, 1.0f, c.x + (key.getKeyCode() == juce::KeyPress::rightKey ? step : -step)));
        else
            model.set(P::TerrainY, juce::jlimit(0.0f, 1.0f, c.y + (key.getKeyCode() == juce::KeyPress::upKey ? step : -step)));
        return true;
    }
    if (code >= '1' && code <= '9')
    {
        glideToScene(code - '1', mods.isShiftDown());
        return true;
    }
    if (code == 'M')
    {
        noteMode = ! noteMode;
        if (! noteMode)
            releaseHolds();
        core.status(noteMode ? "Keys on: A W S E D F T G Y H U J K play Bloom, Z/X octave, C/V velocity. M to leave." : "Keys off: letters are gestures again");
        return true;
    }
    if (noteMode && handleNoteKey(key))
        return true;

    for (auto& h : holds)
        if (code == h.keyCode)
        {
            if (! h.down)
            {
                h.down = true;
                model.set(h.param, 1.0f);
            }
            return true;
        }

    switch (code)
    {
        case 'F': model.toggle(P::FreezeOn); break;
        case 'I': model.toggle(P::InputFreeze); break;
        case 'E': model.toggle(P::LoopsOn); break;
        case 'L': core.engine.command(mods.isShiftDown() ? engine::Command::LoopClear : engine::Command::LoopRecord); break;
        case 'K': core.engine.command(engine::Command::Catch); break;
        case 'G': gestureToggle(core, mods.isShiftDown()); break;
        case 'P': terrain->setDrawMode(! terrain->isDrawMode()); break;
        case 'C': core.captureSceneAtCursor(); break;
        case 'R':
            if (mods.isShiftDown())
                core.toggleRecording();
            else
            {
                core.scenes.releaseLiveLayer();
                core.status("Every held control handed back to the terrain");
            }
            break;
        default: break;
    }
    return ! core.host.isPlugin() || std::string_view("FIELKCRPSHTMG").find(static_cast<char>(code)) != std::string_view::npos;
}

namespace {
constexpr const char* kDragSound = "tidefield-sound:";
constexpr const char* kDragFile = "tidefield-file:";
constexpr int kAddToKeyboard = engine::kNumClouds + 1;

void addZonesToBloom(AppCore& core, Model& model, std::vector<engine::Engine::BloomZone> added, const juce::String& what)
{
    auto zones = core.engine.getBloomZones();
    if (zones.size() == 1 && zones.front().root < 0.0f)
        zones.front().root = model.value(P::BloomRoot);
    const auto maxZones = static_cast<std::size_t>(dsp::BloomSampler::kMaxZones);
    if (zones.size() >= maxZones)
        return core.status("Bloom already plays 8 sounds; load one sound to start again.", true);
    if (added.size() > maxZones - zones.size())
        added.resize(maxZones - zones.size());
    zones.insert(zones.end(), added.begin(), added.end());
    std::sort(zones.begin(), zones.end(), [](const auto& x, const auto& y) { return x.root < y.root; });
    core.engine.loadBloomZones(zones);
    core.status("Added " + what + " to Bloom's keyboard (" + juce::String(static_cast<int>(zones.size())) + " sounds)");
}

juce::String slotName(int slot)
{
    if (slot == engine::kNumClouds)
        return "Bloom";
    if (slot == kAddToKeyboard)
        return "Bloom's keyboard";
    return "Cloud " + juce::String(slot + 1);
}
}

void MainView::chooseSample(int slot)
{
    const bool isBloom = slot == engine::kNumClouds;
    chooser = std::make_unique<juce::FileChooser>(isBloom ? juce::String("Load one or more sounds into Bloom") : "Load a sample into Cloud " + juce::String(slot + 1),
                                                  juce::File(), io::kAudioFileWildcard);
    juce::Component::SafePointer<MainView> safe(this);
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                      | (isBloom ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    chooser->launchAsync(flags, [safe, slot](const juce::FileChooser& fc) {
        if (safe != nullptr)
            safe->loadFiles(fc.getResults(), slot);
    });
}

void MainView::loadFiles(const juce::Array<juce::File>& files, int slot)
{
    if (files.isEmpty() || slot < 0 || slot > kAddToKeyboard)
        return;
    juce::Component::SafePointer<MainView> safe(this);
    core.workers.addJob([safe, slot, files] {
        const bool forBloom = slot >= engine::kNumClouds;
        std::vector<engine::Engine::BloomZone> zones;
        juce::String error, firstName;
        std::optional<dsp::PitchEstimate> firstPitch;
        for (const auto& file : files)
        {
            juce::String fileError;
            std::shared_ptr<dsp::SampleBuffer> buffer(io::loadSample(file, fileError).release());
            if (buffer == nullptr)
            {
                error = fileError;
                continue;
            }
            const auto pitch = forBloom ? dsp::detectPitch(*buffer) : std::nullopt;
            if (zones.empty())
            {
                firstName = buffer->name;
                firstPitch = pitch;
            }
            zones.push_back({ buffer, pitch.has_value() ? std::round(pitch->midiNote) : 60.0f });
            if (zones.size() >= static_cast<std::size_t>(dsp::BloomSampler::kMaxZones) || ! forBloom)
                break;
        }
        juce::MessageManager::callAsync([safe, slot, zones, error, firstName, firstPitch]() mutable {
            if (safe == nullptr)
                return;
            if (zones.empty())
                return safe->core.status(error.isNotEmpty() ? error : juce::String("Nothing to load"), true);
            if (slot < engine::kNumClouds)
            {
                safe->core.engine.loadCloudSample(slot, zones.front().buffer);
                const auto level = engine::kStrips[static_cast<std::size_t>(slot + 1)].level;
                if (safe->model.value(level) <= -59.0f)
                    safe->model.set(level, -6.0f);
                return safe->core.status("Loaded " + firstName + " into Cloud " + juce::String(slot + 1));
            }
            if (slot == kAddToKeyboard)
            {
                const auto what = zones.size() == 1 ? firstName : juce::String(static_cast<int>(zones.size())) + " sounds";
                return addZonesToBloom(safe->core, safe->model, std::move(zones), what);
            }
            if (zones.size() == 1)
            {
                zones.front().root = -1.0f;
                safe->core.engine.loadBloomZones(zones);
                if (firstPitch.has_value())
                {
                    const float root = std::round(firstPitch->midiNote);
                    safe->model.set(P::BloomRoot, root);
                    return safe->core.status("Loaded " + firstName + " into Bloom, tuned to " + Model::noteName(root));
                }
                return safe->core.status("Loaded " + firstName + " into Bloom, no clear pitch: set Sample Root by ear");
            }
            std::sort(zones.begin(), zones.end(), [](const auto& x, const auto& y) { return x.root < y.root; });
            safe->core.engine.loadBloomZones(zones);
            juce::String roots;
            for (const auto& z : zones)
                roots << (roots.isEmpty() ? "" : ", ") << Model::noteName(z.root);
            safe->core.status("Bloom now plays " + juce::String(static_cast<int>(zones.size())) + " sounds across the keyboard, rooted at " + roots);
        });
    });
}

void MainView::loadFactory(int soundIndex, int slot)
{
    const auto& sounds = factorySounds();
    if (soundIndex < 0 || soundIndex >= static_cast<int>(sounds.size()))
        return;
    const auto sound = sounds[static_cast<std::size_t>(soundIndex)];
    juce::Component::SafePointer<MainView> safe(this);
    core.workers.addJob([safe, slot, sound] {
        auto buffer = loadFactorySound(sound);
        juce::MessageManager::callAsync([safe, slot, sound, buffer] {
            if (safe == nullptr || buffer == nullptr)
                return;
            auto& core = safe->core;
            if (slot == kAddToKeyboard)
                return addZonesToBloom(core, safe->model, { { buffer, sound.rootNote >= 0 ? static_cast<float>(sound.rootNote) : 60.0f } }, sound.name);
            if (slot == engine::kNumClouds)
            {
                core.engine.loadBloomSample(buffer);
                if (sound.rootNote >= 0)
                    safe->model.set(P::BloomRoot, static_cast<float>(sound.rootNote));
            }
            else
            {
                core.engine.loadCloudSample(slot, buffer);
                const auto level = engine::kStrips[static_cast<std::size_t>(slot + 1)].level;
                if (safe->model.value(level) <= -59.0f)
                    safe->model.set(level, -6.0f);
            }
            core.status("Loaded " + juce::String(sound.name) + (slot == engine::kNumClouds ? " into Bloom" : " into Cloud " + juce::String(slot + 1)));
        });
    });
}

void MainView::showLoadMenu(const juce::String& title, bool canAddToKeyboard, std::function<void(int slot)> chosen)
{
    juce::PopupMenu m;
    m.addSectionHeader(title);
    for (int c = 0; c < engine::kNumClouds; ++c)
    {
        const auto current = model.engine.getCloudSample(c);
        m.addItem(c + 1, "Cloud " + juce::String(c + 1) + (current != nullptr ? "   (" + juce::String(current->name) + ")" : juce::String()));
    }
    const auto bloom = model.engine.getBloomSample();
    m.addItem(engine::kNumClouds + 1, "Bloom" + (bloom != nullptr ? "   (" + juce::String(bloom->name) + ")" : juce::String()));
    if (canAddToKeyboard && bloom != nullptr)
        m.addItem(kAddToKeyboard + 1, "Add to Bloom's keyboard");
    showMenu(m, this, [chosen = std::move(chosen)](int r) { chosen(r - 1); });
}

juce::var MainView::dragFactorySound(int soundIndex) { return kDragSound + juce::String(soundIndex); }

juce::var MainView::dragFile(const juce::File& file) { return kDragFile + file.getFullPathName(); }

MainView::DragContent MainView::classify(const juce::StringArray& files)
{
    bool audio = false, folder = false;
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (io::isSessionFile(f))
            return DragContent::Session;
        audio = audio || io::isAudioFile(f);
        folder = folder || f.isDirectory();
    }
    return audio ? DragContent::Audio : folder ? DragContent::Folder : DragContent::Nothing;
}

MainView::DropTarget MainView::dropTargetAt(juce::Point<int> p, DragContent content) const
{
    if (content == DragContent::Session)
        return { DropTarget::Open, -1, getLocalBounds() };
    if (content == DragContent::Folder)
        return { DropTarget::Place, -1, browser->getBounds() };
    if (content != DragContent::Audio)
        return {};
    if (terrain->getBounds().contains(p))
        return { DropTarget::Ask, -1, terrain->getBounds() };
    if (! devices->getBounds().contains(p))
        return {};
    const auto local = p - devices->getPosition();
    const auto clouds = devices->tabBounds(DeviceView::Clouds);
    const auto bloom = devices->tabBounds(DeviceView::Bloom);
    if (clouds.contains(local))
        return { DropTarget::Ask, -1, clouds + devices->getPosition() };
    if (bloom.contains(local))
        return { DropTarget::Slot, engine::kNumClouds, bloom + devices->getPosition() };
    for (auto* c = devices->getComponentAt(local); c != nullptr && c != devices.get(); c = c->getParentComponent())
        if (auto* device = dynamic_cast<Device*>(c))
            for (auto* child : device->getChildren())
                if (auto* wave = dynamic_cast<Waveform*>(child))
                    return { DropTarget::Slot, wave->getSlot(), getLocalArea(device, device->getLocalBounds()) };
    const int page = devices->getPage();
    if (page == DeviceView::Bloom)
        return { DropTarget::Slot, engine::kNumClouds, devices->getBounds().withTrimmedTop(clouds.getBottom()) };
    if (page == DeviceView::Clouds)
        return { DropTarget::Ask, -1, devices->getBounds().withTrimmedTop(clouds.getBottom()) };
    return {};
}

void MainView::showDropTarget(const DropTarget& target)
{
    if (target.area == dropHighlight && target.kind == dropKind && target.slot == dropSlot)
        return;
    repaint(dropHighlight.expanded(4));
    dropHighlight = target.kind == DropTarget::None ? juce::Rectangle<int>() : target.area;
    dropKind = target.kind;
    dropSlot = target.slot;
    if (target.kind == DropTarget::Open)
        dropLabel = "Drop to open the project";
    else if (target.kind == DropTarget::Place)
        dropLabel = "Drop to add to Places";
    else if (target.kind == DropTarget::Ask)
        dropLabel = "Drop, then choose where it plays";
    else if (target.kind == DropTarget::Slot)
        dropLabel = dropCount > 1 && target.slot == engine::kNumClouds ? "Drop to spread across Bloom's keyboard" : "Drop into " + slotName(target.slot);
    else
        dropLabel = {};
    repaint(dropHighlight.expanded(4));
}

void MainView::paintOverChildren(juce::Graphics& g)
{
    if (dropHighlight.isEmpty())
        return;
    const auto r = dropHighlight.toFloat().reduced(1.5f);
    g.setColour(colour::accent().withAlpha(0.10f));
    g.fillRoundedRectangle(r, metric::radius);
    g.setColour(colour::accent());
    g.drawRoundedRectangle(r, metric::radius, 2.0f);
    if (dropLabel.isEmpty() || r.getHeight() < 40.0f)
        return;
    const auto f = font(12.0f, 600);
    const float w = std::min(r.getWidth() - 16.0f, static_cast<float>(juce::GlyphArrangement::getStringWidthInt(f, dropLabel)) + 24.0f);
    const auto pill = juce::Rectangle<float>(w, 24.0f).withCentre({ r.getCentreX(), r.getY() + 22.0f });
    g.setColour(colour::accent());
    g.fillRoundedRectangle(pill, 12.0f);
    g.setColour(colour::window());
    g.setFont(f);
    g.drawText(dropLabel, pill, juce::Justification::centred, true);
}

void MainView::dropAudio(const juce::Array<juce::File>& files, const DropTarget& target)
{
    if (files.isEmpty())
        return;
    if (target.kind == DropTarget::Slot)
        return loadFiles(target.slot == engine::kNumClouds ? files : juce::Array<juce::File> { files.getFirst() }, target.slot);
    if (target.kind != DropTarget::Ask)
        return core.status("Drop sounds on the terrain, a cloud or Bloom.", true);
    const auto title = files.size() == 1 ? "Load " + files.getFirst().getFileName() + " into" : "Load " + juce::String(files.size()) + " sounds into";
    juce::Component::SafePointer<MainView> safe(this);
    showLoadMenu(title, true, [safe, files](int slot) {
        safe->loadFiles(slot >= engine::kNumClouds ? files : juce::Array<juce::File> { files.getFirst() }, slot);
    });
}

void MainView::dropFactory(int soundIndex, const DropTarget& target)
{
    const auto& sounds = factorySounds();
    if (soundIndex < 0 || soundIndex >= static_cast<int>(sounds.size()))
        return;
    if (target.kind == DropTarget::Slot)
        return loadFactory(soundIndex, target.slot);
    if (target.kind != DropTarget::Ask)
        return;
    const auto& sound = sounds[static_cast<std::size_t>(soundIndex)];
    juce::Component::SafePointer<MainView> safe(this);
    showLoadMenu("Load " + juce::String(sound.name) + " into", sound.rootNote >= 0, [safe, soundIndex](int slot) { safe->loadFactory(soundIndex, slot); });
}

bool MainView::isInterestedInFileDrag(const juce::StringArray& files) { return classify(files) != DragContent::Nothing; }

void MainView::fileDragEnter(const juce::StringArray& files, int x, int y) { fileDragMove(files, x, y); }

void MainView::fileDragMove(const juce::StringArray& files, int x, int y)
{
    dropCount = files.size();
    showDropTarget(dropTargetAt({ x, y }, classify(files)));
}

void MainView::fileDragExit(const juce::StringArray&) { showDropTarget({}); }

void MainView::filesDropped(const juce::StringArray& files, int x, int y)
{
    const auto content = classify(files);
    const auto target = dropTargetAt({ x, y }, content);
    showDropTarget({});
    if (content == DragContent::Session)
    {
        for (const auto& path : files)
            if (io::isSessionFile(juce::File(path)))
                return core.session.openFile(juce::File(path));
    }
    juce::Array<juce::File> audio;
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (f.isDirectory())
            browser->addPlace(f);
        else if (io::isAudioFile(f))
            audio.add(f);
    }
    if (content == DragContent::Audio)
        dropAudio(audio, target);
}

bool MainView::isInterestedInDragSource(const SourceDetails& details)
{
    const auto d = details.description.toString();
    return d.startsWith(kDragSound) || d.startsWith(kDragFile);
}

void MainView::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void MainView::itemDragMove(const SourceDetails& details)
{
    dropCount = 1;
    showDropTarget(dropTargetAt(details.localPosition.toInt(), DragContent::Audio));
}

void MainView::itemDragExit(const SourceDetails&) { showDropTarget({}); }

void MainView::itemDropped(const SourceDetails& details)
{
    const auto target = dropTargetAt(details.localPosition.toInt(), DragContent::Audio);
    showDropTarget({});
    const auto d = details.description.toString();
    if (d.startsWith(kDragSound))
        dropFactory(d.fromFirstOccurrenceOf(kDragSound, false, false).getIntValue(), target);
    else if (d.startsWith(kDragFile))
        dropAudio({ juce::File(d.fromFirstOccurrenceOf(kDragFile, false, false)) }, target);
}

bool MainView::shouldDropFilesWhenDraggedExternally(const SourceDetails& details, juce::StringArray& files, bool& canMoveFiles)
{
    const auto d = details.description.toString();
    if (! d.startsWith(kDragFile))
        return false;
    files.add(d.fromFirstOccurrenceOf(kDragFile, false, false));
    canMoveFiles = false;
    return true;
}

bool MainView::performKey(const juce::KeyPress& key)
{
    const bool notes = noteMode;
    const bool togglesNotes = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode())) == 'M';
    if (! togglesNotes)
        noteMode = false;
    const bool handled = keyPressed(key);
    if (! togglesNotes)
        noteMode = notes;
    return handled;
}

void MainView::showAudioSettings() { openSettings(SettingsTab::Audio); }

void MainView::openSettings(SettingsTab tab) { gui::openSettings(*this, model, tab); }
}
