#include "Pages.h"
#include "Timeline.h"

#include "../LinkSync.h"
#include "../PluginHost.h"

#include <dsp/core/TempoSync.h>
#include <engine/mix/Layout.h>

#include <map>

namespace tf::app::gui {
using engine::P;

namespace {
constexpr int kGap = 4;
constexpr int kTabH = 26;

juce::String helpFor(P p)
{
    static const std::map<P, const char*> help {
        { P::DroneRoot, "the drone's lowest note; the voices stack on it in the key" },
        { P::DroneCutoff, "opens the drone's filter" },
        { P::DroneShape, "each voice's tone: a bright saw on the left, a pure sine on the right" },
        { P::DroneDensity, "how many voices sound at once" },
        { P::DroneEvolve, "how often voices move to new notes of the chord" },
        { P::DroneGravity, "how strongly voices are pulled into the key" },
        { P::DroneNoise, "air and breath under the tone" },
        { P::ResStructure, "from strings (left) to bars and bells (right)" },
        { P::ResRain, "random strikes, like drops on a resonant surface" },
        { P::ResDecay, "how long each mode rings" },
        { P::ResGravity, "pulls the modes into the key; bells (Structure far right) keep their own tuning" },
        { P::BloomTransform, "what a held note turns into while it sustains" },
        { P::BloomAmount, "how strongly the transform acts" },
        { P::BloomAttack, "how each note fades in; Swell rises out of the sample by itself, and Ghost always takes at least 1.5 s" },
        { P::BloomPosition, "where in the sound Freeze and Ghost take their moment" },
        { P::Cloud1Shape, "each grain's envelope: percussive on the left, soft in the middle, flat and full on the right" },
        { P::Cloud2Shape, "each grain's envelope: percussive on the left, soft in the middle, flat and full on the right" },
        { P::Cloud3Shape, "each grain's envelope: percussive on the left, soft in the middle, flat and full on the right" },
        { P::Cloud4Shape, "each grain's envelope: percussive on the left, soft in the middle, flat and full on the right" },
        { P::InputArmed, "hear the live input through its strip" },
        { P::InputFreeze, "hold the input's sound forever as a spectral pad" },
        { P::LoopErosion, "how much each pass wears the tape" },
        { P::LoopFlakes, "dropouts, like oxide falling off old tape; they come with Erosion" },
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
        { P::LoopsRate, "pace of every cycle; they never line up" },
        { P::LoopsPattern, "which set of cycle lengths and notes" },
        { P::SeasonsDepth, "scales every season at once" },
        { P::ModLfo1Rate, "how fast LFO 1 cycles; it follows Tide" },
        { P::ModLfo2Rate, "how fast LFO 2 cycles; it follows Tide" },
        { P::ModLfo3Rate, "how fast LFO 3 cycles; it follows Tide" },
        { P::ModLfo4Rate, "how fast LFO 4 cycles; it follows Tide" },
        { P::ModRandom1Rate, "how often Random 1 picks a new value" },
        { P::ModRandom2Rate, "how often Random 2 picks a new value" },
        { P::ModRandom1Smooth, "low: jumps to each new value; high: drifts there" },
        { P::ModRandom2Smooth, "low: jumps to each new value; high: drifts there" },
        { P::ModFollowAttack, "how quickly the followers rise when the sound gets louder" },
        { P::ModFollowRelease, "how slowly the followers fall back when it gets quieter" },
        { P::ModFollowGain, "raise for a quiet input, lower for a loud one" },
    };
    const auto it = help.find(p);
    return it != help.end() ? juce::String(it->second) : juce::String();
}

std::function<juce::String(float)> fxFormatter(const dsp::ProcessorControl& c)
{
    return [c](float v) {
        char buf[48] {};
        c.display.format(v, buf, static_cast<int>(sizeof(buf)));
        return juce::String(buf);
    };
}

class FxDevice final : public Device, public Animated
{
public:
    FxDevice(Model& m, int s) : Device(m, engine::kFxSlots[static_cast<std::size_t>(s)].name, colour::tide()), slot(s)
    {
        model.add(this);
        menu = setTop(std::make_unique<juce::ComboBox>(), 24, 4 * metric::knobW);
        menu->onChange = [this] { chosen(menu->getSelectedId()); };
        const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(s)].firstParam);
        for (int k = 0; k < 7; ++k)
            knobs[static_cast<std::size_t>(k)] = static_cast<Knob*>(addKnob(static_cast<P>(first + static_cast<engine::ParamIndex>(k))));
        auto open = std::make_unique<FlatButton>("Plugin window");
        open->setHelp(&model, "open the plugin's own controls");
        open->onClick = [this] {
            if (model.core.plugins != nullptr)
                model.core.plugins->openEditor(slot);
        };
        openButton = add(std::move(open), 2 * metric::knobW, 24);
        fillMenu();
        refresh();
    }
    ~FxDevice() override { model.remove(this); }

    void tick() override
    {
        const int version = model.core.plugins != nullptr ? model.core.plugins->getListVersion() : 0;
        if (version != shownListVersion)
            fillMenu();
        if (model.core.fx.getType(slot) != shownType)
            refresh();
        if (model.core.plugins != nullptr && model.core.fx.isExternal(slot) && ++syncFrames % 6 == 0)
            followPluginWindow();
    }

private:
    static constexpr int kScanId = 900, kPluginBase = 1000;

