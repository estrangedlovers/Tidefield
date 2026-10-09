#include "Session.h"

#include "AudioFileIO.h"

#include <algorithm>
#include <cmath>

#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>
#include <engine/guest/GuestManager.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/ModRouteManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/perform/GestureManager.h>
#include <engine/scene/PathManager.h>
#include <engine/scene/SceneManager.h>

namespace tf::io {
namespace {
constexpr const char* kFormatTag = "tidefield-session";
constexpr const char* kJsonEntry = "session.json";

std::vector<std::string> sampleSlotNames()
{
    std::vector<std::string> names;
    for (int k = 0; k < engine::kNumClouds; ++k)
        names.push_back("cloud" + std::to_string(k + 1));
    names.push_back("bloom");
    for (int k = 2; k <= 8; ++k)
        names.push_back("bloom" + std::to_string(k));
    return names;
}

bool migrate(juce::DynamicObject& root, int from, juce::String& error)
{
    for (int v = from; v < SessionData::kCurrentVersion; ++v)
    {
        switch (v)
        {
            default:
                error = "No migration from session version " + juce::String(v);
                return false;
        }
    }
    root.setProperty("version", SessionData::kCurrentVersion);
    return true;
}

juce::var mapToVar(const std::map<std::string, float>& m)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& [k, v] : m)
        obj->setProperty(juce::Identifier(juce::String(k)), v);
    return juce::var(obj);
}

std::map<std::string, float> varToMap(const juce::var& v)
{
    std::map<std::string, float> m;
    if (const auto* obj = v.getDynamicObject())
        for (const auto& prop : obj->getProperties())
            if (const double v = prop.value; std::isfinite(v))
                m[prop.name.toString().toStdString()] = static_cast<float>(v);
    return m;
}
}

juce::var midiToJson(const engine::MidiManager& midi, const engine::ParamRegistry& registry)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("noteChannel", midi.getNoteChannel());
    root->setProperty("notesToDrone", midi.getNotesToDrone());
    root->setProperty("mpe", midi.getMpe());
    juce::Array<juce::var> list;
    for (const auto& b : midi.getBindings())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("source", b.source == engine::MidiBinding::Source::Cc ? "cc" : "note");
        o->setProperty("channel", b.channel);
        o->setProperty("number", b.cc);
        if (b.action != engine::MidiAction::None)
            o->setProperty("action", static_cast<int>(b.action));
        else
            o->setProperty("param", juce::String(registry.spec(b.param).id));
        o->setProperty("low", b.low);
        o->setProperty("high", b.high);
        o->setProperty("curve", b.curve);
        o->setProperty("pickup", b.pickup);
        list.add(juce::var(o));
    }
    root->setProperty("bindings", list);
    return juce::var(root);
}

std::vector<std::string> applyMidiJson(const juce::var& json, engine::MidiManager& midi, const engine::ParamRegistry& registry)
{
    std::vector<std::string> warnings;
    if (json.getDynamicObject() == nullptr)
        return warnings;
    std::vector<engine::MidiBinding> list;
    if (const auto* arr = json.getProperty("bindings", juce::var()).getArray())
        for (const auto& v : *arr)
        {
            engine::MidiBinding b;
            b.source = v.getProperty("source", "cc").toString() == "note" ? engine::MidiBinding::Source::Note : engine::MidiBinding::Source::Cc;
            b.channel = static_cast<int>(v.getProperty("channel", -1));
            b.cc = static_cast<int>(v.getProperty("number", 0));
            b.low = static_cast<float>(static_cast<double>(v.getProperty("low", 0.0)));
            b.high = static_cast<float>(static_cast<double>(v.getProperty("high", 1.0)));
            b.curve = static_cast<float>(static_cast<double>(v.getProperty("curve", 0.0)));
            b.pickup = static_cast<bool>(v.getProperty("pickup", true));
            if (v.hasProperty("action"))
            {
                b.action = static_cast<engine::MidiAction>(std::clamp(static_cast<int>(v["action"]), 0, static_cast<int>(engine::MidiAction::InputFreezeToggle)));
            }
            else
            {
                const auto id = v.getProperty("param", "").toString().toStdString();
                const auto index = registry.find(id);
                if (! index)
                {
                    warnings.push_back("MIDI binding to unknown parameter '" + id + "'");
                    continue;
                }
                b.param = *index;
            }
            list.push_back(b);
        }
    midi.setNoteChannel(static_cast<int>(json.getProperty("noteChannel", -1)));
    midi.setNotesToDrone(static_cast<bool>(json.getProperty("notesToDrone", false)));
    midi.setMpe(static_cast<bool>(json.getProperty("mpe", false)));
    midi.setBindings(std::move(list));
    return warnings;
}

