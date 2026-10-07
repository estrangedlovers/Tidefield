#include "UiProtocol.h"

#include <dsp/fx/ProcessorFactory.h>
#include <dsp/fx/medium/Medium.h>
#include <dsp/harmony/Scale.h>
#include <dsp/sources/bloom/BloomSampler.h>
#include <engine/Engine.h>
#include <engine/midi/MidiTypes.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/scene/SceneManager.h>

#include <cmath>

namespace tf::io {

namespace {

/** Rounds for compact JSON (UI precision, not audio precision). */
double q(float v, double scale = 10000.0) { return std::round(static_cast<double>(v) * scale) / scale; }

juce::var arr(std::initializer_list<juce::var> items)
{
    juce::Array<juce::var> a;
    for (const auto& i : items)
        a.add(i);
    return a;
}

const char* taperName(engine::Taper t)
{
    switch (t)
    {
        case engine::Taper::Linear: return "linear";
        case engine::Taper::Log: return "log";
        case engine::Taper::Decibel: return "db";
    }
    return "linear";
}

const char* curveName(dsp::DisplayMap::Curve c)
{
    switch (c)
    {
        case dsp::DisplayMap::Curve::Linear: return "linear";
        case dsp::DisplayMap::Curve::Exp: return "exp";
        case dsp::DisplayMap::Curve::Power: return "power";
        case dsp::DisplayMap::Curve::Choice: return "choice";
        case dsp::DisplayMap::Curve::Hidden: return "hidden";
    }
    return "linear";
}

} // namespace

juce::var buildSchema(const engine::Engine& engine)
{
    using namespace engine;
    const auto& reg = engine.getRegistry();
    auto* root = new juce::DynamicObject();

    juce::Array<juce::var> params;
    for (std::size_t i = 0; i < reg.size(); ++i)
    {
        const auto& s = reg.spec(static_cast<ParamIndex>(i));
        auto* o = new juce::DynamicObject();
        o->setProperty("i", static_cast<int>(i));
        o->setProperty("id", juce::String(s.id));
        o->setProperty("name", juce::String(s.name));
        o->setProperty("min", s.minValue);
        o->setProperty("max", s.maxValue);
        o->setProperty("def", s.defaultValue);
        o->setProperty("taper", taperName(s.taper));
        o->setProperty("unit", juce::String(s.unit));
        o->setProperty("terrain", (s.flags & ParamFlag::kTerrainBound) != 0);
        o->setProperty("midi", (s.flags & ParamFlag::kMidiLearnable) != 0);
        o->setProperty("perform", (s.flags & ParamFlag::kPerformance) != 0);
        o->setProperty("discrete", (s.flags & ParamFlag::kDiscrete) != 0);
        params.add(juce::var(o));
    }
    root->setProperty("params", params);

    juce::Array<juce::var> strips;
    for (int s = 0; s < kNumStrips; ++s)
    {
        const auto& info = kStrips[static_cast<std::size_t>(s)];
        auto* o = new juce::DynamicObject();
        o->setProperty("id", info.id);
        o->setProperty("name", info.name);
        o->setProperty("level", idx(info.level));
        o->setProperty("pan", idx(info.pan));
        o->setProperty("width", idx(info.width));
        o->setProperty("sendA", idx(info.sendA));
        o->setProperty("sendB", idx(info.sendB));
        o->setProperty("fx", arr({ s * 2, s * 2 + 1 }));
        strips.add(juce::var(o));
    }
    root->setProperty("strips", strips);

    juce::Array<juce::var> slots;
    for (int s = 0; s < kNumFxSlots; ++s)
    {
        const auto& info = kFxSlots[static_cast<std::size_t>(s)];
        auto* o = new juce::DynamicObject();
        o->setProperty("id", info.id);
        o->setProperty("name", info.name);
        o->setProperty("first", idx(info.firstParam));
        slots.add(juce::var(o));
    }
    root->setProperty("fxSlots", slots);
    root->setProperty("busASlot", kBusASlot);
    root->setProperty("busBSlot", kBusBSlot);
    root->setProperty("masterSlot", kMasterSlot);

    juce::Array<juce::var> processors;
    for (const auto& e : dsp::ProcessorFactory::instance().entries())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("type", e.info->typeId);
        o->setProperty("name", e.info->name);
        o->setProperty("send", e.info->sendStyle);
        juce::Array<juce::var> controls;
        for (const auto& c : e.info->controls)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty("name", c.name);
            co->setProperty("def", c.defaultValue);
            auto* d = new juce::DynamicObject();
            d->setProperty("curve", curveName(c.display.curve));
            d->setProperty("a", c.display.a);
            d->setProperty("b", c.display.b);
            d->setProperty("unit", c.display.unit);
            d->setProperty("decimals", c.display.decimals);
            juce::Array<juce::var> choices;
            for (int k = 0; k < c.display.numChoices; ++k)
                choices.add(c.display.choices[k]);
            d->setProperty("choices", choices);
            co->setProperty("display", juce::var(d));
            controls.add(juce::var(co));
        }
        o->setProperty("controls", controls);
        processors.add(juce::var(o));
    }
    root->setProperty("processors", processors);

    juce::Array<juce::var> clouds;
    for (int k = 0; k < kNumClouds; ++k)
        clouds.add(idx(kCloudFirstParam[static_cast<std::size_t>(k)]));
    root->setProperty("cloudFirstParam", clouds);

    juce::Array<juce::var> scales, notes, media, transforms;
    for (const auto& st : dsp::kScaleTypes)
        scales.add(st.name);
    for (const auto* n : dsp::kNoteNames)
        notes.add(n);
    for (int m = 0; m < dsp::Medium::kNumTypes; ++m)
        media.add(dsp::Medium::typeName(static_cast<dsp::Medium::Type>(m)));
    for (int t = 0; t < dsp::BloomSampler::kNumTransforms; ++t)
        transforms.add(dsp::BloomSampler::transformName(static_cast<dsp::BloomSampler::Transform>(t)));
    root->setProperty("scales", scales);
    root->setProperty("noteNames", notes);
    root->setProperty("mediumTypes", media);
    root->setProperty("bloomTransforms", transforms);
    root->setProperty("wanderStyles", arr({ "Drift", "Orbit", "Tide pool", "Journey", "Path" }));

    auto* limits = new juce::DynamicObject();
    limits->setProperty("scenes", kMaxScenes);
    limits->setProperty("clouds", kNumClouds);
    limits->setProperty("modes", dsp::ResonatorBank::kMaxModes);
    limits->setProperty("bloomVoices", dsp::BloomSampler::kMaxVoices);
    limits->setProperty("droneVoices", dsp::DroneGenerator::kMaxVoices);
    limits->setProperty("midiPorts", kMaxMidiPorts);
    root->setProperty("limits", juce::var(limits));
    return juce::var(root);
}

