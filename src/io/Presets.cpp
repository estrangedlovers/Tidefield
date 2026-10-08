#include "Presets.h"

#include <algorithm>

namespace tf::io {
namespace {
juce::String safeName(const std::string& name)
{
    auto s = juce::File::createLegalFileName(juce::String(name)).trim();
    return s.isEmpty() ? juce::String("Preset") : s;
}
}

PresetLibrary::PresetLibrary(juce::File f) : folder(std::move(f)) {}

juce::File PresetLibrary::defaultFolder()
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Tidefield").getChildFile("Presets");
}

juce::File PresetLibrary::folderFor(const std::string& kind) const
{
    return folder.getChildFile(juce::String(kind).replaceCharacter(':', '-'));
}

void PresetLibrary::addFactory(Preset p)
{
    p.factory = true;
    factory.push_back(std::move(p));
}

std::vector<Preset> PresetLibrary::list(const std::string& kind) const
{
    std::vector<Preset> out;
    for (const auto& p : factory)
        if (p.kind == kind)
            out.push_back(p);
    std::vector<Preset> user;
    for (const auto& file : folderFor(kind).findChildFiles(juce::File::findFiles, false, "*.json"))
        if (auto p = fromJson(juce::JSON::parse(file.loadFileAsString())); p && p->kind == kind)
        {
            p->file = file;
            user.push_back(std::move(*p));
        }
    auto byName = [](const Preset& a, const Preset& b) { return juce::String(a.name).compareNatural(juce::String(b.name)) < 0; };
    std::sort(out.begin(), out.end(), byName);
    std::sort(user.begin(), user.end(), byName);
    out.insert(out.end(), user.begin(), user.end());
    return out;
}

bool PresetLibrary::save(const Preset& p, juce::String& error)
{
    const auto dir = folderFor(p.kind);
    if (! dir.createDirectory())
    {
        error = "Could not create " + dir.getFullPathName();
        return false;
    }
    auto file = dir.getChildFile(safeName(p.name) + ".json");
    for (int n = 2; file.existsAsFile(); ++n)
    {
        const auto existing = fromJson(juce::JSON::parse(file.loadFileAsString()));
        if (existing && existing->name == p.name)
            break;
        file = dir.getChildFile(safeName(p.name) + " " + juce::String(n) + ".json");
    }
    if (! file.replaceWithText(juce::JSON::toString(toJson(p), false)))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool PresetLibrary::remove(const Preset& p)
{
    if (p.factory)
        return false;
    const auto file = p.file != juce::File() ? p.file : folderFor(p.kind).getChildFile(safeName(p.name) + ".json");
    return file.isAChildOf(folder) && file.deleteFile();
}

juce::var PresetLibrary::toJson(const Preset& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("format", "tidefield-preset");
    o->setProperty("name", juce::String(p.name));
    o->setProperty("kind", juce::String(p.kind));
    auto* values = new juce::DynamicObject();
    for (const auto& [key, value] : p.values)
        values->setProperty(juce::Identifier(juce::String(key)), value);
    o->setProperty("values", juce::var(values));
    return juce::var(o);
}

std::optional<Preset> PresetLibrary::fromJson(const juce::var& json)
{
    if (json.getProperty("format", {}).toString() != "tidefield-preset")
        return std::nullopt;
    Preset p;
    p.name = json.getProperty("name", "").toString().toStdString();
    p.kind = json.getProperty("kind", "").toString().toStdString();
    if (p.name.empty() || p.kind.empty())
        return std::nullopt;
    if (const auto* values = json.getProperty("values", {}).getDynamicObject())
        for (const auto& prop : values->getProperties())
            p.values[prop.name.toString().toStdString()] = static_cast<float>(static_cast<double>(prop.value));
    return p;
}
}
