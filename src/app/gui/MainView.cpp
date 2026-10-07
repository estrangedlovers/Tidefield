#include "MainView.h"

#include "../FactoryContent.h"

#include <io/AudioFileIO.h>

#include <juce_audio_utils/juce_audio_utils.h>

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
int openViews = 0; // message thread only

/** One button for gestures: record, stop, play, stop. Shift (or recordNew) always
    records a new take. */
void gestureToggle(AppCore& core, bool recordNew)
{
    const auto state = core.latest().gestureState;
    if (state != engine::GestureState::Idle && ! recordNew)
        core.gestures.stop();
    else if (recordNew || ! core.gestures.hasTake())
    {
        core.gestures.record();
        core.status("Recording a gesture: play, move, turn. Press G again to stop.");
    }
    else
        core.gestures.play();
}

void showGestureMenu(AppCore& core)
{
    juce::PopupMenu m;
    const auto state = core.latest().gestureState;
    m.addSectionHeader("Gesture");
    m.addItem(1, "Record a new take");
    m.addItem(2, "Play", core.gestures.hasTake() && state == engine::GestureState::Idle);
    m.addItem(3, "Stop", state != engine::GestureState::Idle);
    m.addItem(4, "Loop", core.gestures.hasTake(), core.gestures.isLooping());
    m.addSeparator();
    m.addItem(5, "Clear the take", core.gestures.hasTake());
    m.showMenuAsync(juce::PopupMenu::Options(), [&core, state](int r) {
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

/** A discrete parameter shown as its current choice; click for the list. */
class MenuBox final : public ParamComponent
{
public:
    using ParamComponent::ParamComponent;
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(isMouseOver() ? colour::panelHi.brighter(0.07f) : colour::panelHi);
        g.fillRoundedRectangle(r, metric::radius);
        g.setColour(valueColour() == colour::accent ? colour::text : valueColour());
        g.setFont(font(12.0f, 600));
        g.drawText(valueText(), r.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);
        juce::Path arrow;
        const float cx = r.getRight() - 11.0f, cy = r.getCentreY();
        arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(colour::textDim);
        g.fillPath(arrow);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return model.showParamMenu(param);
        juce::PopupMenu m;
        const auto items = model.choices(param);
        const int current = juce::roundToInt(model.value(param));
        for (int i = 0; i < items.size(); ++i)
            m.addItem(i + 1, items[i], true, i == current);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
            if (r > 0)
                model.set(param, static_cast<float>(r - 1));
        });
    }
};

/** Record button: click records; right-click for stems and the folder. */
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
        g.setColour(shownOn ? colour::warn : (isMouseOver() ? colour::panelHi.brighter(0.07f) : colour::panelHi));
        g.fillRoundedRectangle(r, metric::radius);
        g.setColour(shownOn ? colour::text : colour::warn);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ r.getX() + 14.0f, r.getCentreY() }));
        g.setColour(colour::text);
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
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
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

/** Tempo, like a studio's transport: Sync on/off, the tempo (drag to change, or
    the DAW's when hosted) with a dot on every beat, and Tap. */
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
        g.setColour(shownOn ? colour::tide : colour::panelHi);
        g.fillRoundedRectangle(syncArea, metric::radius);
        g.setColour(shownOn ? colour::well : colour::text);
        g.setFont(font(12.0f, 600));
        g.drawText("Sync", syncArea.withTrimmedRight(10.0f), juce::Justification::centred);
        g.setColour((shownOn ? colour::well : colour::textFaint).withAlpha(0.35f + 0.65f * shownBeat));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ syncArea.getRight() - 9.0f, syncArea.getCentreY() }));

        auto field = r.reduced(3.0f, 0.0f);
        drawWell(g, field);
        g.setColour(shownHost ? colour::tide : colour::text);
        g.setFont(font(13.0f, 600));
        g.drawText(juce::String(shownBpm, 1), field, juce::Justification::centred);

        g.setColour(isMouseOver() && tapArea.contains(getMouseXYRelative().toFloat()) ? colour::panelHi.brighter(0.08f) : colour::panelHi);
        g.fillRoundedRectangle(tapArea, metric::radius);
        g.setColour(colour::text);
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
            return model.showParamMenu(e.x < 54 ? P::SyncOn : P::SyncBpm);
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
            taps.clear(); // a pause starts a new count
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

} // namespace

