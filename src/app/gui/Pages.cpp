#include "Pages.h"

#include <engine/mix/Layout.h>

#include <map>

namespace tf::app::gui {

using engine::P;

namespace {

constexpr int kGap = 4;
constexpr int kTabH = 26;

/** One-line explanations shown in the status bar while hovering. */
juce::String helpFor(P p)
{
    static const std::map<P, const char*> help {
        { P::DroneRoot, "the drone's lowest note; the voices stack on it in the key" },
        { P::DroneCutoff, "opens the drone's filter" },
        { P::DroneDensity, "how many voices sound at once" },
        { P::DroneEvolve, "how often voices move to new notes of the chord" },
        { P::DroneGravity, "how strongly voices are pulled into the key" },
        { P::DroneNoise, "air and breath under the tone" },
        { P::ResStructure, "from strings (left) to bars and bells (right)" },
        { P::ResRain, "random strikes, like drops on a resonant surface" },
        { P::ResDecay, "how long each mode rings" },
        { P::BloomTransform, "what a held note turns into while it sustains" },
        { P::BloomAmount, "how strongly the transform acts" },
        { P::InputArmed, "hear the live input through its strip" },
        { P::InputFreeze, "hold the input's sound forever as a spectral pad" },
        { P::LoopErosion, "how much each pass wears the tape" },
        { P::LoopFlakes, "dropouts, like oxide falling off old tape" },
        { P::WeatherGust, "how much the wind and rain swell and lull" },
        { P::WeatherDistance, "from right here to far across a valley" },
        { P::FreezeDuck, "how far the rest of the mix steps back while frozen" },
        { P::CatchSeconds, "how far back Catch reaches" },
        { P::TerrainGlide, "how long the sound takes to travel to where you point" },
        { P::TerrainFocus, "high: scenes are distinct places; low: everything blends" },
        { P::TerrainWanderRate, "how fast the sound wanders on its own" },
        { P::HarmonyMorph, "how long a key change takes to migrate" },
        { P::MasterAuto, "listens to the mix and masters it: loudness, tonal balance, glue, width" },
        { P::MasterAutoAmount, "how far the tonal correction goes" },
        { P::MasterCeiling, "true-peak ceiling of the safety limiter" },
        { P::MasterFadeSecs, "length of the Space-bar fade" },
        { P::LoopsRate, "pace of every loop; they never line up" },
        { P::LoopsPattern, "which set of cycle lengths and notes" },
        { P::SeasonsDepth, "scales every season at once" },
    };
    const auto it = help.find(p);
    return it != help.end() ? juce::String(it->second) : juce::String();
}

/** Display formatting for an FX slot control, from the loaded processor. */
std::function<juce::String(float)> fxFormatter(const dsp::ProcessorControl& c)
{
    return [c](float v) {
        char buf[48] {};
        c.display.format(v, buf, static_cast<int>(sizeof(buf)));
        return juce::String(buf);
    };
}

// --- Specialised devices --------------------------------------------------------------

/** An FX slot: a type menu over six controls and mix, named by the loaded effect. */
class FxDevice final : public Device, public Animated
{
public:
    FxDevice(Model& m, int s) : Device(m, engine::kFxSlots[static_cast<std::size_t>(s)].name, colour::tide), slot(s)
    {
        model.add(this);
        menu = setTop(std::make_unique<juce::ComboBox>(), 24, 4 * metric::knobW);
        menu->addItem("Empty", 1);
        const auto& entries = dsp::ProcessorFactory::instance().entries();
        for (std::size_t k = 0; k < entries.size(); ++k)
            menu->addItem(entries[k].info->name, static_cast<int>(k) + 2);
        menu->onChange = [this] {
            const int id = menu->getSelectedId();
            const auto& e = dsp::ProcessorFactory::instance().entries();
            model.core.fx.setType(slot, id <= 1 ? std::string() : std::string(e[static_cast<std::size_t>(id - 2)].info->typeId));
        };
        const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(s)].firstParam);
        for (int k = 0; k < 7; ++k)
            knobs[static_cast<std::size_t>(k)] = static_cast<Knob*>(addKnob(static_cast<P>(first + static_cast<engine::ParamIndex>(k))));
        refresh();
    }
    ~FxDevice() override { model.remove(this); }

    void tick() override
    {
        if (model.core.fx.getType(slot) != shownType)
            refresh();
    }

private:
    void refresh()
    {
        shownType = model.core.fx.getType(slot);
        const auto* info = model.core.fx.getInfo(slot);
        int selected = 1;
        const auto& entries = dsp::ProcessorFactory::instance().entries();
        for (std::size_t k = 0; k < entries.size(); ++k)
            if (shownType == entries[k].info->typeId)
                selected = static_cast<int>(k) + 2;
        menu->setSelectedId(selected, juce::dontSendNotification);
        for (int k = 0; k < 6; ++k)
        {
            auto* kn = knobs[static_cast<std::size_t>(k)];
            const bool used = info != nullptr && info->controls[static_cast<std::size_t>(k)].display.curve != dsp::DisplayMap::Curve::Hidden
                              && info->controls[static_cast<std::size_t>(k)].name[0] != '\0';
            kn->setVisible(used);
            if (used)
            {
                kn->setLabel(info->controls[static_cast<std::size_t>(k)].name);
                kn->formatter = fxFormatter(info->controls[static_cast<std::size_t>(k)]);
            }
        }
        knobs[6]->setVisible(info != nullptr);
        title = info != nullptr ? juce::String(info->name) : juce::String(engine::kFxSlots[static_cast<std::size_t>(slot)].name);
        repaint();
    }

    int slot;
    juce::ComboBox* menu = nullptr;
    std::array<Knob*, 7> knobs {};
    std::string shownType = "\x01";
};