juce::var describeScenes(const engine::SceneManager& scenes)
{
    juce::Array<juce::var> list;
    for (const auto& sc : scenes.getScenes())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", juce::String(sc.name));
        o->setProperty("x", q(sc.position.x));
        o->setProperty("y", q(sc.position.y));
        list.add(juce::var(o));
    }
    return list;
}

juce::var describeFx(const engine::FxManager& fx)
{
    juce::Array<juce::var> list;
    for (int s = 0; s < engine::kNumFxSlots; ++s)
        list.add(juce::String(fx.getType(s)));
    return list;
}

juce::var describeSeasons(const engine::SeasonManager& seasons)
{
    juce::Array<juce::var> list;
    for (const auto& s : seasons.getSeasons())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("param", static_cast<int>(s.param));
        o->setProperty("depth", s.depth);
        o->setProperty("period", s.periodSeconds);
        o->setProperty("shape", static_cast<int>(s.shape));
        o->setProperty("phase", s.phase);
        list.add(juce::var(o));
    }
    return list;
}

juce::var describeSamples(const engine::Engine& engine)
{
    juce::Array<juce::var> clouds;
    for (int k = 0; k < engine::kNumClouds; ++k)
    {
        const auto s = engine.getCloudSample(k);
        clouds.add(s != nullptr ? juce::var(juce::String(s->name)) : juce::var());
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("clouds", clouds);
    const auto b = engine.getBloomSample();
    o->setProperty("bloom", b != nullptr ? juce::var(juce::String(b->name)) : juce::var());
    return juce::var(o);
}

juce::var TelemetryEncoder::encode(const engine::TelemetryFrame& f)
{
    using namespace engine;
    auto* o = new juce::DynamicObject();
    o->setProperty("t", static_cast<juce::int64>(f.sampleTime));
    o->setProperty("meter", arr({ q(f.peakL), q(f.peakR), q(f.rmsL), q(f.rmsR) }));
    o->setProperty("limiter", q(f.limiterGain));
    o->setProperty("fade", arr({ q(f.fadeGain), static_cast<int>(f.fadeState) }));
    o->setProperty("panic", f.panicActive);
    o->setProperty("guard", static_cast<int>(f.guardTrips));
    o->setProperty("load", arr({ q(f.dspLoad), f.guardLevel }));
    o->setProperty("tide", q(f.tide));
    o->setProperty("key", arr({ f.harmonyRoot, f.harmonyScale, q(f.harmonyMorph) }));
    o->setProperty("medium", f.mediumType);
    o->setProperty("sustain", f.sustainPedal);

    juce::Array<juce::var> strips;
    for (int s = 0; s < kNumStrips; ++s)
        strips.add(arr({ q(f.stripPeakL[static_cast<std::size_t>(s)]), q(f.stripPeakR[static_cast<std::size_t>(s)]) }));
    o->setProperty("strips", strips);

    juce::Array<juce::var> drone;
    for (std::size_t v = 0; v < f.droneVoiceLevel.size(); ++v)
        drone.add(arr({ q(f.droneVoiceLevel[v], 1000.0), q(f.droneVoiceNote[v], 100.0) }));
    o->setProperty("drone", drone);

    juce::Array<juce::var> clouds;
    for (int k = 0; k < kNumClouds; ++k)
    {
        const auto uk = static_cast<std::size_t>(k);
        auto* c = new juce::DynamicObject();
        c->setProperty("loaded", f.cloudLoaded[uk]);
        c->setProperty("count", f.cloudGrainCount[uk]);
        juce::Array<juce::var> grains;
        for (int g = 0; g < f.cloudGrainViews[uk]; ++g)
        {
            const auto& gv = f.cloudGrains[uk][static_cast<std::size_t>(g)];
            grains.add(arr({ q(gv.position, 1000.0), q(gv.amplitude, 1000.0), q(gv.pan, 100.0) }));
        }
        c->setProperty("grains", grains);
        clouds.add(juce::var(c));
    }
    o->setProperty("clouds", clouds);

    juce::Array<juce::var> modes;
    for (std::size_t m = 0; m < f.modeLevel.size(); ++m)
        modes.add(arr({ q(f.modeLevel[m], 1000.0), q(f.modeNote[m], 100.0) }));
    o->setProperty("modes", modes);

    auto* bloom = new juce::DynamicObject();
    bloom->setProperty("loaded", f.bloomLoaded);
    juce::Array<juce::var> voices;
    for (const auto& v : f.bloomVoices)
        voices.add(arr({ v.active, q(v.note, 100.0), q(v.level, 1000.0) }));
    bloom->setProperty("voices", voices);
    o->setProperty("bloom", juce::var(bloom));
    o->setProperty("input", arr({ q(f.inputLevel, 1000.0), f.inputGateOpen, q(f.inputFreeze) }));

    // Performance layer: swell, seasons, incommensurate loops, looper, weather, freeze.
    auto* perf = new juce::DynamicObject();
    perf->setProperty("swell", q(f.swell));
    perf->setProperty("hush", q(f.hush));
    perf->setProperty("slow", q(f.slow));
    perf->setProperty("freeze", q(f.freezeGain));
    juce::Array<juce::var> seasonValues, loopPhase, loopNote, loopFlash;
    for (std::size_t k = 0; k < f.seasonValue.size(); ++k)
    {
        seasonValues.add(q(f.seasonValue[k]));
        loopPhase.add(q(f.loopPhase[k]));
        loopNote.add(q(f.loopNote[k], 10.0));
        loopFlash.add(q(f.loopFlash[k]));
    }
    perf->setProperty("seasons", seasonValues);
    perf->setProperty("loops", arr({ juce::var(loopPhase), juce::var(loopNote), juce::var(loopFlash) }));
    perf->setProperty("looper", arr({ f.loopState, q(f.loopPosition), q(f.loopSeconds, 100.0), f.loopPasses }));
    perf->setProperty("weather", arr({ q(f.weatherGust), q(f.weatherWave) }));
    o->setProperty("perf", juce::var(perf));
    juce::Array<juce::var> am;
    for (float v : f.autoMaster)
        am.add(q(v, 100.0));
    o->setProperty("auto", am);

    auto* terrain = new juce::DynamicObject();
    terrain->setProperty("cursor", arr({ q(f.cursor.x), q(f.cursor.y) }));
    terrain->setProperty("pos", arr({ q(f.position.x), q(f.position.y) }));
    terrain->setProperty("n", f.numScenes);
    terrain->setProperty("version", static_cast<juce::int64>(f.sceneSetVersion));
    juce::Array<juce::var> weights;
    for (int s = 0; s < f.numScenes; ++s)
        weights.add(q(f.sceneWeights[static_cast<std::size_t>(s)], 1000.0));
    terrain->setProperty("w", weights);
    o->setProperty("terrain", juce::var(terrain));

    // Parameter targets, live layer and MIDI pickup: only what changed.
    if (! primed)
    {
        lastTargets.assign(kNumParams, std::nanf(""));
        lastLive.assign(kNumParams, 255);
        lastPickup.assign(kNumParams, 99);
        primed = true;
        o->setProperty("full", true);
    }
    juce::Array<juce::var> p, live, pickup;
    for (std::size_t i = 0; i < kNumParams; ++i)
    {
        const float v = f.paramTargets[i];
        if (! (std::fabs(v - lastTargets[i]) < 1.0e-6f))
        {
            p.add(arr({ static_cast<int>(i), static_cast<double>(v) }));
            lastTargets[i] = v;
        }
        if (f.live[i] != lastLive[i])
        {
            live.add(arr({ static_cast<int>(i), static_cast<int>(f.live[i]) }));
            lastLive[i] = f.live[i];
        }
        if (f.midiPickup[i] != lastPickup[i])
        {
            pickup.add(arr({ static_cast<int>(i), static_cast<int>(f.midiPickup[i]) }));
            lastPickup[i] = f.midiPickup[i];
        }
    }
    if (! p.isEmpty())
        o->setProperty("p", p);
    if (! live.isEmpty())
        o->setProperty("live", live);
    if (! pickup.isEmpty())
        o->setProperty("pickup", pickup);
    return juce::var(o);
}

} // namespace tf::io