// --- Top bar --------------------------------------------------------------------------

class TopBar final : public juce::Component, public Animated
{
public:
    TopBar(Model& m, MainView& v) : model(m), view(v), meter(m, -1, true), rec(m), autoMaster(m, P::MasterAuto, "Auto master", {}, colour::good), tempo(m)
    {
        model.add(this);
        sessionButton.setHelp(&model, "new, open, save (Cmd+N, Cmd+O, Cmd+S)");
        sessionButton.onClick = [this] {
            juce::PopupMenu menu;
            menu.addItem(1, "New session");
            menu.addItem(2, "Open...");
            menu.addSeparator();
            menu.addItem(3, "Save");
            menu.addItem(4, "Save as...");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sessionButton), [this](int r) {
                auto& s = model.core.session;
                if (r == 1) s.newSession();
                else if (r == 2) s.open();
                else if (r == 3) s.save();
                else if (r == 4) s.saveAs();
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
        audio.setHelp(&model, "audio device, sample rate and buffer size");
        audio.onClick = [this] { view.showAudioSettings(); };
        for (auto* c : std::initializer_list<juce::Component*> { &sessionButton, &fade, &panic, &keys, &audio, &meter, &rec, &autoMaster, &tempo })
            addAndMakeVisible(c);
        audio.setVisible(model.core.host.getDeviceManager() != nullptr); // in a DAW, the DAW owns the device
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
        r.removeFromLeft(112); // wordmark
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

        audio.setBounds(r.removeFromRight(58));
        r.removeFromRight(8);
        meter.setBounds(r.removeFromRight(150).reduced(0, 4));
        r.removeFromRight(10);
        cpuArea = r.removeFromRight(std::min(190, r.getWidth())); // shrinks first when the window is narrow
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colour::header);
        g.setColour(colour::line);
        g.fillRect(getLocalBounds().removeFromBottom(1));

        // Wordmark: a small tide glyph and the name.
        auto mark = getLocalBounds().reduced(12, 0).removeFromLeft(110).toFloat();
        juce::Path wave;
        const float cy = mark.getCentreY();
        wave.startNewSubPath(mark.getX(), cy);
        for (int i = 1; i <= 16; ++i)
            wave.lineTo(mark.getX() + static_cast<float>(i), cy + 3.5f * std::sin(static_cast<float>(i) * 0.785f));
        g.setColour(colour::tide);
        g.strokePath(wave, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour::text);
        g.setFont(font(15.0f, 600).withExtraKerningFactor(0.02f));
        g.drawText("Tidefield", mark.withTrimmedLeft(24.0f), juce::Justification::centredLeft);

        // Device and load.
        const auto& f = model.frame();
        const float load = f.dspLoad > 0.0f ? f.dspLoad : static_cast<float>(model.core.host.getCpuLoad());
        g.setFont(font(11.5f, 500));
        auto a = cpuArea.toFloat();
        const auto output = model.core.host.describeOutput();
        if (output.isEmpty())
        {
            g.setColour(colour::warn);
            g.drawText("No audio output", a, juce::Justification::centredRight);
            return;
        }
        const int pct = juce::roundToInt(load * 100.0f);
        g.setColour(pct > 70 ? colour::warn : colour::textDim);
        const auto cpu = juce::String(pct) + "% CPU" + (f.guardLevel > 0 ? "  lite " + juce::String(f.guardLevel) : juce::String());
        g.drawText(cpu, a.removeFromRight(90.0f), juce::Justification::centredRight);
        g.setColour(colour::textFaint);
        g.drawText(output, a, juce::Justification::centredRight, true);
    }

private:
    Model& model;
    MainView& view;
    FlatButton sessionButton { "Untitled" }, fade { "Fade in", colour::good }, panic { "Panic", colour::warn }, keys { "Keys", colour::tide },
        audio { "Audio" };
    Meter meter;
    RecordButton rec;
    Toggle autoMaster;
    TempoWidget tempo;
    juce::Rectangle<int> cpuArea;
    int slow = 0;
};