/** A level fader with the strip's meter beside it. */
class FaderMeter final : public juce::Component
{
public:
    FaderMeter(Model& m, P level, int strip, const juce::String& label) : fader(m, level), meter(m, strip, false)
    {
        fader.setLabel(label);
        addAndMakeVisible(fader);
        addAndMakeVisible(meter);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        meter.setBounds(r.removeFromRight(10).withTrimmedTop(22).withTrimmedBottom(18));
        r.removeFromRight(3);
        fader.setBounds(r);
    }

private:
    Fader fader;
    Meter meter;
};

/** What the auto master is doing: loudness against target, the EQ it applies, glue,
    width and make-up. */
class AutoMasterView final : public juce::Component, public Animated
{
public:
    explicit AutoMasterView(Model& m) : model(m) { model.add(this); }
    ~AutoMasterView() override { model.remove(this); }
    void tick() override
    {
        if (isShowing())
            repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawWell(g, r);
        r = r.reduced(8.0f, 6.0f);
        const auto& a = model.frame().autoMaster;
        const bool on = model.value(P::MasterAuto) > 0.5f;
        static const float targets[] = { -23.0f, -16.0f, -14.0f };
        const float target = targets[juce::jlimit(0, 2, juce::roundToInt(model.value(P::MasterAutoTarget)))];

        g.setFont(font(22.0f, 600));
        g.setColour(on ? colour::text : colour::textFaint);
        const float lufs = a[0];
        g.drawText(lufs > -69.0f ? juce::String(lufs, 1) : juce::String("--"), r.removeFromTop(28.0f), juce::Justification::centredLeft);
        g.setFont(font(10.5f, 500));
        g.setColour(colour::textDim);
        g.drawText("LUFS short-term, target " + juce::String(target, 0) + (on ? "" : "   (off)"), r.removeFromTop(16.0f), juce::Justification::centredLeft);
        r.removeFromTop(6.0f);

        auto bar = [&](const juce::String& name, float v, float range, const juce::String& text) {
            auto row = r.removeFromTop(17.0f);
            g.setColour(colour::textDim);
            g.drawText(name, row.removeFromLeft(54.0f), juce::Justification::centredLeft);
            auto val = row.removeFromRight(56.0f);
            auto track = row.reduced(0.0f, 5.0f);
            g.setColour(colour::panelHi);
            g.fillRoundedRectangle(track, 2.0f);
            const float c = track.getCentreX();
            const float x = c + juce::jlimit(-1.0f, 1.0f, v / range) * track.getWidth() * 0.5f;
            g.setColour(on ? colour::accent : colour::textFaint);
            g.fillRoundedRectangle(juce::Rectangle<float>(std::min(c, x), track.getY(), std::abs(x - c) + 1.0f, track.getHeight()), 2.0f);
            g.setColour(colour::text);
            g.drawText(text, val, juce::Justification::centredRight);
        };
        auto db = [](float v) { return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " dB"; };
        bar("Low", a[2], 6.0f, db(a[2]));
        bar("Mud", a[3], 6.0f, db(a[3]));
        bar("High", a[4], 6.0f, db(a[4]));
        bar("Glue", -a[6], 6.0f, db(-a[6]));
        bar("Width", a[5] - 1.0f, 0.5f, juce::String(juce::roundToInt(a[5] * 100.0f)) + "%");
        bar("Make-up", a[1], 12.0f, db(a[1]));
    }

private:
    Model& model;
};