juce::var seasonsToJson(const engine::SeasonManager& seasons, const engine::ParamRegistry& reg)
{
    juce::Array<juce::var> list;
    for (const auto& s : seasons.getSeasons())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("param", juce::String(reg.spec(s.param).id));
        o->setProperty("depth", s.depth);
        o->setProperty("period", s.periodSeconds);
        o->setProperty("shape", static_cast<int>(s.shape));
        o->setProperty("phase", s.phase);
        list.add(juce::var(o));
    }
    return list;
}

std::vector<std::string> applySeasonsJson(const juce::var& json, engine::SeasonManager& seasons, const engine::ParamRegistry& reg)
{
    std::vector<std::string> warnings;
    std::vector<engine::Season> list;
    if (const auto* arr = json.getArray())
        for (const auto& v : *arr)
        {
            const auto id = v.getProperty("param", "").toString().toStdString();
            const auto index = reg.find(id);
            if (! index)
            {
                warnings.push_back("Season on unknown parameter '" + id + "'");
                continue;
            }
            engine::Season s;
            s.param = *index;
            s.depth = static_cast<float>(static_cast<double>(v.getProperty("depth", 0.2)));
            s.periodSeconds = static_cast<float>(static_cast<double>(v.getProperty("period", 300.0)));
            s.shape = static_cast<engine::Season::Shape>(std::clamp(static_cast<int>(v.getProperty("shape", 0)), 0, 2));
            s.phase = static_cast<float>(static_cast<double>(v.getProperty("phase", 0.0)));
            list.push_back(s);
        }
    seasons.replaceAll(std::move(list));
    return warnings;
}

juce::var gestureToJson(const engine::GestureTake& take, const engine::ParamRegistry& reg)
{
    using T = engine::ControlEvent::Type;
    auto* o = new juce::DynamicObject();
    const double fs = take.sampleRate > 0.0 ? take.sampleRate : 48000.0;
    o->setProperty("seconds", static_cast<double>(take.length) / fs);
    o->setProperty("loop", take.loop);
    juce::Array<juce::var> events;
    for (const auto& g : take.events)
    {
        const auto& e = g.event;
        juce::Array<juce::var> row;
        row.add(std::round(static_cast<double>(g.time) / fs * 100000.0) / 100000.0);
        if (e.type == T::SetParam || e.type == T::ReleaseParam)
        {
            if (e.param >= reg.size())
                continue;
            row.add(e.type == T::SetParam ? "set" : "release");
            row.add(juce::String(reg.spec(e.param).id));
            row.add(e.value);
        }
        else if (e.type == T::Note)
        {
            row.add("note");
            row.add(static_cast<int>(e.param));
            row.add(e.value);
        }
        else if (e.type == T::Command)
        {
            const char* name = e.command == engine::Command::Catch ? "catch" : e.command == engine::Command::LoopRecord ? "loopRecord"
                               : e.command == engine::Command::LoopClear                                    ? "loopClear"
                                                                                                             : nullptr;
            if (name == nullptr)
                continue;
            row.add(name);
            row.add(0);
            row.add(0);
        }
        else
            continue;
        events.add(row);
    }
    o->setProperty("events", events);
    return juce::var(o);
}

