#include "Model.h"
#include "Style.h"

#include <dsp/fx/medium/Medium.h>
#include <dsp/harmony/Scale.h>
#include <dsp/sources/bloom/BloomSampler.h>

namespace tf::app::gui {

namespace {
constexpr int kHoldFrames = 12; // ~200 ms: long enough for the engine to echo the value back
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
    // A copy (a component may unregister itself while ticking), into a buffer that
    // keeps its capacity, so a frame allocates nothing.
    ticking.assign(animated.begin(), animated.end());
    for (std::size_t i = 0; i < ticking.size(); ++i) // by index: remove() may null entries as we go
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
        if (weak.expired() || token != jumpToken) // gone, or a later jump owns the restore
            return;
        set(P::TerrainGlide, savedGlide);
        jumpPending = false;
    });
}

float Model::value(P p) const noexcept
{
    const auto i = engine::idx(p);
    // Without telemetry (no audio running) a value set here is the best guess.
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
    else if (id == "master.autoTarget")
        c = { "Quiet -23", "Streaming -16", "Loud -14" };
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
    showMenu(m, owner, [this, p, i](int r) {
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

} // namespace tf::app::gui