/** Looper state, the loop's ring and its two buttons. */
class LooperView final : public juce::Component, public Animated
{
public:
    explicit LooperView(Model& m) : model(m), rec("Record"), clear("Clear")
    {
        model.add(this);
        rec.setHelp(&model, "record, close the loop, overdub (L)");
        clear.setHelp(&model, "fade the loop out and clear it (Shift+L)");
        rec.onClick = [this] { model.engine.command(engine::Command::LoopRecord); };
        clear.onClick = [this] { model.engine.command(engine::Command::LoopClear); };
        addAndMakeVisible(rec);
        addAndMakeVisible(clear);
    }
    ~LooperView() override { model.remove(this); }
    void tick() override
    {
        if (isShowing())
            repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().removeFromBottom(26);
        rec.setBounds(r.removeFromLeft(r.getWidth() / 2 - 2));
        r.removeFromLeft(4);
        clear.setBounds(r);
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedBottom(32.0f);
        drawWell(g, r);
        const auto& f = model.frame();
        static const char* states[] = { "Empty", "Recording", "Looping", "Overdubbing", "Clearing" };
        const int st = juce::jlimit(0, 4, f.loopState);
        const float size = std::min({ r.getHeight(), r.getWidth(), 130.0f }) - 22.0f;
        const auto ring = juce::Rectangle<float>(size, size).withCentre({ r.getX() + size * 0.5f + 12.0f, r.getCentreY() });
        juce::Path track, fill;
        track.addCentredArc(ring.getCentreX(), ring.getCentreY(), size * 0.5f, size * 0.5f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour(colour::panelHi);
        g.strokePath(track, juce::PathStrokeType(5.0f));
        if (st >= 2)
        {
            fill.addCentredArc(ring.getCentreX(), ring.getCentreY(), size * 0.5f, size * 0.5f, 0.0f, 0.0f,
                               juce::MathConstants<float>::twoPi * f.loopPosition, true);
            g.setColour(st == 3 ? colour::warn : colour::accent);
            g.strokePath(fill, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (st == 1)
        {
            g.setColour(colour::warn);
            g.fillEllipse(ring.withSizeKeepingCentre(14.0f, 14.0f));
        }
        auto text = r.withLeft(ring.getRight() + 14.0f).reduced(0.0f, 8.0f);
        g.setFont(font(16.0f, 600));
        g.setColour(colour::text);
        g.drawText(states[st], text.removeFromTop(24.0f), juce::Justification::centredLeft);
        g.setFont(font(11.5f, 500));
        g.setColour(colour::textDim);
        if (st > 0)
            g.drawText(juce::String(f.loopSeconds, 1) + " s" + (st >= 2 ? ", pass " + juce::String(f.loopPasses) : juce::String()),
                       text.removeFromTop(18.0f), juce::Justification::centredLeft);
        else
            g.drawText("press Record or L", text.removeFromTop(18.0f), juce::Justification::centredLeft);
        rec.setToggleState(st == 1 || st == 3, juce::dontSendNotification);
        rec.setButtonText(st == 0 ? "Record" : st == 1 ? "Close loop" : st == 2 ? "Overdub" : "Stop dub");
    }

private:
    Model& model;
    FlatButton rec, clear;
};

/** The seasons: slow cycles on any continuous parameter, minutes to an hour long. */
class SeasonList final : public juce::Component, public Animated
{
public:
    explicit SeasonList(Model& m) : model(m), addButton("Add a season")
    {
        model.add(this);
        addButton.setHelp(&model, "a slow cycle on a parameter of your choice, 20 seconds to an hour long");
        addButton.onClick = [this] { showParamMenu(-1); };
        addAndMakeVisible(addButton);
    }
    ~SeasonList() override { model.remove(this); }

    void tick() override
    {
        const auto& list = model.core.seasons.getSeasons();
        if (list.size() != shownCount)
        {
            shownCount = list.size();
            resized();
        }
        if (isShowing())
            repaint();
    }

    void resized() override { addButton.setBounds(getLocalBounds().removeFromBottom(24).removeFromLeft(140)); }

    void paint(juce::Graphics& g) override
    {
        const auto& list = model.core.seasons.getSeasons();
        auto r = getLocalBounds().toFloat().withTrimmedBottom(28.0f);
        drawWell(g, r);
        r = r.reduced(6.0f, 4.0f);
        if (list.empty())
        {
            g.setFont(font(11.5f));
            g.setColour(colour::textFaint);
            g.drawText("No seasons yet. A season moves one parameter slowly back and forth for as long as you play.", r,
                       juce::Justification::centred, true);
            return;
        }
        static const char* shapes[] = { "Sine", "Triangle", "Drift" };
        g.setFont(font(11.5f, 500));
        for (std::size_t k = 0; k < list.size(); ++k)
        {
            auto row = r.removeFromTop(19.0f);
            const auto& s = list[k];
            g.setColour(colour::forScene(static_cast<int>(k)));
            g.fillRoundedRectangle(row.removeFromLeft(4.0f).reduced(0.0f, 3.0f), 1.0f);
            row.removeFromLeft(6.0f);
            g.setColour(colour::text);
            g.drawText(juce::String(model.registry.spec(s.param).id), row.removeFromLeft(150.0f), juce::Justification::centredLeft, true);
            g.setColour(colour::textDim);
            g.drawText((s.depth > 0 ? "+" : "") + juce::String(juce::roundToInt(s.depth * 100.0f)) + "%", row.removeFromLeft(48.0f),
                       juce::Justification::centredLeft);
            g.drawText(s.periodSeconds >= 60.0f ? juce::String(s.periodSeconds / 60.0f, 1) + " min" : juce::String(juce::roundToInt(s.periodSeconds)) + " s",
                       row.removeFromLeft(58.0f), juce::Justification::centredLeft);
            g.drawText(shapes[static_cast<int>(s.shape)], row.removeFromLeft(60.0f), juce::Justification::centredLeft);
            // Where the cycle is now.
            auto lane = row.removeFromLeft(110.0f).reduced(0.0f, 7.0f);
            g.setColour(colour::panelHi);
            g.fillRoundedRectangle(lane, 2.0f);
            const float v = model.frame().seasonValue[k];
            g.setColour(colour::forScene(static_cast<int>(k)));
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ lane.getCentreX() + v * lane.getWidth() * 0.5f, lane.getCentreY() }));
            g.setColour(colour::textFaint);
            g.drawText("edit", row.removeFromLeft(36.0f), juce::Justification::centred);
            g.drawText("remove", row.removeFromLeft(50.0f), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto& list = model.core.seasons.getSeasons();
        const int rowIndex = static_cast<int>((e.position.y - 4.0f) / 19.0f);
        if (rowIndex < 0 || rowIndex >= static_cast<int>(list.size()))
            return;
        const float x = e.position.x - 6.0f;
        const float editX = 4.0f + 6.0f + 150.0f + 48.0f + 58.0f + 60.0f + 110.0f;
        if (x >= editX + 36.0f && x < editX + 86.0f)
            model.core.seasons.remove(rowIndex);
        else
            showEditMenu(rowIndex);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (model.onHover)
            model.onHover("Seasons: click a season to change its depth, length or shape");
    }

private:
    void showParamMenu(int index)
    {
        // Continuous, terrain-bound parameters grouped by their prefix.
        std::map<juce::String, juce::PopupMenu> groups;
        for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
        {
            const auto& s = model.registry.spec(i);
            if ((s.flags & engine::ParamFlag::kDiscrete) != 0 || (s.flags & engine::ParamFlag::kTerrainBound) == 0)
                continue;
            const juce::String id(s.id);
            if (id.contains(".fx"))
                continue;
            groups[id.upToFirstOccurrenceOf(".", false, false)].addItem(static_cast<int>(i) + 1, juce::String(s.name) + "  (" + id + ")");
        }
        juce::PopupMenu m;
        m.addSectionHeader("Which parameter should the season move?");
        for (auto& [name, sub] : groups)
            m.addSubMenu(name, sub);
        m.showMenuAsync(juce::PopupMenu::Options(), [this, index](int r) {
            if (r <= 0)
                return;
            engine::Season s;
            if (index >= 0)
                s = model.core.seasons.getSeasons()[static_cast<std::size_t>(index)];
            s.param = static_cast<engine::ParamIndex>(r - 1);
            const int at = index >= 0 ? index : static_cast<int>(model.core.seasons.getSeasons().size());
            if (! model.core.seasons.set(at, s))
                model.core.status("All 8 seasons are in use.", true);
        });
    }

    void showEditMenu(int index)
    {
        auto s = model.core.seasons.getSeasons()[static_cast<std::size_t>(index)];
        juce::PopupMenu depth, period, shape;
        for (int d : { -60, -40, -25, -15, -8, 8, 15, 25, 40, 60 })
            depth.addItem(100 + d + 100, (d > 0 ? "+" : "") + juce::String(d) + "%", true, juce::roundToInt(s.depth * 100.0f) == d);
        const int periods[] = { 20, 45, 90, 180, 300, 600, 1200, 2400, 3600 };
        for (int k = 0; k < 9; ++k)
            period.addItem(400 + k, periods[k] >= 60 ? juce::String(periods[k] / 60) + " min" : juce::String(periods[k]) + " s", true,
                           juce::roundToInt(s.periodSeconds) == periods[k]);
        const char* shapes[] = { "Sine", "Triangle", "Drift (random, smooth)" };
        for (int k = 0; k < 3; ++k)
            shape.addItem(500 + k, shapes[k], true, static_cast<int>(s.shape) == k);
        juce::PopupMenu m;
        m.addSectionHeader(juce::String(model.registry.spec(s.param).name));
        m.addSubMenu("Depth", depth);
        m.addSubMenu("Length", period);
        m.addSubMenu("Shape", shape);
        m.addItem(1, "Change parameter...");
        m.addSeparator();
        m.addItem(2, "Remove");
        m.showMenuAsync(juce::PopupMenu::Options(), [this, index, s, periods](int r) mutable {
            if (r == 1)
                return showParamMenu(index);
            if (r == 2)
                return model.core.seasons.remove(index);
            if (r >= 100 && r < 400)
                s.depth = static_cast<float>(r - 200) / 100.0f;
            else if (r >= 400 && r < 409)
                s.periodSeconds = static_cast<float>(periods[r - 400]);
            else if (r >= 500 && r < 503)
                s.shape = static_cast<engine::Season::Shape>(r - 500);
            else
                return;
            model.core.seasons.set(index, s);
        });
    }

    Model& model;
    FlatButton addButton;
    std::size_t shownCount = 0;
};

/** MIDI: what is mapped, learning actions, devices and the note channel. */
class MidiView final : public juce::Component, public Animated, private juce::ListBoxModel
{
public:
    explicit MidiView(Model& m) : model(m)
    {
        model.add(this);
        list.setModel(this);
        list.setRowHeight(20);
        list.setColour(juce::ListBox::backgroundColourId, colour::well);
        addAndMakeVisible(list);

        const std::pair<engine::MidiAction, const char*> actions[] = {
            { engine::MidiAction::FadeToggle, "Fade" },          { engine::MidiAction::Panic, "Panic" },
            { engine::MidiAction::Catch, "Catch" },              { engine::MidiAction::CaptureScene, "Capture scene" },
            { engine::MidiAction::ReleaseLive, "Release" },      { engine::MidiAction::RecordToggle, "Record" },
            { engine::MidiAction::LoopRecord, "Loop" },          { engine::MidiAction::LoopClear, "Loop clear" },
            { engine::MidiAction::FreezeToggle, "Freeze all" },  { engine::MidiAction::InputFreezeToggle, "Hold input" },
        };
        for (const auto& [action, name] : actions)
        {
            auto b = std::make_unique<FlatButton>(name, colour::learn);
            b->setHelp(&model, "press, then move a pad or button on your controller to trigger this");
            b->onClick = [this, action = action] { model.core.midi.learnAction(action); };
            addAndMakeVisible(*b);
            learnButtons.push_back(std::move(b));
        }
        defaults.onClick = [this] { model.core.midi.loadDefaultLayout(); };
        clearAll.onClick = [this] { model.core.midi.clearAll(); };
        defaults.setHelp(&model, "the built-in mapping for a generic 8-knob controller");
        notesToDrone.setClickingTogglesState(true);
        notesToDrone.onClick = [this] { model.core.midi.setNotesToDrone(notesToDrone.getToggleState()); };
        notesToDrone.setHelp(&model, "on: played notes set the drone's root as well as playing Bloom");
        channel.addItem("Notes: any channel", 1);
        for (int c = 1; c <= 16; ++c)
            channel.addItem("Notes: channel " + juce::String(c), c + 1);
        channel.onChange = [this] { model.core.midi.setNoteChannel(channel.getSelectedId() <= 1 ? -1 : channel.getSelectedId() - 2); };
        for (auto* c : std::initializer_list<juce::Component*> { &defaults, &clearAll, &notesToDrone, &channel })
            addAndMakeVisible(c);
    }
    ~MidiView() override { model.remove(this); }

    void tick() override
    {
        // Rebuild cheaply when anything visible changed.
        juce::String sig = juce::String(model.core.midi.getBindings().size()) + (model.core.midi.isLearning() ? "L" : "-");
        for (const auto& d : model.core.midiInputs.getDevices())
            sig << d.info.identifier << (d.enabled ? 1 : 0) << (d.open ? 1 : 0);
        sig << model.core.midi.getNoteChannel() << (model.core.midi.getNotesToDrone() ? 1 : 0);
        if (sig != signature)
        {
            signature = sig;
            texts.clear();
            for (const auto& b : model.core.midi.getBindings())
                texts.add(model.core.midi.describe(b));
            list.updateContent();
            list.repaint();
            rebuildDevices();
            channel.setSelectedId(model.core.midi.getNoteChannel() < 0 ? 1 : model.core.midi.getNoteChannel() + 2, juce::dontSendNotification);
            notesToDrone.setToggleState(model.core.midi.getNotesToDrone(), juce::dontSendNotification);
            repaint();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft(300);
        auto buttons = left.removeFromBottom(24);
        defaults.setBounds(buttons.removeFromLeft(130));
        buttons.removeFromLeft(4);
        clearAll.setBounds(buttons.removeFromLeft(90));
        left.removeFromBottom(4);
        list.setBounds(left.withTrimmedTop(16));
        r.removeFromLeft(10);

        auto learn = r.removeFromLeft(240).withTrimmedTop(16);
        for (std::size_t k = 0; k < learnButtons.size(); ++k)
        {
            const int col = static_cast<int>(k % 2), rowI = static_cast<int>(k / 2);
            learnButtons[k]->setBounds(learn.getX() + col * 120, learn.getY() + rowI * 28, 116, 24);
        }
        r.removeFromLeft(10);
        auto devices = r.withTrimmedTop(16);
        channel.setBounds(devices.removeFromTop(24).removeFromLeft(220));
        devices.removeFromTop(4);
        notesToDrone.setBounds(devices.removeFromTop(24).removeFromLeft(220));
        devices.removeFromTop(8);
        for (auto& b : deviceButtons)
        {
            b->setBounds(devices.removeFromTop(24).removeFromLeft(220));
            devices.removeFromTop(4);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.setFont(caps());
        g.setColour(colour::textFaint);
        g.drawText(model.core.midi.isLearning() ? "LEARNING: MOVE A CONTROL" : "MAPPINGS", 0, 0, 300, 14, juce::Justification::centredLeft);
        g.drawText("LEARN A PAD FOR", 310, 0, 240, 14, juce::Justification::centredLeft);
        g.drawText("NOTES AND DEVICES", 560, 0, 220, 14, juce::Justification::centredLeft);
    }

    int getNumRows() override { return texts.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected)
            g.fillAll(colour::panelHi);
        g.setFont(font(11.5f));
        g.setColour(colour::text);
        g.drawText(texts[row], 8, 0, w - 40, h, juce::Justification::centredLeft, true);
        g.setColour(colour::textFaint);
        g.drawText("x", w - 24, 0, 18, h, juce::Justification::centred);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (e.x > list.getWidth() - 34)
            model.core.midi.removeBinding(row);
    }

private:
    void rebuildDevices()
    {
        for (auto& b : deviceButtons)
            removeChildComponent(b.get());
        deviceButtons.clear();
        for (const auto& d : model.core.midiInputs.getDevices())
        {
            auto b = std::make_unique<FlatButton>(d.info.name + (d.enabled && ! d.open ? " (unavailable)" : ""), colour::good);
            b->setToggleState(d.enabled, juce::dontSendNotification);
            b->setHelp(&model, "listen to this MIDI device");
            const auto id = d.info.identifier;
            const bool enabled = d.enabled;
            b->onClick = [this, id, enabled] { juce::MessageManager::callAsync([this, id, enabled] { model.core.midiInputs.setEnabled(id, ! enabled); }); };
            addAndMakeVisible(*b);
            deviceButtons.push_back(std::move(b));
        }
        resized();
    }

    Model& model;
    juce::ListBox list;
    juce::StringArray texts;
    std::vector<std::unique_ptr<FlatButton>> learnButtons, deviceButtons;
    FlatButton defaults { "Default mapping" }, clearAll { "Clear all" }, notesToDrone { "Notes move the drone", colour::good };
    juce::ComboBox channel;
    juce::String signature;
};

/** Which effect chain to show on the Effects page. */
class ChainPicker final : public juce::Component
{
public:
    ChainPicker(int selected, std::function<void(int)> pick) : current(selected), onPick(std::move(pick)) {}
    static juce::String nameFor(int i)
    {
        if (i < engine::kNumStrips)
            return engine::kStrips[static_cast<std::size_t>(i)].name;
        return i == engine::kNumStrips ? "Reverb bus" : (i == engine::kNumStrips + 1 ? "Delay bus" : "Master");
    }
    static constexpr int count = engine::kNumStrips + 3;
    void paint(juce::Graphics& g) override
    {
        for (int i = 0; i < count; ++i)
        {
            const auto r = cell(i).toFloat().reduced(1.0f);
            g.setColour(i == current ? colour::tide : (cell(i).contains(getMouseXYRelative()) && isMouseOver() ? colour::panelHi.brighter(0.08f) : colour::panelHi));
            g.fillRoundedRectangle(r, metric::radius);
            g.setColour(i == current ? colour::well : colour::text);
            g.setFont(font(11.0f, 500));
            g.drawText(nameFor(i), r.reduced(5.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int i = 0; i < count; ++i)
            if (cell(i).contains(e.getPosition()))
                return onPick(i);
    }

private:
    juce::Rectangle<int> cell(int i) const
    {
        const int rows = (count + 1) / 2;
        const int h = getHeight() / rows, w = getWidth() / 2;
        return { (i / rows) * w, (i % rows) * h, w, h };
    }
    int current;
    std::function<void(int)> onPick;
};

} // namespace

// --- Device ---------------------------------------------------------------------------

Device::Device(Model& m, juce::String t, juce::Colour c) : model(m), title(std::move(t)), tab(c) {}

ParamComponent* Device::add(P p, juce::String help)
{
    if (help.isEmpty())
        help = helpFor(p);
    const auto& s = model.spec(p);
    const bool discrete = (s.flags & engine::ParamFlag::kDiscrete) != 0;
    const auto choices = model.choices(p);
    if (discrete && s.minValue == 0.0f && s.maxValue == 1.0f && (choices.isEmpty() || choices[0] == "Off"))
        return add(std::make_unique<Toggle>(model, p, model.name(p), help), 2 * metric::knobW, 26);
    if (discrete && ! choices.isEmpty() && choices.size() <= 6)
    {
        const int cols = choices.size() <= 3 ? choices.size() : (choices.size() == 4 ? 2 : 3);
        auto c = std::make_unique<Choice>(model, p, cols, help);
        int cell = 0;
        for (const auto& t : choices)
            cell = std::max(cell, juce::GlyphArrangement::getStringWidthInt(font(11.5f, 600), t) + 18);
        const int w = std::max(2 * metric::knobW, cols * cell);
        const int h = c->preferredHeight(w) + 16;
        auto* raw = add(std::move(c), w, h);
        raw->setLabel(model.name(p));
        return raw;
    }
    return addKnob(p, {}, help);
}

ParamComponent* Device::addKnob(P p, juce::String label, juce::String help, int w, int h)
{
    auto* k = add(std::make_unique<Knob>(model, p, help.isNotEmpty() ? help : helpFor(p)), w, h);
    if (label.isNotEmpty())
        k->setLabel(label);
    return k;
}

juce::Rectangle<int> Device::content() const
{
    return getLocalBounds().withTrimmedTop(metric::header).reduced(metric::pad - 2, 6);
}

int Device::preferredWidth(int height) const
{
    const int avail = height - metric::header - 12 - (top.c != nullptr ? top.h + kGap : 0);
    int width = 0, colW = 0, y = 0;
    for (const auto& it0 : items)
    {
        if (! it0.c->isVisible())
            continue;
        const Item it { it0.c, it0.w, it0.h > 0 ? it0.h : avail };
        if (y > 0 && y + it.h > avail)
        {
            width += colW + kGap;
            colW = 0;
            y = 0;
        }
        colW = std::max(colW, it.w);
        y += it.h + kGap;
    }
    width += colW;
    width = std::max(width, top.c != nullptr ? top.w : 0);
    return std::max(width, 120) + 2 * (metric::pad - 2);
}

void Device::resized()
{
    auto r = content();
    if (top.c != nullptr)
    {
        top.c->setBounds(r.removeFromTop(top.h));
        r.removeFromTop(kGap);
    }
    int x = r.getX(), y = r.getY(), colW = 0;
    for (const auto& it0 : items)
    {
        if (! it0.c->isVisible())
            continue;
        const Item it { it0.c, it0.w, it0.h > 0 ? it0.h : r.getHeight() };
        if (y > r.getY() && y + it.h > r.getBottom())
        {
            x += colW + kGap;
            colW = 0;
            y = r.getY();
        }
        // A switch or choice row with a label above it.
        if (dynamic_cast<Choice*>(it.c) != nullptr)
            it.c->setBounds(x, y + 16, it.w, it.h - 16);
        else
            it.c->setBounds(x, y, it.w, it.h);
        colW = std::max(colW, it.w);
        y += it.h + kGap;
    }
}

void Device::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), title, colour::text);
    // The device's colour tab on its title bar.
    g.setColour(tab);
    g.fillRoundedRectangle(juce::Rectangle<float>(3.0f, 6.0f, 3.0f, static_cast<float>(metric::header) - 12.0f), 1.0f);
    g.setFont(font(11.0f, 500));
    g.setColour(colour::textDim);
    for (const auto& it : items)
        if (auto* c = dynamic_cast<Choice*>(it.c); c != nullptr && c->isVisible())
            g.drawText(model.name(c->getParam()), c->getX(), c->getY() - 16, c->getWidth(), 14, juce::Justification::centredLeft);
}

// --- DeviceView -----------------------------------------------------------------------

DeviceView::DeviceView(Model& m) : model(m)
{
    viewport.setViewedComponent(&row, false);
    viewport.setScrollBarsShown(false, true, false, true);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);
    build();
}