std::vector<std::string> applyGestureJson(const juce::var& json, engine::GestureManager& gestures, const engine::ParamRegistry& reg)
{
    std::vector<std::string> warnings;
    engine::GestureTake take;
    if (! json.isObject())
    {
        gestures.setTake(take);
        return warnings;
    }
    take.sampleRate = 48000.0;
    auto seconds = [](const juce::var& v) {
        const double s = v;
        return std::isfinite(s) ? std::clamp(s, 0.0, 86400.0) : 0.0;
    };
    take.length = static_cast<std::uint64_t>(seconds(json.getProperty("seconds", 0.0)) * take.sampleRate);
    take.loop = static_cast<bool>(json.getProperty("loop", true));
    int unknown = 0;
    if (const auto* events = json.getProperty("events", {}).getArray())
        for (const auto& row : *events)
        {
            const auto* r = row.getArray();
            if (r == nullptr || r->size() < 4 || take.events.size() >= 200000)
                continue;
            engine::GestureEvent g;
            g.time = static_cast<std::uint64_t>(seconds((*r)[0]) * take.sampleRate);
            const auto kind = (*r)[1].toString();
            const double raw = (*r)[3];
            const float value = std::isfinite(raw) ? static_cast<float>(raw) : 0.0f;
            if (kind == "set" || kind == "release")
            {
                const auto index = reg.find((*r)[2].toString().toStdString());
                if (! index)
                {
                    ++unknown;
                    continue;
                }
                g.event = kind == "set" ? engine::ControlEvent::setParam(*index, value) : engine::ControlEvent::releaseParam(*index);
            }
            else if (kind == "note")
                g.event = engine::ControlEvent::note(std::clamp(static_cast<int>((*r)[2]), 0, 127), std::clamp(value, 0.0f, 1.0f));
            else if (kind == "catch")
                g.event = engine::ControlEvent::makeCommand(engine::Command::Catch);
            else if (kind == "loopRecord")
                g.event = engine::ControlEvent::makeCommand(engine::Command::LoopRecord);
            else if (kind == "loopClear")
                g.event = engine::ControlEvent::makeCommand(engine::Command::LoopClear);
            else
                continue;
            take.events.push_back(g);
        }
    std::stable_sort(take.events.begin(), take.events.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    if (unknown > 0)
        warnings.push_back("Gesture: " + std::to_string(unknown) + " moves on parameters this version does not have were skipped");
    gestures.setTake(std::move(take));
    return warnings;
}

juce::var modRoutesToJson(const engine::ModRouteManager& mod, const engine::ParamRegistry& reg)
{
    juce::Array<juce::var> out;
    for (const auto& r : mod.getRoutes())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("source", juce::String(engine::kModSources[static_cast<std::size_t>(r.source)].id));
        o->setProperty("param", juce::String(reg.spec(r.param).id));
        o->setProperty("slot", r.slot);
        out.add(juce::var(o));
    }
    return out;
}

juce::var macrosToJson(const engine::ModRouteManager& mod, const engine::ParamRegistry& reg)
{
    juce::Array<juce::var> out;
    for (const auto& m : mod.getMacros())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", juce::String(m.name));
        juce::Array<juce::var> targets;
        for (const auto& t : m.targets)
        {
            auto* to = new juce::DynamicObject();
            to->setProperty("param", juce::String(reg.spec(t.param).id));
            to->setProperty("from", t.from);
            to->setProperty("to", t.to);
            targets.add(juce::var(to));
        }
        o->setProperty("targets", targets);
        out.add(juce::var(o));
    }
    return out;
}

std::vector<std::string> applyMacrosJson(const juce::var& json, engine::ModRouteManager& mod, const engine::ParamRegistry& reg)
{
    std::vector<std::string> warnings;
    std::array<engine::ModRouteManager::Macro, engine::kNumMacros> all;
    if (const auto* arr = json.getArray())
        for (int m = 0; m < std::min(arr->size(), engine::kNumMacros); ++m)
        {
            const auto& v = (*arr)[m];
            all[static_cast<std::size_t>(m)].name = v.getProperty("name", "").toString().substring(0, 40).toStdString();
            if (const auto* targets = v.getProperty("targets", {}).getArray())
                for (const auto& t : *targets)
                {
                    const auto id = t.getProperty("param", "").toString().toStdString();
                    const auto param = reg.find(id);
                    if (! param.has_value())
                    {
                        warnings.push_back("A macro target '" + id + "' was skipped: this version does not have it");
                        continue;
                    }
                    auto number = [&t](const char* key, float fallback) {
                        const double d = t.getProperty(key, fallback);
                        return std::isfinite(d) ? static_cast<float>(d) : fallback;
                    };
                    all[static_cast<std::size_t>(m)].targets.push_back({ *param, number("from", 0.0f), number("to", 0.5f) });
                }
        }
    mod.replaceMacros(all);
    return warnings;
}

