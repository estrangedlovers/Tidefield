#include "Model.h"
#include "Style.h"

#include <dsp/fx/medium/Medium.h>
#include <dsp/harmony/Scale.h>
#include <dsp/sources/bloom/BloomSampler.h>

namespace tf::app::gui {
namespace {
constexpr int kHoldFrames = 12;
}

Model::Model(AppCore& c) : core(c), engine(c.engine), registry(c.engine.getRegistry())
{
    touching.assign(engine::kNumParams, 0);
    local.resize(engine::kNumParams);
    for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
        local[i] = registry.spec(i).defaultValue;
    holdFrames.assign(engine::kNumParams, 0);
    setHere.assign(engine::kNumParams, 0);
}

Model::~Model() { *alive = false; }

void Model::tick()
{
    for (auto& h : holdFrames)
        if (h > 0)
            --h;
    ticking.assign(animated.begin(), animated.end());
    for (std::size_t i = 0; i < ticking.size(); ++i)
        if (auto* a = ticking[i])
            a->tick();
    ticking.clear();
}

void Model::jumpTerrain(engine::Point2 to)
{
    if (! jumpPending)
        savedGlide = value(P::TerrainGlide);
    jumpPending = true;
    const int token = ++jumpToken;
    set(P::TerrainGlide, 0.05f);
    set(P::TerrainX, to.x);
    set(P::TerrainY, to.y);
    juce::Timer::callAfterDelay(150, [this, weak = std::weak_ptr<bool>(alive), token] {
        if (weak.expired() || token != jumpToken)
            return;
        set(P::TerrainGlide, savedGlide);
        jumpPending = false;
    });
}

float Model::value(P p) const noexcept
{
    const auto i = engine::idx(p);
    if (touching[i] != 0 || holdFrames[i] > 0 || (frame().sampleTime == 0 && setHere[i] != 0))
        return local[i];
    return frame().paramTargets[i];
}

bool Model::isLearning(P p) const
{
    const auto lp = core.midi.getLearnParam();
    return core.midi.isLearning() && lp && *lp == engine::idx(p);
}

void Model::set(P p, float v)
{
    const auto i = engine::idx(p);
    const float clamped = spec(p).clamp(v);
    local[i] = clamped;
    setHere[i] = 1;
    holdFrames[i] = kHoldFrames;
    engine.post(engine::ControlEvent::setParam(i, clamped));
}

void Model::release(P p)
{
    holdFrames[engine::idx(p)] = 0;
    engine.post(engine::ControlEvent::releaseParam(engine::idx(p)));
}

juce::String Model::noteName(float midi)
{
    const int n = juce::roundToInt(midi);
    return juce::String(dsp::kNoteNames[static_cast<std::size_t>(((n % 12) + 12) % 12)]) + juce::String(n / 12 - 1);
}

juce::StringArray Model::choices(P p) const
{
    juce::StringArray c;
    const auto& id = spec(p).id;
    if (id == "harmony.root")
        for (const auto* n : dsp::kNoteNames)
            c.add(n);
    else if (id == "harmony.scale")
        for (const auto& s : dsp::kScaleTypes)
            c.add(s.name);
    else if (id == "medium.type")
        for (int t = 0; t < dsp::Medium::kNumTypes; ++t)
            c.add(dsp::Medium::typeName(static_cast<dsp::Medium::Type>(t)));
    else if (id == "bloom.transform")
        for (int t = 0; t < dsp::BloomSampler::kNumTransforms; ++t)
            c.add(dsp::BloomSampler::transformName(static_cast<dsp::BloomSampler::Transform>(t)));
    else if (id == "terrain.wanderStyle")
        c = { "Drift", "Orbit", "Tide pool", "Journey", "Path" };
    else if (id == "catch.source")
        c = { "Output", "Live input" };
    else if (id == "catch.target")
        c = { "Auto", "Cloud 1", "Cloud 2", "Cloud 3", "Cloud 4" };
    else if (id == "input.channel")
        c = { "Input 1", "Input 2", "1 + 2" };
    else if (id == "loop.source")
        c = { "Live input", "The mix" };
    else if (id == "loops.target")
        c = { "Bloom", "Resonator", "Both" };
    else if (id.starts_with("mod.lfo") && id.ends_with(".shape"))
        for (const auto* n : engine::kLfoShapeNames)
            c.add(n);
    else if (id == "sync.source")
        c = { "Internal", "MIDI clock" };
    else if (id == "master.autoTarget")
        c = { "Broadcast -23", "Streaming -16", "Loud -14" };
    else if (spec(p).flags & engine::ParamFlag::kDiscrete && spec(p).minValue == 0.0f && spec(p).maxValue == 1.0f)
        c = { "Off", "On" };
    return c;
}

juce::String Model::format(P p, float v) const
{
    const auto& s = spec(p);
    const auto c = choices(p);
    if (! c.isEmpty())
        return c[juce::jlimit(0, c.size() - 1, juce::roundToInt(v))];
    const juce::String id(s.id);
    if (id.endsWith(".root") || id == "loops.register")
        return noteName(v);
    if (s.flags & engine::ParamFlag::kDiscrete)
        return juce::String(juce::roundToInt(v));

    const juce::String unit(s.unit);
    auto fixed = [](float x, int d) { return juce::String(x, d); };
    if (unit == "Hz")
    {
        if (v >= 1000.0f)
            return fixed(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        if (v < 1.0f)
            return fixed(v, v < 0.1f ? 3 : 2) + " Hz";
        return v < 10.0f ? fixed(v, 1) + " Hz" : juce::String(juce::roundToInt(v)) + " Hz";
    }
    if (unit == "s")
        return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : fixed(v, v < 10.0f ? 2 : 1) + " s";
    if (unit == "ms")
        return v >= 1000.0f ? fixed(v / 1000.0f, 2) + " s" : juce::String(juce::roundToInt(v)) + " ms";
    if (unit == "dB")
        return v <= s.minValue + 0.05f && s.minValue <= -59.0f ? juce::String("Off") : (v > 0.0f ? "+" : "") + fixed(v, 1) + " dB";
    if (unit == "oct")
        return fixed(v, 1) + " oct";
    if (unit == "st")
        return (v > 0.0f ? "+" : "") + fixed(v, 1) + " st";
    if (unit == "ct")
        return juce::String(juce::roundToInt(v)) + " ct";
    if (unit == "BPM")
        return fixed(v, 1) + " BPM";
    if (unit == "x")
        return fixed(v, 2) + "x";
    if (unit == "/s")
        return fixed(v, v < 10.0f ? 1 : 0) + " /s";
    if (s.minValue == -1.0f && s.maxValue == 1.0f)
    {
        if (id.endsWith(".pan"))
            return std::abs(v) < 0.02f ? juce::String("C") : juce::String(v < 0.0f ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(v) * 100.0f));
        return (v > 0.0f ? "+" : "") + juce::String(juce::roundToInt(v * 100.0f)) + "%";
    }
    if (s.minValue == 0.0f && (s.maxValue == 1.0f || s.maxValue == 2.0f))
        return juce::String(juce::roundToInt(v * 100.0f)) + "%";
    return fixed(v, std::abs(s.maxValue - s.minValue) > 50.0f ? 0 : 2);
}

juce::String Model::groupName(engine::ParamIndex i) const
{
    const juce::String id(registry.spec(i).id);
    for (int s = 0; s < engine::kNumFxSlots; ++s)
    {
        const juce::String slotId(engine::kFxSlots[static_cast<std::size_t>(s)].id);
        if (id.startsWith(slotId + "."))
            return engine::kFxSlots[static_cast<std::size_t>(s)].name;
    }
    const auto head = id.upToFirstOccurrenceOf(".", false, false);
    if (head == "mod")
    {
        const auto part = id.fromFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf(".", false, false);
        if (part.startsWith("lfo"))
            return "LFO " + part.substring(3);
        if (part.startsWith("random"))
            return "Random " + part.substring(6);
        if (part.startsWith("route"))
            return "Route " + part.substring(5);
        return "Followers";
    }
    static const std::pair<const char*, const char*> names[] = {
        { "drone", "Drone" }, { "cloud1", "Cloud 1" }, { "cloud2", "Cloud 2" }, { "cloud3", "Cloud 3" }, { "cloud4", "Cloud 4" },
        { "res", "Resonator" }, { "input", "Input" }, { "bloom", "Bloom" }, { "loop", "Looper" }, { "loops", "Cycles" },
        { "weather", "Weather" }, { "freeze", "Freeze all" }, { "master", "Master" }, { "terrain", "Terrain" }, { "tide", "Tide" },
        { "harmony", "Harmony" }, { "medium", "Medium" }, { "catch", "Catch" }, { "swell", "Swell" }, { "hush", "Hush" },
        { "slow", "Slow" }, { "perform", "Shape" }, { "seasons", "Seasons" }, { "sync", "Tempo" }, { "busA", "Reverb return" },
        { "busB", "Delay return" },
    };
    for (const auto& [key, name] : names)
        if (head == key)
            return name;
    return head;
}

juce::String Model::longName(engine::ParamIndex i) const
{
    const juce::String id(registry.spec(i).id);
    juce::String control = registry.spec(i).name;
    for (int s = 0; s < engine::kNumFxSlots; ++s)
    {
        const auto first = engine::idx(engine::kFxSlots[static_cast<std::size_t>(s)].firstParam);
        if (i >= first && i < first + 6)
            if (const auto* info = core.fx.getInfo(s); info != nullptr && info->controls[static_cast<std::size_t>(i - first)].name[0] != 0)
                control = info->controls[static_cast<std::size_t>(i - first)].name;
    }
    return groupName(i) + " " + control;
}

void Model::addParamMenus(juce::PopupMenu& menu, const std::function<bool(engine::ParamIndex)>& include, int idOffset) const
{
    std::vector<std::pair<juce::String, juce::PopupMenu>> groups;
    for (engine::ParamIndex i = 0; i < engine::kNumParams; ++i)
    {
        if (! include(i))
            continue;
        const auto group = groupName(i);
        auto it = std::find_if(groups.begin(), groups.end(), [&group](const auto& g) { return g.first == group; });
        if (it == groups.end())
        {
            groups.emplace_back(group, juce::PopupMenu());
            it = groups.end() - 1;
        }
        const auto full = longName(i);
        it->second.addItem(static_cast<int>(i) + idOffset, full.fromFirstOccurrenceOf(group + " ", false, false).isNotEmpty()
                                                               ? full.fromFirstOccurrenceOf(group + " ", false, false)
                                                               : full);
    }
    for (auto& [name, sub] : groups)
        menu.addSubMenu(name, sub);
}

void Model::modulate(engine::ModSource source, engine::ParamIndex param, float depth)
{
    if (core.mod.add(source, param, depth) < 0)
    {
        core.status(core.mod.freeSlot() < 0 ? "All 16 modulation routes are in use. Remove one on the Modulation tab." : "That control cannot be modulated.",
                    true);
        return;
    }
    core.status(juce::String(engine::kModSources[static_cast<std::size_t>(source)].name) + " now moves " + longName(param)
                + ". Set the depth on the Modulation tab.");
}

void Model::showParamMenu(P p, juce::Component* owner)
{
    const auto i = engine::idx(p);
    const bool learnable = (spec(p).flags & engine::ParamFlag::kMidiLearnable) != 0;
    const bool hasBinding = std::any_of(core.midi.getBindings().begin(), core.midi.getBindings().end(),
                                        [i](const auto& b) { return b.action == engine::MidiAction::None && b.param == i; });
    juce::PopupMenu m;
    m.addSectionHeader(name(p));
    m.addItem(1, isLearning(p) ? "Cancel MIDI learn" : "MIDI learn", learnable);
    m.addItem(2, "Forget MIDI mapping", hasBinding);
    m.addSeparator();
    m.addItem(3, "Release to the terrain", isLive(p));
    m.addItem(4, "Reset to default");
    if (core.mod.canModulate(i))
    {
        juce::PopupMenu sources;
        for (int s = 0; s < engine::kNumModSources; ++s)
            sources.addItem(1000 + s, engine::kModSources[static_cast<std::size_t>(s)].name);
        m.addSeparator();
        m.addSubMenu("Modulate with", sources);
        const auto& routes = core.mod.getRoutes();
        for (std::size_t k = 0; k < routes.size(); ++k)
            if (routes[k].param == i)
                m.addItem(2000 + static_cast<int>(k),
                          "Stop " + juce::String(engine::kModSources[static_cast<std::size_t>(routes[k].source)].name) + " moving this");
    }
    showMenu(m, owner, [this, p, i](int r) {
        if (r >= 1000 && r < 1000 + engine::kNumModSources)
            return modulate(static_cast<engine::ModSource>(r - 1000), i);
        if (r >= 2000 && r < 2000 + engine::kMaxModRoutes)
            return core.mod.remove(r - 2000);
        if (r == 1)
            isLearning(p) ? core.midi.cancelLearn() : core.midi.learnParam(i);
        else if (r == 2)
            core.midi.clearParam(i);
        else if (r == 3)
            release(p);
        else if (r == 4)
            resetToDefault(p);
    });
}
}