    void followPluginWindow()
    {
        const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(slot)].firstParam);
        for (int k = 0; k < 6; ++k)
        {
            const float pluginValue = model.core.plugins->parameterValue(slot, k);
            const auto p = static_cast<P>(first + static_cast<engine::ParamIndex>(k));
            if (pluginValue >= 0.0f && std::abs(pluginValue - model.value(p)) > 0.01f && std::abs(model.modulation(p)) < 1.0e-4f
                && ! knobs[static_cast<std::size_t>(k)]->isMouseButtonDown())
                model.set(p, pluginValue, false);
        }
    }

    void fillMenu()
    {
        auto* plugins = model.core.plugins.get();
        shownListVersion = plugins != nullptr ? plugins->getListVersion() : 0;
        pluginList = plugins != nullptr ? plugins->effects() : std::vector<juce::PluginDescription> {};
        menu->clear(juce::dontSendNotification);
        auto* root = menu->getRootMenu();
        root->addItem(1, "Empty");
        root->addSectionHeader("Tidefield");
        const auto& entries = dsp::ProcessorFactory::instance().entries();
        for (std::size_t k = 0; k < entries.size(); ++k)
            root->addItem(static_cast<int>(k) + 2, entries[k].info->name);
        if (plugins != nullptr)
        {
            root->addSectionHeader("Plugins");
            std::map<juce::String, juce::PopupMenu> byMaker;
            for (std::size_t k = 0; k < pluginList.size(); ++k)
            {
                const auto& d = pluginList[k];
                const auto maker = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String("Other");
                byMaker[maker].addItem(kPluginBase + static_cast<int>(k), d.name + "  (" + d.pluginFormatName + ")");
            }
            for (auto& [maker, sub] : byMaker)
                root->addSubMenu(maker, sub);
            root->addItem(kScanId, plugins->hasScanned() ? "Scan for new plugins" : "Find my plugins...");
        }
        refresh();
    }

    void chosen(int id)
    {
        if (id == kScanId)
        {
            menu->setSelectedId(selectedId, juce::dontSendNotification);
            if (model.core.plugins != nullptr)
                model.core.plugins->startScan();
            return;
        }
        std::string type;
        if (id >= kPluginBase && id - kPluginBase < static_cast<int>(pluginList.size()))
            type = PluginHost::typeIdFor(pluginList[static_cast<std::size_t>(id - kPluginBase)]);
        else if (id >= 2 && id < kScanId)
            type = dsp::ProcessorFactory::instance().entries()[static_cast<std::size_t>(id - 2)].info->typeId;
        model.core.setEffect(slot, type);
    }

    void refresh()
    {
        shownType = model.core.fx.getType(slot);
        const auto* info = model.core.fx.getInfo(slot);
        const bool isPlugin = model.core.fx.isExternal(slot);
        selectedId = 1;
        const auto& entries = dsp::ProcessorFactory::instance().entries();
        for (std::size_t k = 0; k < entries.size(); ++k)
            if (shownType == entries[k].info->typeId)
                selectedId = static_cast<int>(k) + 2;
        for (std::size_t k = 0; k < pluginList.size(); ++k)
            if (shownType == PluginHost::typeIdFor(pluginList[k]))
                selectedId = kPluginBase + static_cast<int>(k);
        menu->setSelectedId(selectedId, juce::dontSendNotification);
        for (int k = 0; k < 6; ++k)
        {
            auto* kn = knobs[static_cast<std::size_t>(k)];
            const bool used = info != nullptr && info->controls[static_cast<std::size_t>(k)].display.curve != dsp::DisplayMap::Curve::Hidden
                              && info->controls[static_cast<std::size_t>(k)].name[0] != '\0';
            kn->setVisible(used);
            if (! used)
                continue;
            kn->setLabel(info->controls[static_cast<std::size_t>(k)].name);
            kn->formatter = fxFormatter(info->controls[static_cast<std::size_t>(k)]);
            if (isPlugin)
                kn->formatter = [this, k, fallback = kn->formatter](float v) {
                    const auto text = model.core.plugins != nullptr ? model.core.plugins->parameterText(slot, k, v) : juce::String();
                    return text.isNotEmpty() ? text : fallback(v);
                };
            else if (k == 0 && (shownType == "tf.delay" || shownType == "tf.wornEcho"))
                kn->formatter = [this, free = kn->formatter, control = info->controls[0]](float v) {
                    const auto& f = model.frame();
                    if (! f.syncOn)
                        return free(v);
                    const int d = dsp::nearestDivision(control.display.value(v) * 0.001f, 60.0f / std::max(20.0f, f.bpm), 2.0f);
                    return d < 0 ? free(v) : juce::String(dsp::kBeatDivisions[static_cast<std::size_t>(d)].name);
                };
        }
        knobs[6]->setVisible(info != nullptr);
        openButton->setVisible(isPlugin);
        const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(slot)].firstParam);
        std::vector<P> ps;
        for (engine::ParamIndex k = 0; k < 7; ++k)
            ps.push_back(static_cast<P>(first + k));
        setPresets(shownType.empty() || isPlugin ? std::string() : "fx:" + shownType, std::string(engine::kFxSlots[static_cast<std::size_t>(slot)].id) + ".",
                   ps);
        title = info != nullptr ? juce::String(info->name) : juce::String(engine::kFxSlots[static_cast<std::size_t>(slot)].name);
        resized();
        repaint();
    }

    int slot;
    juce::ComboBox* menu = nullptr;
    FlatButton* openButton = nullptr;
    std::array<Knob*, 7> knobs {};
    std::string shownType = "\x01";
    std::vector<juce::PluginDescription> pluginList;
    int shownListVersion = -1;
    int selectedId = 1;
    int syncFrames = 0;
};

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