DeviceView::~DeviceView()
{
    devices.clear();
}

juce::String DeviceView::pageName(int p)
{
    static const char* names[] = { "Drone", "Clouds", "Resonator", "Bloom", "Input", "Looper", "Weather", "Gestures", "Loops", "Seasons", "Mixer", "Effects", "Master", "MIDI" };
    return names[juce::jlimit(0, NumPages - 1, p)];
}

juce::Rectangle<int> DeviceView::tabBounds(int i) const
{
    int x = 6;
    for (int k = 0; k < i; ++k)
        x += juce::GlyphArrangement::getStringWidthInt(font(12.0f, 600), pageName(k)) + 22;
    return { x, 0, juce::GlyphArrangement::getStringWidthInt(font(12.0f, 600), pageName(i)) + 20, kTabH };
}

void DeviceView::show(int p)
{
    if (p == page)
        return;
    page = juce::jlimit(0, NumPages - 1, p);
    build();
    repaint();
}

void DeviceView::showEffectsFor(int chain)
{
    fxChain = juce::jlimit(0, ChainPicker::count - 1, chain);
    page = Effects;
    build();
    repaint();
}

void DeviceView::paint(juce::Graphics& g)
{
    g.setColour(colour::window);
    g.fillRect(getLocalBounds().removeFromTop(kTabH));
    for (int i = 0; i < NumPages; ++i)
    {
        const auto r = tabBounds(i).toFloat();
        const bool on = i == page;
        if (on || i == hoverTab)
        {
            g.setColour(on ? colour::panel : colour::panel.withAlpha(0.45f));
            juce::Path p;
            p.addRoundedRectangle(r.getX(), r.getY() + 3.0f, r.getWidth(), r.getHeight() - 3.0f, metric::radius, metric::radius, true, true, false, false);
            g.fillPath(p);
        }
        if (on)
        {
            g.setColour(colour::accent);
            g.fillRect(r.getX() + 6.0f, r.getY() + 3.0f, r.getWidth() - 12.0f, 2.0f);
        }
        g.setFont(font(12.0f, on ? 600 : 500));
        g.setColour(on ? colour::text : colour::textDim);
        g.drawText(pageName(i), r.withTrimmedTop(3.0f), juce::Justification::centred);
    }
    g.setColour(colour::panel);
    g.fillRect(getLocalBounds().withTrimmedTop(kTabH));
}