std::vector<std::string> applyModRoutesJson(const juce::var& json, engine::ModRouteManager& mod, const engine::ParamRegistry& reg)
{
    std::vector<std::string> warnings;
    std::vector<engine::ModRouteManager::Route> list;
    if (const auto* arr = json.getArray())
        for (const auto& v : *arr)
        {
            const auto sourceId = v.getProperty("source", "").toString().toStdString();
            const auto paramId = v.getProperty("param", "").toString().toStdString();
            const int source = engine::modSourceFromId(sourceId);
            const auto param = reg.find(paramId);
            if (source < 0 || ! param.has_value())
            {
                warnings.push_back("Modulation from '" + sourceId + "' to '" + paramId + "' was skipped: this version does not have it");
                continue;
            }
            engine::ModRouteManager::Route r;
            r.source = static_cast<engine::ModSource>(source);
            r.param = *param;
            r.slot = static_cast<int>(v.getProperty("slot", -1));
            list.push_back(r);
        }
    mod.replaceAll(list);
    return warnings;
}

SessionData captureSession(const engine::Engine& engine, const engine::TelemetryFrame& latest, const engine::SceneManager& scenes,
                           const engine::FxManager& fx, const engine::MidiManager* midi, const engine::SeasonManager* seasons,
                           const engine::PathManager* path, const engine::GestureManager* gestures, const engine::ModRouteManager* mod,
                           const engine::GuestManager* guest)
{
    const auto& reg = engine.getRegistry();
    SessionData s;
    for (std::size_t i = 0; i < reg.size(); ++i)
        s.params[reg.spec(static_cast<engine::ParamIndex>(i)).id] = latest.paramTargets[i];

    for (const auto& sc : scenes.getScenes())
    {
        SessionData::SceneData d;
        d.name = sc.name;
        d.x = sc.position.x;
        d.y = sc.position.y;
        for (const auto& [param, value] : sc.values)
            d.values[reg.spec(param).id] = value;
        s.scenes.push_back(std::move(d));
    }
    for (auto p : scenes.getPins())
        s.pins.push_back(reg.spec(p).id);

    for (int slot = 0; slot < engine::kNumFxSlots; ++slot)
    {
        s.fx[engine::kFxSlots[static_cast<std::size_t>(slot)].id] = fx.getType(slot);
        if (fx.isExternal(slot))
            s.fxState[engine::kFxSlots[static_cast<std::size_t>(slot)].id] = fx.getState(slot);
    }
    if (guest != nullptr && ! guest->isEmpty())
        s.guest = { guest->getType(), guest->getName(), guest->getState() };

    for (int k = 0; k < engine::kNumClouds; ++k)
        if (auto b = engine.getCloudSample(k))
            s.samples["cloud" + std::to_string(k + 1)] = b;
    const auto zones = engine.getBloomZones();
    for (std::size_t k = 0; k < zones.size(); ++k)
    {
        s.samples[k == 0 ? std::string("bloom") : "bloom" + std::to_string(k + 1)] = zones[k].buffer;
        s.bloomRoots.push_back(zones[k].root);
    }
    if (midi != nullptr)
        s.midi = midiToJson(*midi, reg);
    if (seasons != nullptr)
        s.seasons = seasonsToJson(*seasons, reg);
    if (path != nullptr)
        s.path = path->getStroke();
    if (gestures != nullptr && gestures->hasTake())
        s.gesture = gestureToJson(gestures->getTake(), reg);
    if (mod != nullptr)
    {
        s.modRoutes = modRoutesToJson(*mod, reg);
        s.macros = macrosToJson(*mod, reg);
    }
    return s;
}

SessionData defaultSession(const engine::Engine& engine)
{
    const auto& reg = engine.getRegistry();
    SessionData s;
    s.name = "Untitled";
    for (const auto& spec : reg.all())
        s.params[spec.id] = spec.defaultValue;
    for (int slot = 0; slot < engine::kNumFxSlots; ++slot)
        s.fx[engine::kFxSlots[static_cast<std::size_t>(slot)].id] = "";
    s.fx[engine::kFxSlots[static_cast<std::size_t>(engine::kBusASlot)].id] = "tf.reverb";
    s.fx[engine::kFxSlots[static_cast<std::size_t>(engine::kBusBSlot)].id] = "tf.delay";
    return s;
}