// --- Browser --------------------------------------------------------------------------

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
    }
    ~Browser() override { model.remove(this); }

    void tick() override
    {
        const auto& scenes = model.core.scenes.getScenes();
        juce::String sig;
        for (const auto& s : scenes)
            sig << s.name << ";";
        const auto& f = model.frame();
        bool weightsMoved = false;
        for (std::size_t k = 0; k < scenes.size() && k < shownWeights.size(); ++k)
            weightsMoved = weightsMoved || std::abs(shownWeights[k] - f.sceneWeights[k]) > 0.01f;
        if (sig != signature)
        {
            signature = sig;
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
        viewport.setBounds(getLocalBounds().withTrimmedTop(metric::header).reduced(2, 4));
        layout();
    }

    void paint(juce::Graphics& g) override { drawPanel(g, getLocalBounds().toFloat(), "Browser"); }

private:
    struct Row
    {
        enum Kind { Header, Scene, Capture, Sound, Disk } kind;
        int index = -1;
        juce::String text, detail;
    };

    struct List final : public juce::Component
    {
        Browser* owner = nullptr;
        int hover = -1;
        void paint(juce::Graphics& g) override { owner->paintRows(g); }
        void mouseMove(const juce::MouseEvent& e) override { setHover(e.y / kRowH); }
        void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
        void mouseDown(const juce::MouseEvent& e) override { owner->clicked(e.y / kRowH, e); }
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

    void layout()
    {
        rows.clear();
        rows.push_back({ Row::Header, -1, "SCENES", "double-click the terrain to add" });
        const auto& scenes = model.core.scenes.getScenes();
        for (std::size_t k = 0; k < scenes.size(); ++k)
            rows.push_back({ Row::Scene, static_cast<int>(k), juce::String(scenes[k].name), k < 9 ? juce::String(static_cast<int>(k) + 1) : juce::String() });
        rows.push_back({ Row::Capture, -1, "+ Capture what you hear", "C" });
        const char* category = "";
        const auto& sounds = factorySounds();
        for (std::size_t k = 0; k < sounds.size(); ++k)
        {
            if (juce::String(sounds[k].category) != category)
            {
                category = sounds[k].category;
                rows.push_back({ Row::Header, -1, juce::String(category).toUpperCase() + " SOUNDS", {} });
            }
            rows.push_back({ Row::Sound, static_cast<int>(k), sounds[k].name,
                             sounds[k].rootNote >= 0 ? Model::noteName(static_cast<float>(sounds[k].rootNote)) : juce::String() });
        }
        rows.push_back({ Row::Header, -1, "YOUR SOUNDS", {} });
        rows.push_back({ Row::Disk, -1, "Load from disk...", {} });
        list.setSize(std::max(10, viewport.getWidth() - (viewport.getVerticalScrollBar().isVisible() ? 8 : 0)), static_cast<int>(rows.size()) * kRowH + 6);
        list.repaint();
    }

    void paintRows(juce::Graphics& g)
    {
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto& row = rows[i];
            auto r = juce::Rectangle<float>(0.0f, static_cast<float>(i) * kRowH, static_cast<float>(list.getWidth()), static_cast<float>(kRowH));
            const bool hover = static_cast<int>(i) == list.hover && row.kind != Row::Header;
            if (hover)
            {
                g.setColour(colour::panelHi);
                g.fillRoundedRectangle(r.reduced(2.0f, 1.0f), metric::radius);
            }
            r = r.reduced(8.0f, 0.0f);
            switch (row.kind)
            {
                case Row::Header:
                    g.setFont(caps(10.0f));
                    g.setColour(colour::textFaint);
                    g.drawText(row.text, r.withTrimmedTop(6.0f), juce::Justification::centredLeft, true);
                    break;
                case Row::Scene:
                {
                    const auto c = colour::forScene(row.index);
                    g.setColour(c);
                    g.fillRoundedRectangle(r.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 10.0f), 2.0f);
                    r.removeFromLeft(8.0f);
                    const float w = static_cast<std::size_t>(row.index) < shownWeights.size() ? shownWeights[static_cast<std::size_t>(row.index)] : 0.0f;
                    auto bar = r.removeFromRight(34.0f).withSizeKeepingCentre(34.0f, 4.0f);
                    g.setColour(colour::well);
                    g.fillRoundedRectangle(bar, 2.0f);
                    g.setColour(c);
                    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, w)), 2.0f);
                    g.setColour(colour::textFaint);
                    g.setFont(font(10.5f, 500));
                    g.drawText(row.detail, r.removeFromRight(16.0f), juce::Justification::centred);
                    g.setColour(w > 0.3f ? colour::text : colour::textDim);
                    g.setFont(font(12.5f, w > 0.3f ? 600 : 500));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    break;
                }
                case Row::Capture:
                case Row::Disk:
                    g.setColour(hover ? colour::accent : colour::textDim);
                    g.setFont(font(12.0f, 500));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    g.setColour(colour::textFaint);
                    g.drawText(row.detail, r, juce::Justification::centredRight);
                    break;
                case Row::Sound:
                {
                    juce::Path note;
                    const auto icon = r.removeFromLeft(10.0f).withSizeKeepingCentre(8.0f, 8.0f);
                    note.addEllipse(icon);
                    g.setColour(colour::tide.withAlpha(0.8f));
                    g.fillPath(note);
                    r.removeFromLeft(8.0f);
                    g.setColour(colour::textFaint);
                    g.setFont(font(10.5f, 500));
                    g.drawText(row.detail, r.removeFromRight(30.0f), juce::Justification::centredRight);
                    g.setColour(colour::text);
                    g.setFont(font(12.5f, 500));
                    g.drawText(row.text, r, juce::Justification::centredLeft, true);
                    break;
                }
            }
        }
    }

    void hovered(int i)
    {
        if (model.onHover == nullptr || i < 0 || i >= static_cast<int>(rows.size()))
            return;
        const auto& row = rows[static_cast<std::size_t>(i)];
        if (row.kind == Row::Scene)
            model.onHover("Scene \"" + row.text + "\": click to glide there (or press " + row.detail + "), double-click to rename, right-click for more");
        else if (row.kind == Row::Sound)
            model.onHover(row.text + ": click to load it into a cloud or Bloom" + (row.detail.isNotEmpty() ? " (sounds at " + row.detail + ")" : juce::String()));
        else if (row.kind == Row::Capture)
            model.onHover("Capture: store everything you hear now as a scene at the cursor (C)");
    }

    void clicked(int i, const juce::MouseEvent& e)
    {
        if (i < 0 || i >= static_cast<int>(rows.size()))
            return;
        const auto row = rows[static_cast<std::size_t>(i)];
        if (row.kind == Row::Scene)
        {
            if (e.mods.isPopupMenu())
                return showSceneMenu(model, row.index);
            const auto p = model.core.scenes.getScenes()[static_cast<std::size_t>(row.index)].position;
            model.set(P::TerrainX, p.x);
            model.set(P::TerrainY, p.y);
        }
        else if (row.kind == Row::Capture)
            model.core.captureSceneAtCursor();
        else if (row.kind == Row::Sound || row.kind == Row::Disk)
        {
            juce::PopupMenu m;
            m.addSectionHeader(row.kind == Row::Sound ? "Load " + row.text + " into" : "Load a sound from disk into");
            for (int c = 0; c < engine::kNumClouds; ++c)
            {
                const auto current = model.engine.getCloudSample(c);
                m.addItem(c + 1, "Cloud " + juce::String(c + 1) + (current != nullptr ? "   (" + juce::String(current->name) + ")" : juce::String()));
            }
            const auto bloom = model.engine.getBloomSample();
            m.addItem(engine::kNumClouds + 1, "Bloom" + (bloom != nullptr ? "   (" + juce::String(bloom->name) + ")" : juce::String()));
            m.showMenuAsync(juce::PopupMenu::Options(), [this, row](int r) {
                if (r <= 0)
                    return;
                if (row.kind == Row::Sound)
                    view.loadFactory(row.index, r - 1);
                else
                    view.chooseSample(r - 1);
            });
        }
    }

    void doubleClicked(int i)
    {
        if (i >= 0 && i < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(i)].kind == Row::Scene)
            showSceneMenu(model, rows[static_cast<std::size_t>(i)].index);
    }

    Model& model;
    MainView& view;
    juce::Viewport viewport;
    List list;
    std::vector<Row> rows;
    juce::String signature = "\x01";
    std::vector<float> shownWeights;
};