void DeviceView::resized()
{
    viewport.setBounds(getLocalBounds().withTrimmedTop(kTabH).reduced(4, 4));
    layoutRow();
}

void DeviceView::layoutRow()
{
    const int h = std::max(160, viewport.getHeight() - (viewport.isHorizontalScrollBarShown() ? 8 : 0));
    int x = 0;
    for (auto& d : devices)
    {
        const int w = d->preferredWidth(h);
        d->setBounds(x, 0, w, h);
        x += w + metric::gap;
    }
    row.setSize(std::max(x - metric::gap, viewport.getWidth()), h);
}

void DeviceView::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < NumPages; ++i)
        if (tabBounds(i).contains(e.getPosition()))
            return show(i);
}

void DeviceView::mouseMove(const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < NumPages; ++i)
        if (tabBounds(i).contains(e.getPosition()))
            h = i;
    if (h != hoverTab)
    {
        hoverTab = h;
        repaint(getLocalBounds().removeFromTop(kTabH));
    }
}

void DeviceView::mouseExit(const juce::MouseEvent&)
{
    hoverTab = -1;
    repaint(getLocalBounds().removeFromTop(kTabH));
}

void DeviceView::build()
{
    for (auto& d : devices)
        row.removeChildComponent(d.get());
    devices.clear();

    auto device = [&](juce::String title, juce::Colour tab = colour::accent) -> Device& {
        devices.push_back(std::make_unique<Device>(model, std::move(title), tab));
        return *devices.back();
    };
    auto params = [&](Device& d, std::initializer_list<P> ps) {
        for (auto p : ps)
            d.add(p);
    };
    auto strip = [&](int s, juce::Colour tab) {
        const auto& info = engine::kStrips[static_cast<std::size_t>(s)];
        auto& d = device("Strip", tab);
        d.add(std::make_unique<FaderMeter>(model, info.level, s, "Level"), 64, 0);
        d.addKnob(info.pan, {}, {}, 56, 62);
        d.addKnob(info.sendA, "Reverb", "send to the reverb bus", 56, 62);
        d.addKnob(info.sendB, "Delay", "send to the delay bus", 56, 62);
        auto fx = std::make_unique<FlatButton>("Effects");
        fx->setHelp(&model, "open this strip's two insert effects");
        fx->onClick = [this, s] { juce::MessageManager::callAsync([this, s] { showEffectsFor(s); }); };
        d.add(std::move(fx), 56, 24);
    };
    auto sample = [&](Device& d, int slot, juce::Colour c) {
        auto w = std::make_unique<Waveform>(model, slot, c);
        w->onLoad = [this](int s) {
            if (onLoadSample)
                onLoadSample(s);
        };
        d.setTop(std::move(w), 56, 6 * metric::knobW);
    };

    const auto sceneTint = [](int i) { return colour::forScene(i); };
    switch (page)
    {
        case Drone:
        {
            auto& d = device("Drone", sceneTint(0));
            params(d, { P::DroneRoot, P::DroneDensity, P::DroneShape, P::DroneDetune, P::DroneCutoff, P::DroneResonance, P::DroneNoise, P::DroneEvolve });
            auto& m = device("Motion", sceneTint(0));
            params(m, { P::DroneDriftDepth, P::DroneDriftRate, P::DroneSpread, P::DroneGravity });
            strip(static_cast<int>(engine::StripId::Drone), sceneTint(0));
            break;
        }
        case Clouds:
            for (int c = 0; c < engine::kNumClouds; ++c)
            {
                const auto tint = sceneTint(c + 1);
                auto& d = device("Cloud " + juce::String(c + 1), tint);
                sample(d, c, tint);
                const auto first = engine::idx(engine::kCloudFirstParam[static_cast<std::size_t>(c)]);
                for (engine::ParamIndex k = 0; k < 12; ++k)
                    d.add(static_cast<P>(first + k));
                auto& s = device("Cloud " + juce::String(c + 1) + " strip", tint);
                const auto& info = engine::kStrips[static_cast<std::size_t>(c + 1)];
                s.add(std::make_unique<FaderMeter>(model, info.level, c + 1, "Level"), 64, 0);
                s.addKnob(info.pan, {}, {}, 56, 62);
                s.addKnob(info.sendA, "Reverb", {}, 56, 62);
                s.addKnob(info.sendB, "Delay", {}, 56, 62);
            }
            break;
        case Resonator:
        {
            auto& d = device("Resonator", sceneTint(2));
            params(d, { P::ResRoot, P::ResModes, P::ResStructure, P::ResDecay, P::ResBrightness, P::ResSpread, P::ResGravity });
            auto& r = device("Rain", sceneTint(2));
            params(r, { P::ResRain, P::ResRainColour });
            auto& x = device("Excite from", sceneTint(2));
            x.addKnob(P::ResExciteInput, "Input");
            x.addKnob(P::ResExciteDrone, "Drone");
            x.addKnob(P::ResExciteClouds, "Clouds");
            x.addKnob(P::ResExciteBloom, "Bloom");
            strip(static_cast<int>(engine::StripId::Resonator), sceneTint(2));
            break;
        }
        case Bloom:
        {
            auto& d = device("Bloom", sceneTint(3));
            sample(d, engine::kNumClouds, sceneTint(3));
            params(d, { P::BloomTransform, P::BloomAmount, P::BloomLength, P::BloomAttack, P::BloomRelease, P::BloomPitch, P::BloomTone, P::BloomSpread,
                        P::BloomRandom, P::BloomPosition, P::BloomGravity, P::BloomRoot });
            auto& k = device("Play", sceneTint(3));
            k.add(std::make_unique<KeyboardStrip>(model), 420, 120);
            strip(static_cast<int>(engine::StripId::Bloom), sceneTint(3));
            break;
        }
        case Input:
        {
            auto& d = device("Live input", sceneTint(4));
            params(d, { P::InputArmed, P::InputChannel, P::InputGain, P::InputHighPass, P::InputGate });
            auto& f = device("Hold", sceneTint(4));
            params(f, { P::InputFreeze, P::InputFreezeLevel, P::InputFreezeDrift });
            auto& c = device("Catch", sceneTint(4));
            params(c, { P::CatchSource, P::CatchTarget, P::CatchSeconds });
            auto catchButton = std::make_unique<FlatButton>("Catch now", colour::accent);
            catchButton->setHelp(&model, "grab the last seconds into a cloud (K)");
            catchButton->onClick = [this] { model.engine.command(engine::Command::Catch); };
            c.add(std::move(catchButton), 2 * metric::knobW, 26);
            strip(static_cast<int>(engine::StripId::Input), sceneTint(4));
            break;
        }
        case Looper:
        {
            auto& d = device("Disintegration looper", sceneTint(5));
            d.add(std::make_unique<LooperView>(model), 260, 0);
            params(d, { P::LoopSource, P::LoopErosion, P::LoopFlakes, P::LoopOverdub });
            strip(static_cast<int>(engine::StripId::Loop), sceneTint(5));
            break;
        }
        case Weather:
        {
            auto& d = device("Weather", sceneTint(6));
            params(d, { P::WeatherWind, P::WeatherRain, P::WeatherSurf, P::WeatherGust, P::WeatherTone, P::WeatherDistance });
            strip(static_cast<int>(engine::StripId::Weather), sceneTint(6));
            break;
        }
        case Gestures:
        {
            auto& s = device("Swell", colour::accent);
            params(s, { P::SwellDepth, P::SwellAttack, P::SwellRelease });
            auto& h = device("Hush", colour::accent);
            params(h, { P::HushDepth });
            auto& f = device("Freeze all", colour::tide);
            params(f, { P::FreezeOn, P::FreezeDuck, P::FreezeTexture });
            auto& t = device("Terrain", colour::tide);
            params(t, { P::TerrainWanderStyle, P::TerrainGlide, P::TerrainFocus, P::TerrainWander, P::TerrainWanderRate, P::TideRate, P::HarmonyMorph });
            auto& m = device("Medium", colour::live);
            params(m, { P::MediumType, P::MediumAge, P::MediumNoise, P::MediumWobble, P::MediumDrive, P::MediumMix });
            strip(static_cast<int>(engine::StripId::Freeze), colour::tide);
            break;
        }
        case Loops:
        {
            auto& d = device("Incommensurate loops", sceneTint(7));
            params(d, { P::LoopsOn, P::LoopsTarget, P::LoopsCount, P::LoopsPattern, P::LoopsRate, P::LoopsDensity, P::LoopsRegister, P::LoopsSpread,
                        P::LoopsVelocity });
            break;
        }
        case Seasons:
        {
            auto& d = device("Seasons", sceneTint(8));
            d.add(std::make_unique<SeasonList>(model), 560, 0);
            auto& g = device("All seasons", sceneTint(8));
            params(g, { P::SeasonsDepth });
            break;
        }
        case Mixer:
        {
            for (int s = 0; s < engine::kNumStrips; ++s)
            {
                const auto& info = engine::kStrips[static_cast<std::size_t>(s)];
                auto& d = device(info.name, sceneTint(s));
                d.add(std::make_unique<FaderMeter>(model, info.level, s, "Level"), 58, 0);
                d.addKnob(info.pan, {}, {}, 52, 62);
                d.addKnob(info.sendA, "Reverb", {}, 52, 62);
                d.addKnob(info.sendB, "Delay", {}, 52, 62);
            }
            auto& r = device("Returns", colour::tide);
            r.add(std::make_unique<FaderMeter>(model, P::BusALevel, -1, "Reverb"), 58, 0);
            r.add(std::make_unique<FaderMeter>(model, P::BusBLevel, -1, "Delay"), 58, 0);
            break;
        }
        case Effects:
        {
            auto& pick = device("Chain", colour::tide);
            pick.add(std::make_unique<ChainPicker>(fxChain, [this](int i) { juce::MessageManager::callAsync([this, i] { showEffectsFor(i); }); }), 220, 0);
            const int firstSlot = fxChain < engine::kNumStrips ? fxChain * 2 : engine::kBusASlot + (fxChain - engine::kNumStrips) * 2;
            for (int k = 0; k < 2; ++k)
                devices.push_back(std::make_unique<FxDevice>(model, firstSlot + k));
            break;
        }
        case Master:
        {
            auto& d = device("Master", colour::accent);
            d.add(std::make_unique<FaderMeter>(model, P::MasterLevel, -1, "Master"), 64, 0);
            params(d, { P::MasterFadeSecs, P::MasterCeiling });
            auto& a = device("Auto master", colour::good);
            params(a, { P::MasterAuto, P::MasterAutoTarget, P::MasterAutoAmount });
            a.add(std::make_unique<AutoMasterView>(model), 250, 0);
            auto& fx = device("Master effects", colour::tide);
            auto open = std::make_unique<FlatButton>("Master inserts");
            open->onClick = [this] { juce::MessageManager::callAsync([this] { showEffectsFor(engine::kNumStrips + 2); }); };
            fx.add(std::move(open), 2 * metric::knobW, 26);
            auto rev = std::make_unique<FlatButton>("Reverb bus");
            rev->onClick = [this] { juce::MessageManager::callAsync([this] { showEffectsFor(engine::kNumStrips); }); };
            fx.add(std::move(rev), 2 * metric::knobW, 26);
            auto del = std::make_unique<FlatButton>("Delay bus");
            del->onClick = [this] { juce::MessageManager::callAsync([this] { showEffectsFor(engine::kNumStrips + 1); }); };
            fx.add(std::move(del), 2 * metric::knobW, 26);
            break;
        }
        case Midi:
        {
            auto& d = device("MIDI", colour::learn);
            d.add(std::make_unique<MidiView>(model), 800, 0);
            break;
        }
        default: break;
    }

    for (auto& d : devices)
        row.addAndMakeVisible(*d);
    layoutRow();
    viewport.setViewPosition(0, 0);
}

} // namespace tf::app::gui