std::vector<std::string> applySession(const SessionData& session, engine::Engine& engine, engine::SceneManager& scenes,
                                      engine::FxManager& fx, bool snap, engine::MidiManager* midi,
                                      engine::SeasonManager* seasons, engine::PathManager* path,
                                      engine::GestureManager* gestures, engine::ModRouteManager* mod, engine::GuestManager* guest)
{
    const auto& reg = engine.getRegistry();
    std::vector<std::string> warnings = session.warnings;

    if (guest != nullptr)
    {
        const auto& g = session.guest;
        if (g.type.empty())
        {
            if (! guest->isEmpty())
                guest->clear();
        }
        else if (g.type != guest->getType() || g.state != guest->getState())
            guest->setType(g.type, false, g.state, g.name);
        if (! g.type.empty() && guest->isMissing())
            warnings.push_back("The instrument plugin " + (g.name.empty() ? std::string("in the Guest strip") : "'" + g.name + "'")
                               + " is not available here; the Guest strip stays silent and keeps its settings for when it is");
    }

    for (int slot = 0; slot < engine::kNumFxSlots; ++slot)
        if (session.fx.find(engine::kFxSlots[static_cast<std::size_t>(slot)].id) == session.fx.end())
            fx.setType(slot, "", false);
    for (const auto& [slotId, type] : session.fx)
    {
        const int slot = engine::FxManager::findSlot(slotId);
        if (slot < 0)
            warnings.push_back("Unknown FX slot '" + slotId + "'");
        else if (! type.empty() && ! fx.knows(type))
        {
            warnings.push_back(juce::String(type).startsWith("plugin:")
                                   ? "The plugin in " + slotId + " is not available here (left empty)"
                                   : "Unknown processor '" + type + "' in " + slotId + " (left empty)");
            fx.setType(slot, "", false);
        }
        else
        {
            const auto state = session.fxState.find(slotId);
            fx.setType(slot, type, false, state != session.fxState.end() ? state->second : std::string());
        }
    }

    engine.command(engine::Command::ReleaseLiveLayer);
    for (const auto& spec : reg.all())
        if (session.params.find(spec.id) == session.params.end())
        {
            const auto index = reg.find(spec.id);
            engine.post(snap ? engine::ControlEvent::snapParam(*index, spec.defaultValue, engine::ControlSource::UI)
                             : engine::ControlEvent::setParam(*index, spec.defaultValue, engine::ControlSource::Terrain));
        }
    for (const auto& [id, value] : session.params)
    {
        const auto index = reg.find(id);
        if (! index)
        {
            warnings.push_back("Unknown parameter '" + id + "'");
            continue;
        }
        engine.post(snap ? engine::ControlEvent::snapParam(*index, value, engine::ControlSource::UI)
                         : engine::ControlEvent::setParam(*index, value, engine::ControlSource::Terrain));
    }

    std::vector<engine::Scene> newScenes;
    for (const auto& d : session.scenes)
    {
        engine::Scene sc;
        sc.name = d.name;
        sc.position = { d.x, d.y };
        for (const auto& [id, value] : d.values)
        {
            if (const auto index = reg.find(id))
                sc.values[*index] = value;
            else
                warnings.push_back("Scene '" + d.name + "': unknown parameter '" + id + "'");
        }
        newScenes.push_back(std::move(sc));
    }
    std::vector<engine::ParamIndex> pins;
    for (const auto& id : session.pins)
        if (const auto index = reg.find(id))
            pins.push_back(*index);
    scenes.replaceAll(std::move(newScenes), pins);

    for (int k = 0; k < engine::kNumClouds; ++k)
    {
        const auto it = session.samples.find("cloud" + std::to_string(k + 1));
        if (! engine.loadCloudSample(k, it != session.samples.end() ? it->second : nullptr))
            warnings.push_back("Cloud " + std::to_string(k + 1) + ": the sound could not be loaded yet (audio is not running); load it again once it is");
    }
    std::vector<engine::Engine::BloomZone> zones;
    for (int k = 0; k < 8; ++k)
    {
        const auto it = session.samples.find(k == 0 ? std::string("bloom") : "bloom" + std::to_string(k + 1));
        if (it == session.samples.end() || it->second == nullptr)
            continue;
        zones.push_back({ it->second, static_cast<std::size_t>(k) < session.bloomRoots.size() ? session.bloomRoots[static_cast<std::size_t>(k)] : -1.0f });
    }
    if (! engine.loadBloomZones(zones))
        warnings.push_back("Bloom: the sound could not be loaded yet (audio is not running); load it again once it is");

    if (seasons != nullptr)
        for (auto& w : applySeasonsJson(session.seasons, *seasons, reg))
            warnings.push_back(std::move(w));
    if (midi != nullptr)
        for (auto& w : applyMidiJson(session.midi, *midi, reg))
            warnings.push_back(std::move(w));
    if (path != nullptr)
        path->set(session.path);
    if (gestures != nullptr)
        for (auto& w : applyGestureJson(session.gesture, *gestures, reg))
            warnings.push_back(std::move(w));
    if (mod != nullptr)
    {
        for (auto& w : applyModRoutesJson(session.modRoutes, *mod, reg))
            warnings.push_back(std::move(w));
        for (auto& w : applyMacrosJson(session.macros, *mod, reg))
            warnings.push_back(std::move(w));
    }
    return warnings;
}