// --- Macro panel ----------------------------------------------------------------------

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
          medium(m, P::MediumType, 2, "what the whole piece sounds recorded on"),
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
        auto faders = r.removeFromTop(juce::jlimit(110, 170, r.getHeight() - 250));
        const int w = (faders.getWidth() - 3 * 6) / 4;
        for (auto* f : { &tide, &wander, &gravity, &glide })
        {
            f->setBounds(faders.removeFromLeft(w));
            faders.removeFromLeft(6);
        }
        r.removeFromTop(8);
        labels.clear();
        auto place = [&](juce::Component& c, const juce::String& label, int h) {
            labels.push_back({ r.removeFromTop(16), label });
            c.setBounds(r.removeFromTop(h));
            r.removeFromTop(6);
        };
        place(root, "Key", 48);
        place(scale, "Scale", 24);
        place(style, "Wander", 48);
        place(medium, "Recorded on", 48);
    }

    void paint(juce::Graphics& g) override
    {
        drawPanel(g, getLocalBounds().toFloat(), "Performance");
        g.setFont(font(11.0f, 500));
        g.setColour(colour::textDim);
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

// --- Pads -----------------------------------------------------------------------------

class PadRow final : public juce::Component
{
public:
    explicit PadRow(Model& m) : model(m), shape(m)
    {
        auto hold = [this](const char* title, const char* sub, juce::Colour c, const char* help, P p, std::function<float()> level) {
            auto pad = std::make_unique<Pad>(model, title, sub, c, help);
            pad->onPress = [this, p] { model.set(p, 1.0f); };
            pad->onRelease = [this, p] { model.set(p, 0.0f); };
            pad->level = std::move(level);
            pad->lit = [this, p] { return model.value(p) > 0.5f; };
            pads.push_back(std::move(pad));
        };
        auto toggle = [this](const char* title, const char* sub, juce::Colour c, const char* help, P p, std::function<float()> level) {
            auto pad = std::make_unique<Pad>(model, title, sub, c, help);
            pad->onPress = [this, p] { model.toggle(p); };
            pad->level = std::move(level);
            pad->lit = [this, p] { return model.value(p) > 0.5f; };
            pads.push_back(std::move(pad));
        };
        const auto& f = [this]() -> const engine::TelemetryFrame& { return model.frame(); };
        hold("Swell", "hold S", colour::accent, "hold to swell: sends bloom and filters open; let go and it ebbs back", P::SwellHold, [f] { return f().swell; });
        hold("Hush", "hold H", colour::forScene(4), "hold to hush: every source sinks while the reverb and delay ring on", P::HushHold, [f] { return f().hush; });
        hold("Slow", "hold T", colour::forScene(2), "hold to slow time to a quarter: every drift and cycle with it", P::SlowHold, [f] { return f().slow; });
        toggle("Freeze all", "the moment, F", colour::tide, "hold the last two seconds as a cloud while the rest steps back", P::FreezeOn,
               [f] { return f().freezeGain; });
        toggle("Hold input", "spectral, I", colour::forScene(9), "hold the live input's sound forever as a spectral pad", P::InputFreeze,
               [f] { return f().inputFreeze; });

        auto loop = std::make_unique<Pad>(model, "Loop", "L", colour::warn, "record, close the loop, overdub; every pass wears the tape a little more");
        loop->onPress = [this] {
            if (juce::ModifierKeys::currentModifiers.isShiftDown())
                model.engine.command(engine::Command::LoopClear);
            else
                model.engine.command(engine::Command::LoopRecord);
        };
        loop->level = [f] { return f().loopState >= 2 ? f().loopPosition : (f().loopState == 1 ? 1.0f : 0.0f); };
        loop->lit = [f] { return f().loopState == 1 || f().loopState == 3; };
        loop->subText = [f] {
            static const char* s[] = { "tap to record, L", "recording: tap to close", "looping: tap to dub", "overdubbing", "clearing" };
            return juce::String(s[juce::jlimit(0, 4, f().loopState)]);
        };
        pads.push_back(std::move(loop));

        toggle("Loops", "never align, E", colour::forScene(7), "notes on long cycles that never line up, always in the key", P::LoopsOn, [f] {
            float m = 0.0f;
            for (float v : f().loopFlash)
                m = std::max(m, v);
            return m * 0.8f;
        });

        auto gesture = std::make_unique<Pad>(model, "Gesture", "G", colour::learn,
                                             "record your moves (knobs, terrain, notes) and play them back, looped; right-click for more");
        gesture->onPress = [this] { gestureToggle(model.core, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        gesture->onMenu = [this] { showGestureMenu(model.core); };
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
            return model.core.gestures.hasTake() ? "tap to play, G" : juce::String("tap to record, G");
        };
        pads.push_back(std::move(gesture));

        auto catchPad = std::make_unique<Pad>(model, "Catch", "last seconds, K", colour::live, "grab what just happened into a cloud, where it keeps playing");
        catchPad->onPress = [this] { model.engine.command(engine::Command::Catch); };
        pads.push_back(std::move(catchPad));

        for (auto& p : pads)
            addAndMakeVisible(*p);
        addAndMakeVisible(shape);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = static_cast<int>(pads.size()) + 1; // + shape pad (wider)
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

// --- Status bar -----------------------------------------------------------------------

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
        g.fillAll(colour::header);
        g.setColour(colour::line);
        g.fillRect(getLocalBounds().removeFromTop(1));
        auto r = getLocalBounds().reduced(10, 0).toFloat();
        g.setFont(font(11.5f, 500));
        if (keysText.isNotEmpty())
        {
            g.setColour(colour::tide);
            g.drawText(keysText, r.removeFromRight(190.0f), juce::Justification::centredRight);
        }
        if (message.isNotEmpty() && alpha > 0.0f)
        {
            g.setColour((warn ? colour::warn : colour::accent).withAlpha(alpha));
            const float w = std::min(r.getWidth() * 0.5f, juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), message) + 20.0f);
            g.drawText(message, r.removeFromRight(w), juce::Justification::centredRight, true);
        }
        g.setColour(colour::textDim);
        g.drawText(help.isNotEmpty() ? help
                                     : juce::String("Space fade   Esc panic   drag the terrain to move   1-9 scenes   C capture   S/H/T hold gestures   G record a gesture   P draw a path   M keys   Tab pages"),
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

// --- MainView -------------------------------------------------------------------------

MainView::MainView(AppCore& c) : core(c), model(c)
{
    ++openViews;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel.get());
    setOpaque(true);
    setWantsKeyboardFocus(true);

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

    model.onHover = [this](const juce::String& h) { status->setHelp(h); };
    core.onStatus = [this](const juce::String& m, bool warning) { status->showMessage(m, warning); };
    core.onSessionChanged = [this] {
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName("Tidefield - " + core.session.getName());
    };

    vblank = std::make_unique<juce::VBlankAttachment>(this, [this] { frame(); });
    startTimerHz(4);
    setSize(1440, 900);
}

MainView::~MainView()
{
    stopTimer();
    vblank.reset();
    releaseHolds();
    core.onStatus = nullptr;
    core.onSessionChanged = nullptr;
    model.onHover = nullptr;
    // Children unregister from the model as they go; destroy them before it.
    status.reset();
    devices.reset();
    pads.reset();
    macros.reset();
    terrain.reset();
    browser.reset();
    topBar.reset();
    if (--openViews == 0) // the last window (several plugin instances may be open)
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainView::frame()
{
    model.tick();
}

void MainView::timerCallback()
{
    // Keep keys coming here (no beeps), and never leave a gesture held when the app
    // is in the background: a key released elsewhere never reaches us.
    if (! juce::Process::isForegroundProcess())
    {
        releaseHolds();
        return;
    }
    if (juce::Component::getCurrentlyFocusedComponent() == nullptr && isShowing() && ! juce::ModalComponentManager::getInstance()->getNumModalComponents())
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

void MainView::paint(juce::Graphics& g) { g.fillAll(colour::window); }

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

// --- Keys -----------------------------------------------------------------------------

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
        if (keyNote[static_cast<std::size_t>(code)] == 0) // ignore auto-repeat
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
        const float glide = model.value(P::TerrainGlide);
        model.set(P::TerrainGlide, 0.05f);
        model.set(P::TerrainX, p.x);
        model.set(P::TerrainY, p.y);
        juce::Timer::callAfterDelay(150, [safe = juce::Component::SafePointer<MainView>(this), glide] {
            if (safe != nullptr)
                safe->model.set(P::TerrainGlide, glide);
        });
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
        if (code == 'S')
            mods.isShiftDown() ? core.session.saveAs() : core.session.save();
        else if (code == 'O')
            core.session.open();
        else if (code == 'N')
            core.session.newSession();
        else if (code == ',')
            showAudioSettings();
        else
            return false; // Cmd+Q and the system's own shortcuts
        return true;
    }

    // In a DAW, Space is the DAW's transport.
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
            if (! h.down) // ignore auto-repeat
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
    // Standalone, every key is ours so macOS never beeps; in a DAW, unused keys go
    // on to the host.
    return ! core.host.isPlugin() || std::string_view("FIELKCRPSHTMG").find(static_cast<char>(code)) != std::string_view::npos;
}

// --- Loading sounds -------------------------------------------------------------------

void MainView::chooseSample(int slot)
{
    const bool isBloom = slot == engine::kNumClouds;
    chooser = std::make_unique<juce::FileChooser>(isBloom ? juce::String("Load a one-shot into Bloom") : "Load a sample into Cloud " + juce::String(slot + 1),
                                                  juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<MainView> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, slot](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (safe == nullptr || file == juce::File())
            return;
        std::thread([safe, slot, file] {
            juce::String error;
            std::shared_ptr<dsp::SampleBuffer> buffer(io::loadSample(file, error).release());
            juce::MessageManager::callAsync([safe, slot, buffer, error] {
                if (safe == nullptr)
                    return;
                if (buffer == nullptr)
                    return safe->core.status(error, true);
                if (slot == engine::kNumClouds)
                    safe->core.engine.loadBloomSample(buffer);
                else
                    safe->core.engine.loadCloudSample(slot, buffer);
                safe->core.status("Loaded " + juce::String(buffer->name));
            });
        }).detach();
    });
}

void MainView::loadFactory(int soundIndex, int slot)
{
    const auto& sounds = factorySounds();
    if (soundIndex < 0 || soundIndex >= static_cast<int>(sounds.size()))
        return;
    const auto sound = sounds[static_cast<std::size_t>(soundIndex)];
    juce::Component::SafePointer<MainView> safe(this);
    std::thread([safe, slot, sound] {
        auto buffer = loadFactorySound(sound);
        juce::MessageManager::callAsync([safe, slot, sound, buffer] {
            if (safe == nullptr || buffer == nullptr)
                return;
            auto& core = safe->core;
            if (slot == engine::kNumClouds)
            {
                core.engine.loadBloomSample(buffer);
                if (sound.rootNote >= 0)
                    safe->model.set(P::BloomRoot, static_cast<float>(sound.rootNote));
            }
            else
            {
                core.engine.loadCloudSample(slot, buffer);
                // A cloud that was muted would make loading look broken.
                const auto level = engine::kStrips[static_cast<std::size_t>(slot + 1)].level;
                if (safe->model.value(level) <= -59.0f)
                    safe->model.set(level, -6.0f);
            }
            core.status("Loaded " + juce::String(sound.name) + (slot == engine::kNumClouds ? " into Bloom" : " into Cloud " + juce::String(slot + 1)));
        });
    }).detach();
}

void MainView::showAudioSettings()
{
    auto* devices = core.host.getDeviceManager();
    if (devices == nullptr)
        return;
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(*devices, 0, 2, 2, 2, false, false, true, false);
    selector->setSize(540, 440);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = colour::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace tf::app::gui