class AutoMasterView final : public juce::Component, public Animated
{
public:
    explicit AutoMasterView(Model& m) : model(m) { model.add(this); }
    ~AutoMasterView() override { model.remove(this); }
    void tick() override
    {
        const auto& a = model.frame().autoMaster;
        const float settings = model.value(P::MasterAuto) + 10.0f * model.value(P::MasterAutoTarget);
        if (isShowing() && (a != shown || settings != shownSettings))
        {
            shown = a;
            shownSettings = settings;
            repaint();
        }
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
        g.setColour(on ? display::text() : display::textFaint());
        const float lufs = a[0];
        g.drawText(lufs > -69.0f ? juce::String(lufs, 1) : juce::String("--"), r.removeFromTop(28.0f), juce::Justification::centredLeft);
        g.setFont(font(10.5f, 500));
        g.setColour(display::textDim());
        g.drawText("LUFS short-term, target " + juce::String(target, 0) + (on ? "" : "   (off)"), r.removeFromTop(16.0f), juce::Justification::centredLeft);
        r.removeFromTop(6.0f);

        auto bar = [&](const juce::String& name, float v, float range, const juce::String& text) {
            auto row = r.removeFromTop(17.0f);
            g.setColour(display::textDim());
            g.drawText(name, row.removeFromLeft(54.0f), juce::Justification::centredLeft);
            auto val = row.removeFromRight(56.0f);
            auto track = row.reduced(0.0f, 5.0f);
            g.setColour(display::panelHi());
            g.fillRoundedRectangle(track, 2.0f);
            const float c = track.getCentreX();
            const float x = c + juce::jlimit(-1.0f, 1.0f, v / range) * track.getWidth() * 0.5f;
            g.setColour(on ? display::accent() : display::textFaint());
            g.fillRoundedRectangle(juce::Rectangle<float>(std::min(c, x), track.getY(), std::abs(x - c) + 1.0f, track.getHeight()), 2.0f);
            g.setColour(display::text());
            g.drawText(text, val, juce::Justification::centredRight);
        };
        auto db = [](float v) { return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " dB"; };
        bar("Low", a[2], 6.0f, db(a[2]));
        bar("Low mid", a[3], 6.0f, db(a[3]));
        bar("High", a[4], 6.0f, db(a[4]));
        bar("Glue", -a[6], 6.0f, db(-a[6]));
        bar("Width", a[5] - 1.0f, 0.5f, juce::String(juce::roundToInt(a[5] * 100.0f)) + "%");
        bar("Make-up", a[1], 12.0f, db(a[1]));
    }

private:
    Model& model;
    std::array<float, 8> shown {};
    float shownSettings = -1.0f;
};

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
        const auto& f = model.frame();
        const float sig = static_cast<float>(f.loopState) * 1000.0f + f.loopPosition + f.loopSeconds * 0.001f + static_cast<float>(f.loopPasses) * 7.0f;
        if (isShowing() && sig != shownSig)
        {
            shownSig = sig;
            repaint();
        }
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
        g.setColour(display::panelHi());
        g.strokePath(track, juce::PathStrokeType(5.0f));
        if (st >= 2)
        {
            fill.addCentredArc(ring.getCentreX(), ring.getCentreY(), size * 0.5f, size * 0.5f, 0.0f, 0.0f,
                               juce::MathConstants<float>::twoPi * f.loopPosition, true);
            g.setColour(st == 3 ? display::warn() : display::accent());
            g.strokePath(fill, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (st == 1)
        {
            g.setColour(display::warn());
            g.fillEllipse(ring.withSizeKeepingCentre(14.0f, 14.0f));
        }
        auto text = r.withLeft(ring.getRight() + 14.0f).reduced(0.0f, 8.0f);
        g.setFont(font(16.0f, 600));
        g.setColour(display::text());
        g.drawText(states[st], text.removeFromTop(24.0f), juce::Justification::centredLeft);
        g.setFont(font(11.5f, 500));
        g.setColour(display::textDim());
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
    float shownSig = -1.0f;
};

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
            repaint();
        }
        if (isShowing() && ++frames % 6 == 0)
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
            g.setColour(display::textFaint());
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
            g.setColour(display::forScene(static_cast<int>(k)));
            g.fillRoundedRectangle(row.removeFromLeft(4.0f).reduced(0.0f, 3.0f), 1.0f);
            row.removeFromLeft(6.0f);
            g.setColour(display::text());
            g.drawText(juce::String(model.registry.spec(s.param).id), row.removeFromLeft(150.0f), juce::Justification::centredLeft, true);
            g.setColour(display::textDim());
            g.drawText((s.depth > 0 ? "+" : "") + juce::String(juce::roundToInt(s.depth * 100.0f)) + "%", row.removeFromLeft(48.0f),
                       juce::Justification::centredLeft);
            g.drawText(s.periodSeconds >= 60.0f ? juce::String(s.periodSeconds / 60.0f, 1) + " min" : juce::String(juce::roundToInt(s.periodSeconds)) + " s",
                       row.removeFromLeft(58.0f), juce::Justification::centredLeft);
            g.drawText(shapes[static_cast<int>(s.shape)], row.removeFromLeft(60.0f), juce::Justification::centredLeft);
            auto lane = row.removeFromLeft(110.0f).reduced(0.0f, 7.0f);
            g.setColour(display::panelHi());
            g.fillRoundedRectangle(lane, 2.0f);
            const float v = model.frame().seasonValue[k];
            g.setColour(display::forScene(static_cast<int>(k)));
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ lane.getCentreX() + v * lane.getWidth() * 0.5f, lane.getCentreY() }));
            g.setColour(display::textFaint());
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
            model.core.editSeasons("Season", [this, rowIndex] { model.core.seasons.remove(rowIndex); });
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
        showMenu(m, this, [this, index](int r) {
            if (r <= 0)
                return;
            engine::Season s;
            if (index >= 0)
                s = model.core.seasons.getSeasons()[static_cast<std::size_t>(index)];
            s.param = static_cast<engine::ParamIndex>(r - 1);
            const int at = index >= 0 ? index : static_cast<int>(model.core.seasons.getSeasons().size());
            bool ok = false;
            model.core.editSeasons("Season", [&] { ok = model.core.seasons.set(at, s); });
            if (! ok)
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
        showMenu(m, this, [this, index, s, periods](int r) mutable {
            if (r == 1)
                return showParamMenu(index);
            if (r == 2)
                return model.core.editSeasons("Season", [this, index] { model.core.seasons.remove(index); });
            if (r >= 100 && r < 400)
                s.depth = static_cast<float>(r - 200) / 100.0f;
            else if (r >= 400 && r < 409)
                s.periodSeconds = static_cast<float>(periods[r - 400]);
            else if (r >= 500 && r < 503)
                s.shape = static_cast<engine::Season::Shape>(r - 500);
            else
                return;
            model.core.editSeasons("Season", [this, index, s] { model.core.seasons.set(index, s); });
        });
    }

    Model& model;
    FlatButton addButton;
    std::size_t shownCount = 0;
    int frames = 0;
};

class ModMeter final : public juce::Component, public Animated
{
public:
    ModMeter(Model& m, engine::ModSource s, bool label = false) : model(m), source(s), labelled(label) { model.add(this); }
    ~ModMeter() override { model.remove(this); }

    void tick() override
    {
        const float v = model.frame().modValue[static_cast<std::size_t>(source)];
        trace[static_cast<std::size_t>(head)] = v;
        head = (head + 1) % kTrace;
        if (isShowing() && std::abs(v - shown) > 0.003f)
        {
            shown = v;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawWell(g, r);
        r = r.reduced(4.0f, 3.0f);
        const bool bipolar = engine::kModSources[static_cast<std::size_t>(source)].bipolar;
        auto yOf = [&](float v) { return bipolar ? r.getCentreY() - v * r.getHeight() * 0.5f : r.getBottom() - v * r.getHeight(); };
        if (bipolar)
        {
            g.setColour(display::wellLine());
            g.drawHorizontalLine(juce::roundToInt(r.getCentreY()), r.getX(), r.getRight());
        }
        juce::Path line;
        for (int k = 0; k < kTrace; ++k)
        {
            const float v = trace[static_cast<std::size_t>((head + k) % kTrace)];
            const float x = r.getX() + r.getWidth() * static_cast<float>(k) / static_cast<float>(kTrace - 1);
            k == 0 ? line.startNewSubPath(x, yOf(v)) : line.lineTo(x, yOf(v));
        }
        g.setColour(display::tide());
        g.strokePath(line, juce::PathStrokeType(1.5f));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ r.getRight(), yOf(shown) }));
        if (labelled)
        {
            g.setFont(caps(9.0f));
            g.setColour(display::textFaint());
            g.drawText(juce::String(engine::kModSources[static_cast<std::size_t>(source)].name).toUpperCase(), r, juce::Justification::topLeft, false);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (model.onHover)
            model.onHover(juce::String(engine::kModSources[static_cast<std::size_t>(source)].name)
                          + ": right-click any knob and choose Modulate with to let this move it");
    }