bool sameContent(const SessionData& a, const SessionData& b)
{
    constexpr float kTolerance = 1.0e-5f;
    constexpr const char* kGuestKey = "guest";
    if (a.params.size() != b.params.size())
        return false;
    for (const auto& [id, value] : a.params)
    {
        const auto it = b.params.find(id);
        if (it == b.params.end() || std::abs(it->second - value) > kTolerance * std::max(1.0f, std::abs(value)))
            return false;
    }
    if (a.samples.size() != b.samples.size())
        return false;
    for (const auto& [id, buffer] : a.samples)
        if (const auto it = b.samples.find(id); it == b.samples.end() || it->second != buffer)
            return false;
    const auto tracked = [&](const std::string& key) { return a.pluginEdits.count(key) != 0 && b.pluginEdits.count(key) != 0; };
    for (const auto& [key, revision] : a.pluginEdits)
        if (tracked(key) && b.pluginEdits.at(key) != revision)
            return false;
    const auto structure = [&](const SessionData& s) {
        SessionData copy = s;
        copy.name.clear();
        copy.params.clear();
        copy.samples.clear();
        copy.performance = juce::var();
        copy.warnings.clear();
        for (auto& [slot, state] : copy.fxState)
            if (tracked(slot))
                state.clear();
        if (tracked(kGuestKey))
            copy.guest.state.clear();
        return juce::JSON::toString(sessionToJson(copy), true);
    };
    return structure(a) == structure(b);
}

juce::var sessionToJson(const SessionData& s)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", kFormatTag);
    root->setProperty("version", SessionData::kCurrentVersion);
    root->setProperty("name", juce::String(s.name));
    root->setProperty("params", mapToVar(s.params));

    juce::Array<juce::var> scenes;
    for (const auto& sc : s.scenes)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", juce::String(sc.name));
        o->setProperty("x", sc.x);
        o->setProperty("y", sc.y);
        o->setProperty("values", mapToVar(sc.values));
        scenes.add(juce::var(o));
    }
    root->setProperty("scenes", scenes);

    juce::Array<juce::var> pins;
    for (const auto& p : s.pins)
        pins.add(juce::String(p));
    root->setProperty("pins", pins);

    auto* fx = new juce::DynamicObject();
    for (const auto& [slot, type] : s.fx)
        fx->setProperty(juce::Identifier(juce::String(slot)), juce::String(type));
    root->setProperty("fx", juce::var(fx));
    if (! s.fxState.empty())
    {
        auto* states = new juce::DynamicObject();
        for (const auto& [slot, state] : s.fxState)
            states->setProperty(juce::Identifier(juce::String(slot)), juce::String(state));
        root->setProperty("fxState", juce::var(states));
    }

    if (! s.guest.type.empty())
    {
        auto* g = new juce::DynamicObject();
        g->setProperty("type", juce::String(s.guest.type));
        g->setProperty("name", juce::String(s.guest.name));
        g->setProperty("state", juce::String(s.guest.state));
        root->setProperty("guest", juce::var(g));
    }

    root->setProperty("midi", s.midi);
    if (s.seasons.isArray())
        root->setProperty("seasons", s.seasons);
    if (s.modRoutes.isArray())
        root->setProperty("modRoutes", s.modRoutes);
    if (s.macros.isArray())
        root->setProperty("macros", s.macros);
    if (s.performance.isObject())
        root->setProperty("performance", s.performance);
    if (s.bloomRoots.size() > 1 || (s.bloomRoots.size() == 1 && s.bloomRoots[0] >= 0.0f))
    {
        juce::Array<juce::var> roots;
        for (float r : s.bloomRoots)
            roots.add(r);
        root->setProperty("bloomRoots", roots);
    }
    if (! s.path.empty())
    {
        juce::Array<juce::var> pts;
        for (const auto& p : s.path)
        {
            pts.add(std::round(p.x * 10000.0f) / 10000.0f);
            pts.add(std::round(p.y * 10000.0f) / 10000.0f);
        }
        root->setProperty("path", pts);
    }
    if (s.gesture.isObject())
        root->setProperty("gesture", s.gesture);

    auto* samples = new juce::DynamicObject();
    for (const auto& [slot, buffer] : s.samples)
    {
        if (buffer == nullptr)
            continue;
        auto* o = new juce::DynamicObject();
        o->setProperty("file", "audio/" + juce::String(slot) + ".flac");
        o->setProperty("name", juce::String(buffer->name));
        o->setProperty("seconds", buffer->seconds());
        samples->setProperty(juce::Identifier(juce::String(slot)), juce::var(o));
    }
    root->setProperty("samples", juce::var(samples));
    return juce::var(root);
}

