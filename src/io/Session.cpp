#include "Session.h"

#include "AudioFileIO.h"

#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
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
    return names;
}

/** Upgrades older JSON in place, one version step at a time. Add a case for every
    schema change; never edit an existing step. */
bool migrate(juce::DynamicObject& root, int from, juce::String& error)
{
    for (int v = from; v < SessionData::kCurrentVersion; ++v)
    {
        switch (v)
        {
            // case 1: (1 -> 2) e.g. rename a parameter id inside "params" and "scenes".
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
            m[prop.name.toString().toStdString()] = static_cast<float>(static_cast<double>(prop.value));
    return m;
}

} // namespace

SessionData captureSession(const engine::Engine& engine, const engine::TelemetryFrame& latest, const engine::SceneManager& scenes,
                           const engine::FxManager& fx)
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
        s.fx[engine::kFxSlots[static_cast<std::size_t>(slot)].id] = fx.getType(slot);

    for (int k = 0; k < engine::kNumClouds; ++k)
        if (auto b = engine.getCloudSample(k))
            s.samples["cloud" + std::to_string(k + 1)] = b;
    if (auto b = engine.getBloomSample())
        s.samples["bloom"] = b;
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
                                      engine::FxManager& fx, bool snap)
{
    const auto& reg = engine.getRegistry();
    std::vector<std::string> warnings = session.warnings;

    // FX types first: loading a processor posts its defaults, which the stored
    // values below then override.
    for (const auto& [slotId, type] : session.fx)
    {
        const int slot = engine::FxManager::findSlot(slotId);
        if (slot < 0)
            warnings.push_back("Unknown FX slot '" + slotId + "'");
        else if (! type.empty() && dsp::ProcessorFactory::instance().find(type) == nullptr)
            warnings.push_back("Unknown processor '" + type + "' in " + slotId + " (left empty)");
        else
            fx.setType(slot, type, false);
    }

    engine.command(engine::Command::ReleaseLiveLayer);
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

    // Samples: every slot is set, so a session without a cloud sample clears it.
    for (int k = 0; k < engine::kNumClouds; ++k)
    {
        const auto it = session.samples.find("cloud" + std::to_string(k + 1));
        engine.loadCloudSample(k, it != session.samples.end() ? it->second : nullptr);
    }
    const auto bloom = session.samples.find("bloom");
    engine.loadBloomSample(bloom != session.samples.end() ? bloom->second : nullptr);
    return warnings;
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

    root->setProperty("midi", s.midi);

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
    s.midi = root->getProperty("midi");
    return s;
}

bool saveSession(const SessionData& s, const juce::File& file, juce::String& error)
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
        // FLAC is already compressed: store it.
        zip.addEntry(std::make_unique<juce::MemoryInputStream>(std::move(flac)), 0, "audio/" + juce::String(slot) + ".flac", now);
    }

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
        if (! zip.writeToStream(out, nullptr))
        {
            error = "Writing the session failed";
            temp.deleteFile();
            return false;
        }
    }
    if (! temp.moveFileTo(file))
    {
        error = "Could not replace " + file.getFullPathName();
        temp.deleteFile();
        return false;
    }
    return true;
}

std::optional<SessionData> loadSession(const juce::File& file, juce::String& error)
{
    juce::ZipFile zip(file);
    const auto* entry = zip.getEntry(kJsonEntry);
    if (entry == nullptr)
    {
        error = file.getFileName() + " is not a Tidefield session";
        return std::nullopt;
    }
    std::unique_ptr<juce::InputStream> jsonStream(zip.createStreamForEntry(*entry));
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

} // namespace tf::io