private:
    static constexpr int kTrace = 90;
    Model& model;
    engine::ModSource source;
    bool labelled = false;
    std::array<float, kTrace> trace {};
    int head = 0;
    float shown = 0.0f;
};

class RouteList final : public juce::Component, public Animated
{
public:
    explicit RouteList(Model& m) : model(m), addButton("Add a route")
    {
        model.add(this);
        addButton.setHelp(&model, "let a source move any control; the depth knob sets how far and which way");
        addButton.onClick = [this] { showSourceMenu(); };
        addAndMakeVisible(addButton);
        rebuild();
    }
    ~RouteList() override { model.remove(this); }

    void tick() override
    {
        if (signature() != shownSignature)
            rebuild();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        addButton.setBounds(r.removeFromBottom(24).removeFromLeft(140));
        r.removeFromBottom(4);
        const int colW = (r.getWidth() - kGap) / 2;
        for (std::size_t k = 0; k < knobs.size(); ++k)
        {
            const int col = static_cast<int>(k) / kRowsPerColumn, row = static_cast<int>(k) % kRowsPerColumn;
            auto cell = juce::Rectangle<int>(r.getX() + col * (colW + kGap), r.getY() + row * kRowH, colW, kRowH);
            knobs[k]->setBounds(cell.removeFromRight(kKnobW));
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedBottom(28.0f);
        drawWell(g, r);
        const auto& routes = model.core.mod.getRoutes();
        if (routes.empty())
        {
            g.setFont(font(11.5f));
            g.setColour(display::textFaint());
            g.drawText("No routes yet. Right-click any knob and choose Modulate with, or add one here.", r.reduced(8.0f),
                       juce::Justification::centred, true);
            return;
        }
        const float colW = (r.getWidth() - static_cast<float>(kGap)) / 2.0f;
        for (std::size_t k = 0; k < routes.size(); ++k)
        {
            const int col = static_cast<int>(k) / kRowsPerColumn, row = static_cast<int>(k) % kRowsPerColumn;
            auto cell = juce::Rectangle<float>(r.getX() + static_cast<float>(col) * (colW + static_cast<float>(kGap)),
                                               r.getY() + static_cast<float>(row * kRowH), colW, static_cast<float>(kRowH))
                            .withTrimmedRight(static_cast<float>(kKnobW));
            const auto& route = routes[k];
            g.setColour(colour::forScene(static_cast<int>(route.source)));
            g.fillRoundedRectangle(cell.removeFromLeft(4.0f).reduced(0.0f, 8.0f).translated(6.0f, 0.0f), 1.0f);
            cell.removeFromLeft(14.0f);
            auto top = cell.removeFromTop(cell.getHeight() * 0.5f);
            g.setFont(font(11.5f, 600));
            g.setColour(display::text());
            g.drawText(engine::kModSources[static_cast<std::size_t>(route.source)].name, top, juce::Justification::bottomLeft, true);
            g.setFont(font(11.0f));
            g.setColour(display::textDim());
            g.drawText(juce::String::fromUTF8("\xe2\x86\x92 ") + model.longName(route.param), cell, juce::Justification::topLeft, true);
            g.setColour(display::textFaint());
            g.drawText("x", juce::Rectangle<float>(cell.getRight() - 14.0f, top.getY(), 14.0f, top.getHeight() * 2.0f), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int k = rowAt(e.getPosition());
        if (k >= 0)
            later(this, [this, k] { model.core.editRoutes("Modulation", [this, k] { model.core.mod.remove(k); }); });
    }

private:
    static constexpr int kRowH = 40, kKnobW = 46, kRowsPerColumn = 8;

    int rowAt(juce::Point<int> p) const
    {
        const auto r = getLocalBounds().withTrimmedBottom(28);
        const int colW = (r.getWidth() - kGap) / 2;
        const int col = (p.x - r.getX()) / (colW + kGap), row = (p.y - r.getY()) / kRowH;
        const int k = col * kRowsPerColumn + row;
        const int xInCol = p.x - r.getX() - col * (colW + kGap);
        if (col < 0 || col > 1 || row < 0 || row >= kRowsPerColumn || k >= static_cast<int>(model.core.mod.getRoutes().size()))
            return -1;
        return xInCol > colW - kKnobW - 18 && xInCol < colW - kKnobW ? k : -1;
    }

    juce::String signature() const
    {
        juce::String sig;
        for (const auto& r : model.core.mod.getRoutes())
            sig << static_cast<int>(r.source) << ":" << static_cast<int>(r.param) << ":" << r.slot << ";";
        return sig;
    }

    void rebuild()
    {
        shownSignature = signature();
        for (auto& k : knobs)
            removeChildComponent(k.get());
        knobs.clear();
        for (const auto& route : model.core.mod.getRoutes())
        {
            auto k = std::make_unique<Knob>(model, static_cast<P>(engine::idx(P::ModRoute1Depth) + route.slot),
                                            "how far " + juce::String(engine::kModSources[static_cast<std::size_t>(route.source)].name)
                                                + " moves " + model.longName(route.param) + "; left of centre moves it the other way");
            k->setLabel("Depth");
            addAndMakeVisible(*k);
            knobs.push_back(std::move(k));
        }
        resized();
        repaint();
    }

    void showSourceMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader("Which source?");
        for (int s = 0; s < engine::kNumModSources; ++s)
            m.addItem(1 + s, engine::kModSources[static_cast<std::size_t>(s)].name);
        showMenu(m, this, [this](int r) { showTargetMenu(static_cast<engine::ModSource>(r - 1)); });
    }

    void showTargetMenu(engine::ModSource source)
    {
        juce::PopupMenu m;
        m.addSectionHeader("What should " + juce::String(engine::kModSources[static_cast<std::size_t>(source)].name) + " move?");
        model.addParamMenus(m, [this](engine::ParamIndex i) {
            const juce::String id(model.registry.spec(i).id);
            return model.core.mod.canModulate(i) && ! id.startsWith("mod.") && ! id.startsWith("terrain.x") && ! id.startsWith("terrain.y");
        }, 1);
        showMenu(m, this, [this, source](int r) { model.modulate(source, static_cast<engine::ParamIndex>(r - 1)); });
    }

    Model& model;
    FlatButton addButton;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::String shownSignature;
};

class RemoteView final : public juce::Component, public Animated
{
public:
    explicit RemoteView(Model& m) : model(m)
    {
        model.add(this);
        mpe.setClickingTogglesState(true);
        mpe.setHelp(&model, "for MPE keyboards: each note bends, presses and brightens on its own channel");
        mpe.onClick = [this] { model.core.midi.setMpe(mpe.getToggleState()); };
        addAndMakeVisible(mpe);
        if (auto* l = model.core.host.getLink(); l != nullptr && LinkSync::isAvailable())
        {
            link.setClickingTogglesState(true);
            link.setHelp(&model, "share tempo and beat with Ableton Live and other Link apps on this network");
            link.onClick = [this, l] {
                l->setEnabled(link.getToggleState());
                model.core.host.getSettings().setValue("link", link.getToggleState());
            };
            addAndMakeVisible(link);
        }
        if (model.core.clockOut != nullptr)
        {
            clock.setTextWhenNothingSelected("Send clock to...");
            clock.onChange = [this] {
                const int id = clock.getSelectedId();
                model.core.clockOut->setDevice(id > 1 && id - 2 < outputs.size() ? outputs[id - 2].identifier : juce::String());
            };
            addAndMakeVisible(clock);
            fillClock();
        }
        if (model.core.osc != nullptr)
        {
            for (auto* e : { &inPort, &outTarget })
            {
                e->setJustification(juce::Justification::centredLeft);
                e->setFont(font(12.0f));
                addAndMakeVisible(*e);
            }
            inPort.setInputRestrictions(5, "0123456789");
            inPort.setText(model.core.osc->getReceivePort() > 0 ? juce::String(model.core.osc->getReceivePort()) : "9000", false);
            outTarget.setText(model.core.osc->getSendTarget(), false);
            oscIn.setClickingTogglesState(true);
            oscOut.setClickingTogglesState(true);
            oscIn.setHelp(&model, "let phones, tablets and other programs play Tidefield over OSC: /tidefield/param/<id>, /tidefield/terrain x y, /tidefield/scene n, /tidefield/fade and more");
            oscOut.setHelp(&model, "send the cursor, levels, beat, scene weights and modulation sources out over OSC, for visuals");
            oscIn.onClick = [this] { model.core.osc->setReceivePort(oscIn.getToggleState() ? inPort.getText().getIntValue() : 0); };
            oscOut.onClick = [this] {
                if (oscOut.getToggleState())
                    model.core.osc->setSendTarget(outTarget.getText());
                else
                    model.core.osc->stopSending();
            };
            addAndMakeVisible(oscIn);
            addAndMakeVisible(oscOut);
        }
        tick();
    }
    ~RemoteView() override { model.remove(this); }

    void tick() override
    {
        mpe.setToggleState(model.core.midi.getMpe(), juce::dontSendNotification);
        if (auto* l = model.core.host.getLink(); l != nullptr && link.isVisible())
        {
            link.setToggleState(l->isEnabled(), juce::dontSendNotification);
            const auto text = l->isEnabled() ? "Ableton Link: " + juce::String(l->numPeers()) + " peers" : juce::String("Ableton Link");
            if (link.getButtonText() != text)
                link.setButtonText(text);
        }
        if (auto* o = model.core.osc.get())
        {
            oscIn.setToggleState(o->isReceiving(), juce::dontSendNotification);
            oscOut.setToggleState(o->isSending(), juce::dontSendNotification);
        }
        if (++frames % 120 == 0 && model.core.clockOut != nullptr && juce::MidiOutput::getAvailableDevices() != outputs)
            fillClock();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        mpe.setBounds(r.removeFromTop(24));
        r.removeFromTop(6);
        if (link.isVisible())
        {
            link.setBounds(r.removeFromTop(24));
            r.removeFromTop(6);
        }
        r.removeFromTop(4);
        if (clock.isVisible())
        {
            r.removeFromTop(14);
            clock.setBounds(r.removeFromTop(24));
            r.removeFromTop(10);
        }
        if (oscIn.isVisible())
        {
            r.removeFromTop(14);
            auto row = r.removeFromTop(24);
            oscIn.setBounds(row.removeFromLeft(120));
            row.removeFromLeft(6);
            inPort.setBounds(row);
            r.removeFromTop(6);
            row = r.removeFromTop(24);
            oscOut.setBounds(row.removeFromLeft(120));
            row.removeFromLeft(6);
            outTarget.setBounds(row);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.setFont(caps());
        g.setColour(colour::textFaint());
        if (clock.isVisible())
            g.drawText("MIDI CLOCK OUT", clock.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
        if (oscIn.isVisible())
            g.drawText("OSC", oscIn.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
    }

private:
    void fillClock()
    {
        outputs = juce::MidiOutput::getAvailableDevices();
        clock.clear(juce::dontSendNotification);
        clock.addItem("No clock out", 1);
        int selected = 1;
        for (int k = 0; k < outputs.size(); ++k)
        {
            clock.addItem(outputs[k].name, k + 2);
            if (outputs[k].identifier == model.core.clockOut->getDeviceId())
                selected = k + 2;
        }
        clock.setSelectedId(selected, juce::dontSendNotification);
    }

    Model& model;
    FlatButton mpe { "MPE keyboard", colour::learn() }, link { "Ableton Link", colour::tide() }, oscIn { "Receive on", colour::tide() }, oscOut { "Send to", colour::tide() };
    juce::ComboBox clock;
    juce::TextEditor inPort, outTarget;
    juce::Array<juce::MidiDeviceInfo> outputs;
    int frames = 0;
};

class MidiView final : public juce::Component, public Animated, private juce::ListBoxModel
{
public:
    explicit MidiView(Model& m) : model(m)
    {
        model.add(this);
        list.setModel(this);
        list.setRowHeight(20);
        list.setColour(juce::ListBox::backgroundColourId, colour::well());
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
            auto b = std::make_unique<FlatButton>(name, colour::learn());
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
        if (++frames % 10 != 0 || ! isShowing())
            return;
        juce::String sig = juce::String(model.core.midi.getBindings().size()) + (model.core.midi.isLearning() ? "L" : "-");
        if (model.core.midiInputs != nullptr)
            for (const auto& d : model.core.midiInputs->getDevices())
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
        g.setColour(colour::textFaint());
        g.drawText(model.core.midi.isLearning() ? "LEARNING: MOVE A CONTROL" : "MAPPINGS", 0, 0, 300, 14, juce::Justification::centredLeft);
        g.drawText("LEARN A PAD FOR", 310, 0, 240, 14, juce::Justification::centredLeft);
        g.drawText("NOTES AND DEVICES", 560, 0, 220, 14, juce::Justification::centredLeft);
    }

    int getNumRows() override { return texts.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected)
            g.fillAll(display::panelHi());
        g.setFont(font(11.5f));
        g.setColour(display::text());
        g.drawText(texts[row], 8, 0, w - 40, h, juce::Justification::centredLeft, true);
        g.setColour(display::textFaint());
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
        if (model.core.midiInputs == nullptr)
            return resized();
        for (const auto& d : model.core.midiInputs->getDevices())
        {
            auto b = std::make_unique<FlatButton>(d.info.name + (d.enabled && ! d.open ? " (unavailable)" : ""), colour::good());
            b->setToggleState(d.enabled, juce::dontSendNotification);
            b->setHelp(&model, "listen to this MIDI device");
            const auto id = d.info.identifier;
            const bool enabled = d.enabled;
            b->onClick = [this, id, enabled] { later(this, [this, id, enabled] { if (model.core.midiInputs != nullptr) model.core.midiInputs->setEnabled(id, ! enabled); }); };
            addAndMakeVisible(*b);
            deviceButtons.push_back(std::move(b));
        }
        resized();
    }

    Model& model;
    juce::ListBox list;
    juce::StringArray texts;
    std::vector<std::unique_ptr<FlatButton>> learnButtons, deviceButtons;
    FlatButton defaults { "Default mapping" }, clearAll { "Clear all" }, notesToDrone { "Notes move the drone", colour::good() };
    juce::ComboBox channel;
    juce::String signature;
    int frames = 9;
};

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
            g.setColour(i == current ? colour::tide() : (cell(i).contains(getMouseXYRelative()) && isMouseOver() ? colour::lift(colour::panelHi(), 0.08f) : colour::panelHi()));
            g.fillRoundedRectangle(r, metric::radius);
            g.setColour(i == current ? colour::well() : colour::text());
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
}

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
    return std::max(width, presetKind.empty() ? 120 : 200) + 2 * (metric::pad - 2);
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
    drawPanel(g, getLocalBounds().toFloat(), title, colour::text());
    g.setColour(tab);
    g.fillRoundedRectangle(juce::Rectangle<float>(3.0f, 6.0f, 3.0f, static_cast<float>(metric::header) - 12.0f), 1.0f);
    g.setFont(font(11.0f, 500));
    g.setColour(colour::textDim());
    for (const auto& it : items)
        if (auto* c = dynamic_cast<Choice*>(it.c); c != nullptr && c->isVisible())
            g.drawText(model.name(c->getParam()), c->getX(), c->getY() - 16, c->getWidth(), 14, juce::Justification::centredLeft);

    if (! presetKind.empty())
    {
        const auto b = presetButton().toFloat();
        g.setColour(presetHover ? colour::lift(colour::panelHi(), 0.1f) : colour::panelHi());
        g.fillRoundedRectangle(b, metric::radius);
        g.setColour(presetHover ? colour::text() : colour::textDim());
        g.setFont(font(10.5f, 600));
        g.drawText(juce::String::fromUTF8("Presets \xe2\x96\xbe"), b, juce::Justification::centred);
    }
}

void Device::setPresets(std::string kind, std::string prefix, std::vector<engine::P> params)
{
    presetKind = std::move(kind);
    presetPrefix = std::move(prefix);
    presetParams = std::move(params);
    repaint();
}

juce::Rectangle<int> Device::presetButton() const
{
    return { getWidth() - 74, 3, 68, metric::header - 6 };
}

void Device::mouseDown(const juce::MouseEvent& e)
{
    if (! presetKind.empty() && presetButton().contains(e.getPosition()))
        showPresetMenu();
}

void Device::mouseMove(const juce::MouseEvent& e)
{
    const bool h = ! presetKind.empty() && presetButton().contains(e.getPosition());
    if (h != presetHover)
    {
        presetHover = h;
        repaint(presetButton());
        if (h && model.onHover)
            model.onHover("Presets: load a starting point for " + title + ", or save what you have");
    }
}

void Device::mouseExit(const juce::MouseEvent&)
{
    if (presetHover)
    {
        presetHover = false;
        repaint(presetButton());
    }
}

void Device::showPresetMenu()
{
    auto& library = model.core.presets;
    const auto list = library.list(presetKind);
    juce::PopupMenu m, del;
    m.addSectionHeader(title + " presets");
    bool anyUser = false;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (! list[i].factory && ! anyUser)
        {
            anyUser = true;
            m.addSeparator();
        }
        m.addItem(static_cast<int>(i) + 1, juce::String(list[i].name));
        if (! list[i].factory)
            del.addItem(10000 + static_cast<int>(i), juce::String(list[i].name));
    }
    if (list.empty())
        m.addItem(-1, "No presets yet", false);
    m.addSeparator();
    m.addItem(9000, "Save as preset...");
    m.addSubMenu("Delete", del, del.getNumItems() > 0);
    m.addItem(9001, "Show presets folder");
    juce::Component::SafePointer<Device> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, list](int r) {
        if (safe == nullptr || r == 0)
            return;
        auto& lib = safe->model.core.presets;
        if (r > 0 && r <= static_cast<int>(list.size()))
        {
            safe->applyPreset(list[static_cast<std::size_t>(r - 1)]);
            safe->model.core.status("Loaded " + juce::String(list[static_cast<std::size_t>(r - 1)].name) + " into " + safe->title);
        }
        else if (r >= 10000 && r - 10000 < static_cast<int>(list.size()))
            lib.remove(list[static_cast<std::size_t>(r - 10000)]);
        else if (r == 9001)
        {
            const auto dir = lib.folderFor(safe->presetKind);
            dir.createDirectory();
            dir.revealToUser();
        }
        else if (r == 9000)
        {
            auto* w = new juce::AlertWindow("Save preset", "A name for this " + safe->title + " preset:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", "My " + safe->title);
            w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int ok) {
                                   if (ok != 1 || safe == nullptr)
                                       return;
                                   const auto name = w->getTextEditorContents("name").trim();
                                   if (name.isEmpty())
                                       return;
                                   juce::String error;
                                   if (safe->model.core.presets.save(safe->capturePreset(name.toStdString()), error))
                                       safe->model.core.status("Saved preset " + name);
                                   else
                                       safe->model.core.status(error, true);
                               }),
                               true);
        }
    });
}