std::optional<SessionData> sessionFromJson(const juce::var& json, juce::String& error)
{
    auto* root = json.getDynamicObject();
    if (root == nullptr || root->getProperty("format").toString() != kFormatTag)
    {
        error = "Not a Tidefield session";
        return std::nullopt;
    }
    const int version = static_cast<int>(root->getProperty("version"));
    if (version > SessionData::kCurrentVersion)
    {
        error = "This session was saved by a newer Tidefield (format " + juce::String(version) + ")";
        return std::nullopt;
    }
    if (version < SessionData::kCurrentVersion && ! migrate(*root, version, error))
        return std::nullopt;

    SessionData s;
    s.version = SessionData::kCurrentVersion;
    s.name = root->getProperty("name").toString().toStdString();
    s.params = varToMap(root->getProperty("params"));
    if (const auto* scenes = root->getProperty("scenes").getArray())
        for (const auto& v : *scenes)
        {
            SessionData::SceneData d;
            d.name = v.getProperty("name", "").toString().toStdString();
            d.x = static_cast<float>(static_cast<double>(v.getProperty("x", 0.5)));
            d.y = static_cast<float>(static_cast<double>(v.getProperty("y", 0.5)));
            d.values = varToMap(v.getProperty("values", juce::var()));
            s.scenes.push_back(std::move(d));
        }
    if (const auto* pins = root->getProperty("pins").getArray())
        for (const auto& p : *pins)
            s.pins.push_back(p.toString().toStdString());
    if (const auto* fx = root->getProperty("fx").getDynamicObject())
        for (const auto& prop : fx->getProperties())
            s.fx[prop.name.toString().toStdString()] = prop.value.toString().toStdString();
    if (const auto* states = root->getProperty("fxState").getDynamicObject())
        for (const auto& prop : states->getProperties())
            s.fxState[prop.name.toString().toStdString()] = prop.value.toString().toStdString();
    if (const auto* g = root->getProperty("guest").getDynamicObject())
        s.guest = { g->getProperty("type").toString().toStdString(), g->getProperty("name").toString().toStdString(),
                    g->getProperty("state").toString().toStdString() };
    s.midi = root->getProperty("midi");
    s.seasons = root->getProperty("seasons");
    s.modRoutes = root->getProperty("modRoutes");
    s.macros = root->getProperty("macros");
    s.performance = root->getProperty("performance");
    if (const auto* roots = root->getProperty("bloomRoots").getArray())
        for (const auto& r : *roots)
            s.bloomRoots.push_back(std::clamp(static_cast<float>(static_cast<double>(r)), -1.0f, 127.0f));
    s.gesture = root->getProperty("gesture");
    if (const auto* pts = root->getProperty("path").getArray())
        for (int i = 0; i + 1 < pts->size(); i += 2)
            s.path.push_back({ static_cast<float>(static_cast<double>((*pts)[i])), static_cast<float>(static_cast<double>((*pts)[i + 1])) });
    return s;
}

bool writeSession(const SessionData& s, juce::OutputStream& out, juce::String& error)
{
    juce::ZipFile::Builder zip;
    const auto now = juce::Time::getCurrentTime();
    const auto json = juce::JSON::toString(sessionToJson(s), false);
    zip.addEntry(std::make_unique<juce::MemoryInputStream>(json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 6, kJsonEntry, now);

    for (const auto& [slot, buffer] : s.samples)
    {
        if (buffer == nullptr)
            continue;
        juce::MemoryBlock flac;
        if (! encodeFlac(*buffer, flac, error))
            return false;
        zip.addEntry(std::make_unique<juce::MemoryInputStream>(std::move(flac)), 0, "audio/" + juce::String(slot) + ".flac", now);
    }
    if (! zip.writeToStream(out, nullptr))
    {
        error = "Writing the session failed";
        return false;
    }
    return true;
}

bool saveSession(const SessionData& s, const juce::File& file, juce::String& error)
{
    const auto temp = file.getSiblingFile(file.getFileName() + ".saving");
    {
        juce::FileOutputStream out(temp);
        if (out.failedToOpen())
        {
            error = "Could not write " + temp.getFullPathName();
            return false;
        }
        out.setPosition(0);
        out.truncate();
        bool ok = writeSession(s, out, error);
        out.flush();
        if (ok && out.getStatus().failed())
        {
            error = "Writing " + file.getFileName() + " failed: " + out.getStatus().getErrorMessage();
            ok = false;
        }
        if (! ok)
        {
            temp.deleteFile();
            return false;
        }
    }
    if (! temp.replaceFileIn(file))
    {
        error = "Could not replace " + file.getFullPathName() + "; the new version was saved as " + temp.getFileName();
        return false;
    }
    return true;
}

namespace {
std::optional<SessionData> loadFromZip(juce::ZipFile& zip, const juce::String& what, juce::String& error)
{
    const auto* entry = zip.getEntry(kJsonEntry);
    if (entry == nullptr)
    {
        error = what + " is not a Tidefield session";
        return std::nullopt;
    }
    std::unique_ptr<juce::InputStream> jsonStream(zip.createStreamForEntry(*entry));
    if (jsonStream == nullptr || entry->uncompressedSize > 64 * 1024 * 1024)
    {
        error = what + " is damaged";
        return std::nullopt;
    }
    juce::var json;
    const auto parsed = juce::JSON::parse(jsonStream->readEntireStreamAsString(), json);
    if (parsed.failed())
    {
        error = "Session JSON is damaged: " + parsed.getErrorMessage();
        return std::nullopt;
    }
    auto session = sessionFromJson(json, error);
    if (! session)
        return std::nullopt;

    if (const auto* samples = json.getProperty("samples", juce::var()).getDynamicObject())
    {
        for (const auto& prop : samples->getProperties())
        {
            const auto slot = prop.name.toString();
            const auto path = prop.value.getProperty("file", "").toString();
            const auto* audioEntry = zip.getEntry(path);
            if (audioEntry == nullptr)
            {
                session->warnings.push_back("Missing audio for " + slot.toStdString());
                continue;
            }
            juce::String audioError;
            auto buffer = loadSample(std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(*audioEntry)),
                                     prop.value.getProperty("name", slot).toString(), audioError);
            if (buffer == nullptr)
            {
                session->warnings.push_back(audioError.toStdString());
                continue;
            }
            session->samples[slot.toStdString()] = std::shared_ptr<const dsp::SampleBuffer>(std::move(buffer));
        }
    }
    return session;
}
}

std::optional<SessionData> loadSession(const juce::File& file, juce::String& error)
{
    juce::ZipFile zip(file);
    return loadFromZip(zip, file.getFileName(), error);
}

std::optional<SessionData> readSession(const void* data, std::size_t size, juce::String& error)
{
    juce::MemoryInputStream stream(data, size, false);
    juce::ZipFile zip(stream);
    return loadFromZip(zip, "The saved state", error);
}
}