void Device::applyPreset(const io::Preset& p)
{
    for (const auto& [key, value] : p.values)
        if (const auto index = model.registry.find(presetPrefix + key))
            model.set(static_cast<engine::P>(*index), value);
}

io::Preset Device::capturePreset(const std::string& name) const
{
    io::Preset p;
    p.name = name;
    p.kind = presetKind;
    for (auto param : presetParams)
    {
        std::string id = model.spec(param).id;
        if (id.rfind(presetPrefix, 0) == 0)
            id = id.substr(presetPrefix.size());
        p.values[id] = model.value(param);
    }
    return p;
}

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
    static const char* names[] = { "Drone", "Clouds", "Resonator", "Bloom", "Input", "Looper", "Weather", "Gestures", "Cycles", "Seasons", "Modulation", "Timeline", "Mixer", "Effects", "Master", "MIDI" };
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
    g.setColour(colour::window());
    g.fillRect(getLocalBounds().removeFromTop(kTabH));
    for (int i = 0; i < NumPages; ++i)
    {
        const auto r = tabBounds(i).toFloat();
        const bool on = i == page;
        if (on || i == hoverTab)
        {
            g.setColour(on ? colour::panel() : colour::panel().withAlpha(0.45f));
            juce::Path p;
            p.addRoundedRectangle(r.getX(), r.getY() + 3.0f, r.getWidth(), r.getHeight() - 3.0f, metric::radius, metric::radius, true, true, false, false);
            g.fillPath(p);
        }
        if (on)
        {
            g.setColour(colour::accent());
            g.fillRect(r.getX() + 6.0f, r.getY() + 3.0f, r.getWidth() - 12.0f, 2.0f);
        }
        g.setFont(font(12.0f, on ? 600 : 500));
        g.setColour(on ? colour::text() : colour::textDim());
        g.drawText(pageName(i), r.withTrimmedTop(3.0f), juce::Justification::centred);
    }
    g.setColour(colour::panel());
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

    auto device = [&](juce::String title, juce::Colour tab = colour::accent()) -> Device& {
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
        d.addKnob(info.width, {}, "stereo width: 0 is mono, 100% as recorded, 200% wider", 56, 62);
        d.addKnob(info.sendA, "Reverb", "send to the reverb bus", 56, 62);
        d.addKnob(info.sendB, "Delay", "send to the delay bus", 56, 62);
        auto fx = std::make_unique<FlatButton>("Effects");
        fx->setHelp(&model, "open this strip's two insert effects");
        fx->onClick = [this, s] { later(this, [this, s] { showEffectsFor(s); }); };
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
            d.setPresets("drone", "drone.", { P::DroneRoot, P::DroneDensity, P::DroneShape, P::DroneDetune, P::DroneCutoff, P::DroneResonance, P::DroneNoise,
                                              P::DroneEvolve, P::DroneDriftDepth, P::DroneDriftRate, P::DroneSpread, P::DroneGravity });
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
                std::vector<P> cloudParams;
                for (engine::ParamIndex k = 0; k < 12; ++k)
                {
                    d.add(static_cast<P>(first + k));
                    cloudParams.push_back(static_cast<P>(first + k));
                }
                d.setPresets("cloud", "cloud" + std::to_string(c + 1) + ".", cloudParams);
                auto& s = device("Cloud " + juce::String(c + 1) + " strip", tint);
                const auto& info = engine::kStrips[static_cast<std::size_t>(c + 1)];
                s.add(std::make_unique<FaderMeter>(model, info.level, c + 1, "Level"), 64, 0);
                s.addKnob(info.pan, {}, {}, 56, 62);
                s.addKnob(info.width, {}, "stereo width: 0 is mono, 100% as recorded, 200% wider", 56, 62);
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
            d.setPresets("resonator", "res.", { P::ResRoot, P::ResModes, P::ResStructure, P::ResDecay, P::ResBrightness, P::ResSpread, P::ResGravity,
                                                P::ResRain, P::ResRainColour });
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
            d.setPresets("bloom", "bloom.", { P::BloomTransform, P::BloomAmount, P::BloomLength, P::BloomAttack, P::BloomRelease, P::BloomPitch, P::BloomTone,
                                              P::BloomSpread, P::BloomRandom, P::BloomPosition, P::BloomGravity });
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
            auto catchButton = std::make_unique<FlatButton>("Catch now", colour::accent());
            catchButton->setHelp(&model, "grab the last seconds into a cloud (K)");
            catchButton->onClick = [this] { model.engine.command(engine::Command::Catch); };
            c.add(std::move(catchButton), 2 * metric::knobW, 26);
            strip(static_cast<int>(engine::StripId::Input), sceneTint(4));
            break;
        }
        case Looper:
        {
            auto& d = device("Tape looper", sceneTint(5));
            d.add(std::make_unique<LooperView>(model), 260, 0);
            params(d, { P::LoopSource, P::LoopErosion, P::LoopFlakes, P::LoopOverdub });
            d.setPresets("looper", "loop.", { P::LoopErosion, P::LoopFlakes, P::LoopOverdub });
            strip(static_cast<int>(engine::StripId::Loop), sceneTint(5));
            break;
        }
        case Weather:
        {
            auto& d = device("Weather", sceneTint(6));
            params(d, { P::WeatherWind, P::WeatherRain, P::WeatherSurf, P::WeatherGust, P::WeatherTone, P::WeatherDistance });
            d.setPresets("weather", "weather.", { P::WeatherWind, P::WeatherRain, P::WeatherSurf, P::WeatherGust, P::WeatherTone, P::WeatherDistance });
            strip(static_cast<int>(engine::StripId::Weather), sceneTint(6));
            break;
        }
        case Gestures:
        {
            auto& s = device("Swell", colour::accent());
            params(s, { P::SwellDepth, P::SwellAttack, P::SwellRelease });
            auto& h = device("Hush", colour::accent());
            params(h, { P::HushDepth });
            auto& f = device("Freeze all", colour::tide());
            params(f, { P::FreezeOn, P::FreezeDuck, P::FreezeTexture });
            auto& t = device("Terrain", colour::tide());
            params(t, { P::TerrainWanderStyle, P::TerrainGlide, P::TerrainFocus, P::TerrainWander, P::TerrainWanderRate, P::TideRate, P::HarmonyMorph });
            auto& m = device("Medium", colour::live());
            params(m, { P::MediumType, P::MediumAge, P::MediumNoise, P::MediumWobble, P::MediumDrive, P::MediumMix });
            m.setPresets("medium", "medium.", { P::MediumType, P::MediumAge, P::MediumNoise, P::MediumWobble, P::MediumDrive, P::MediumMix });
            strip(static_cast<int>(engine::StripId::Freeze), colour::tide());
            break;
        }
        case Loops:
        {
            auto& d = device("Cycles", sceneTint(7));
            params(d, { P::LoopsOn, P::LoopsTarget, P::LoopsCount, P::LoopsPattern, P::LoopsRate, P::LoopsDensity, P::LoopsRegister, P::LoopsSpread,
                        P::LoopsVelocity });
            d.setPresets("loops", "loops.", { P::LoopsCount, P::LoopsRate, P::LoopsDensity, P::LoopsRegister, P::LoopsSpread, P::LoopsVelocity, P::LoopsPattern });
            auto& t = device("Tempo", colour::tide());
            params(t, { P::SyncOn, P::SyncBpm, P::SyncSource });
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
        case Modulation:
        {
            auto& routes = device("Routes", colour::tide());
            routes.add(std::make_unique<RouteList>(model), 620, 0);
            for (int k = 0; k < engine::kNumLfos; ++k)
            {
                auto& d = device("LFO " + juce::String(k + 1), colour::forScene(k));
                d.setTop(std::make_unique<ModMeter>(model, static_cast<engine::ModSource>(k)), 40, 2 * metric::knobW);
                d.add(static_cast<P>(engine::idx(P::ModLfo1Rate) + 2 * k));
                d.add(static_cast<P>(engine::idx(P::ModLfo1Rate) + 2 * k + 1));
            }
            for (int k = 0; k < engine::kNumRandoms; ++k)
            {
                auto& d = device("Random " + juce::String(k + 1), colour::forScene(4 + k));
                d.setTop(std::make_unique<ModMeter>(model, static_cast<engine::ModSource>(static_cast<int>(engine::ModSource::Random1) + k)), 40,
                         2 * metric::knobW);
                d.add(static_cast<P>(engine::idx(P::ModRandom1Rate) + 2 * k));
                d.add(static_cast<P>(engine::idx(P::ModRandom1Rate) + 2 * k + 1));
            }
            auto& f = device("Followers", colour::forScene(6));
            f.add(std::make_unique<ModMeter>(model, engine::ModSource::InputLevel, true), 2 * metric::knobW, 34);
            f.add(std::make_unique<ModMeter>(model, engine::ModSource::InputBrightness, true), 2 * metric::knobW, 34);
            f.add(std::make_unique<ModMeter>(model, engine::ModSource::MixLevel, true), 2 * metric::knobW, 34);
            params(f, { P::ModFollowAttack, P::ModFollowRelease, P::ModFollowGain });
            break;
        }
        case Timeline:
        {
            auto& d = device("Performance", colour::live());
            d.add(std::make_unique<TimelineView>(model), 1300, 0);
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
                d.addKnob(info.width, {}, "stereo width: 0 is mono, 100% as recorded, 200% wider", 52, 62);
                d.addKnob(info.sendA, "Reverb", {}, 52, 62);
                d.addKnob(info.sendB, "Delay", {}, 52, 62);
            }
            auto& r = device("Returns", colour::tide());
            r.add(std::make_unique<FaderMeter>(model, P::BusALevel, -1, "Reverb"), 58, 0);
            r.add(std::make_unique<FaderMeter>(model, P::BusBLevel, -1, "Delay"), 58, 0);
            break;
        }
        case Effects:
        {
            auto& pick = device("Chain", colour::tide());
            pick.add(std::make_unique<ChainPicker>(fxChain, [this](int i) { later(this, [this, i] { showEffectsFor(i); }); }), 220, 0);
            const int firstSlot = fxChain < engine::kNumStrips ? fxChain * 2 : engine::kBusASlot + (fxChain - engine::kNumStrips) * 2;
            for (int k = 0; k < 2; ++k)
                devices.push_back(std::make_unique<FxDevice>(model, firstSlot + k));
            break;
        }
        case Master:
        {
            auto& d = device("Master", colour::accent());
            d.add(std::make_unique<FaderMeter>(model, P::MasterLevel, -1, "Master"), 64, 0);
            params(d, { P::MasterFadeSecs, P::MasterCeiling });
            auto& a = device("Auto master", colour::good());
            params(a, { P::MasterAuto, P::MasterAutoTarget, P::MasterAutoAmount });
            a.add(std::make_unique<AutoMasterView>(model), 250, 0);
            auto& fx = device("Master effects", colour::tide());
            auto open = std::make_unique<FlatButton>("Master inserts");
            open->onClick = [this] { later(this, [this] { showEffectsFor(engine::kNumStrips + 2); }); };
            fx.add(std::move(open), 2 * metric::knobW, 26);
            auto rev = std::make_unique<FlatButton>("Reverb bus");
            rev->onClick = [this] { later(this, [this] { showEffectsFor(engine::kNumStrips); }); };
            fx.add(std::move(rev), 2 * metric::knobW, 26);
            auto del = std::make_unique<FlatButton>("Delay bus");
            del->onClick = [this] { later(this, [this] { showEffectsFor(engine::kNumStrips + 1); }); };
            fx.add(std::move(del), 2 * metric::knobW, 26);
            break;
        }
        case Midi:
        {
            auto& d = device("MIDI", colour::learn());
            d.add(std::make_unique<MidiView>(model), 800, 0);
            auto& r = device("Sync and remote", colour::tide());
            r.add(std::make_unique<RemoteView>(model), 300, 0);
            break;
        }
        default: break;
    }

    for (auto& d : devices)
        row.addAndMakeVisible(*d);
    layoutRow();
    viewport.setViewPosition(0, 0);
}
}
