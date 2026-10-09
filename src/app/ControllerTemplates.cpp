#include "ControllerTemplates.h"

#include <engine/midi/MidiTypes.h>

#include <algorithm>
#include <array>

namespace tf::app {
namespace {
constexpr int kFormat = 1;
constexpr std::array<const char*, 8> kFaderStrips { "drone", "cloud1", "cloud2", "cloud3", "res", "bloom", "loop", "weather" };

class Builder
{
public:
    explicit Builder(const engine::ParamRegistry& r) : registry(r) {}

    Builder& cc(int number, const juce::String& param, std::optional<float> top = std::nullopt)
    {
        float high = 1.0f;
        if (const auto index = registry.find(param.toStdString()); index && top)
            high = registry.spec(*index).toNormalised(*top);
        auto* o = binding("cc", number);
        o->setProperty("param", param);
        o->setProperty("high", high);
        o->setProperty("pickup", true);
        return *this;
    }

    Builder& ccAction(int number, engine::MidiAction action)
    {
        auto* o = binding("cc", number);
        o->setProperty("action", static_cast<int>(action));
        o->setProperty("high", 1.0f);
        o->setProperty("pickup", false);
        return *this;
    }

    Builder& faders(std::initializer_list<int> numbers)
    {
        std::size_t i = 0;
        for (const int n : numbers)
            if (i < kFaderStrips.size())
                cc(n, juce::String(kFaderStrips[i++]) + ".level", 0.0f);
        return *this;
    }

    Builder& row(std::initializer_list<int> numbers, const juce::String& suffix)
    {
        std::size_t i = 0;
        for (const int n : numbers)
            if (i < kFaderStrips.size())
                cc(n, juce::String(kFaderStrips[i++]) + "." + suffix);
        return *this;
    }

    Builder& macros(std::initializer_list<int> numbers)
    {
        int i = 1;
        for (const int n : numbers)
            cc(n, "macro." + juce::String(i++));
        return *this;
    }

    juce::var build() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("bindings", list);
        return juce::var(root);
    }

private:
    juce::DynamicObject* binding(const char* source, int number)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("source", source);
        o->setProperty("channel", -1);
        o->setProperty("number", number);
        o->setProperty("low", 0.0f);
        o->setProperty("curve", 0.0f);
        list.add(juce::var(o));
        return o;
    }

    const engine::ParamRegistry& registry;
    juce::Array<juce::var> list;
};

const juce::Array<juce::var>* bindingsOf(const juce::var& midi) { return midi.getProperty("bindings", juce::var()).getArray(); }

bool sameControl(const juce::var& a, const juce::var& b, bool exactChannel)
{
    if (a.getProperty("source", "cc").toString() != b.getProperty("source", "cc").toString())
        return false;
    if (static_cast<int>(a.getProperty("number", -1)) != static_cast<int>(b.getProperty("number", -1)))
        return false;
    const int ca = a.getProperty("channel", -1), cb = b.getProperty("channel", -1);
    return ca == cb || (! exactChannel && (ca < 0 || cb < 0));
}

bool sameTarget(const juce::var& a, const juce::var& b)
{
    if (a.hasProperty("action") || b.hasProperty("action"))
        return a.hasProperty("action") && b.hasProperty("action") && static_cast<int>(a["action"]) == static_cast<int>(b["action"]);
    return a.getProperty("param", "").toString() == b.getProperty("param", "").toString();
}

juce::StringArray tokens(const juce::String& text)
{
    juce::String cleaned;
    for (const auto c : text.toLowerCase())
        cleaned += juce::CharacterFunctions::isLetterOrDigit(c) ? c : juce::juce_wchar(' ');
    return juce::StringArray::fromTokens(cleaned, " ", {});
}

bool isNumber(const juce::String& token) { return token.isNotEmpty() && token.containsOnly("0123456789"); }
}

std::vector<ControllerTemplate> factoryControllerTemplates(const engine::ParamRegistry& registry)
{
    using engine::MidiAction;
    std::vector<ControllerTemplate> list;
    auto add = [&](const char* name, const char* device, const char* description, const Builder& b) {
        ControllerTemplate t;
        t.name = name;
        t.device = device;
        t.description = description;
        t.midi = b.build();
        t.factory = true;
        list.push_back(std::move(t));
    };

    add("Korg nanoKONTROL2", "nanoKONTROL2",
        "Default scene in CC mode. Knobs play the eight macros, faders the source levels (top is 0 dB). "
        "Play fades, Stop is panic, Record records, Cycle runs the tape loop, Set captures a scene, << releases held controls, >> catches.",
        Builder(registry)
            .macros({ 16, 17, 18, 19, 20, 21, 22, 23 })
            .faders({ 0, 1, 2, 3, 4, 5, 6, 7 })
            .ccAction(41, MidiAction::FadeToggle)
            .ccAction(42, MidiAction::Panic)
            .ccAction(45, MidiAction::RecordToggle)
            .ccAction(46, MidiAction::LoopRecord)
            .ccAction(60, MidiAction::CaptureScene)
            .ccAction(43, MidiAction::ReleaseLive)
            .ccAction(44, MidiAction::Catch));

    add("Akai MIDImix", "MIDI Mix",
        "Factory layout. Top knobs play the macros, middle knobs the reverb sends, bottom knobs the delay sends, faders the source "
        "levels and the master fader the master level (top is 0 dB). The buttons are left free to learn.",
        Builder(registry)
            .macros({ 16, 20, 24, 28, 46, 50, 54, 58 })
            .row({ 17, 21, 25, 29, 47, 51, 55, 59 }, "sendA")
            .row({ 18, 22, 26, 30, 48, 52, 56, 60 }, "sendB")
            .faders({ 19, 23, 27, 31, 49, 53, 57, 61 })
            .cc(62, "master.level", 0.0f));

    add("Novation Launch Control XL", "Launch Control XL",
        "Factory template 1. Top knobs play the macros, middle knobs the reverb sends, bottom knobs the delay sends, faders the "
        "source levels (top is 0 dB). The buttons are left free to learn.",
        Builder(registry)
            .macros({ 13, 14, 15, 16, 17, 18, 19, 20 })
            .row({ 29, 30, 31, 32, 33, 34, 35, 36 }, "sendA")
            .row({ 49, 50, 51, 52, 53, 54, 55, 56 }, "sendB")
            .faders({ 77, 78, 79, 80, 81, 82, 83, 84 }));

    add("Generic 8 knobs + 8 faders", "",
        "Any controller set to send CC 1 to 8 from its knobs and CC 9 to 16 from its faders. Knobs play the macros, faders the "
        "source levels (top is 0 dB). CC 1 is also the mod wheel source.",
        Builder(registry).macros({ 1, 2, 3, 4, 5, 6, 7, 8 }).faders({ 9, 10, 11, 12, 13, 14, 15, 16 }));

    add("Tidefield default (CC 21 to 28)", "",
        "The built-in mapping for an eight-knob controller: terrain X and Y, Tide, Wander, Gravity, reverb return, Cloud 1 density "
        "and master level.",
        Builder(registry)
            .cc(21, "terrain.x")
            .cc(22, "terrain.y")
            .cc(23, "tide.rate")
            .cc(24, "terrain.wander")
            .cc(25, "harmony.gravity")
            .cc(26, "busA.level")
            .cc(27, "cloud1.density")
            .cc(28, "master.level", 0.0f));
    return list;
}

juce::var controllerTemplateToJson(const ControllerTemplate& t)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", kFormat);
    root->setProperty("name", t.name);
    root->setProperty("device", t.device);
    root->setProperty("description", t.description);
    root->setProperty("midi", t.midi);
    return juce::var(root);
}

std::optional<ControllerTemplate> controllerTemplateFromJson(const juce::var& json, juce::String& error)
{
    if (json.getDynamicObject() == nullptr)
    {
        error = "not a controller template";
        return std::nullopt;
    }
    if (static_cast<int>(json.getProperty("format", kFormat)) > kFormat)
    {
        error = "made by a newer Tidefield";
        return std::nullopt;
    }
    ControllerTemplate t;
    t.name = json.getProperty("name", "").toString().trim();
    t.device = json.getProperty("device", "").toString().trim();
    t.description = json.getProperty("description", "").toString();
    t.midi = json.getProperty("midi", juce::var());
    if (t.name.isEmpty())
    {
        error = "the template has no name";
        return std::nullopt;
    }
    if (bindingsOf(t.midi) == nullptr)
    {
        error = "the template has no mappings";
        return std::nullopt;
    }
    return t;
}

std::vector<std::string> validateControllerTemplate(const ControllerTemplate& t, const engine::ParamRegistry& registry)
{
    std::vector<std::string> issues;
    const auto prefix = t.name.toStdString() + ": ";
    if (t.name.trim().isEmpty())
        issues.push_back("a template has no name");
    const auto* list = bindingsOf(t.midi);
    if (list == nullptr || list->isEmpty())
    {
        issues.push_back(prefix + "no mappings");
        return issues;
    }
    if (list->size() > engine::kMaxMidiBindings)
        issues.push_back(prefix + "more than " + std::to_string(engine::kMaxMidiBindings) + " mappings");
    for (int i = 0; i < list->size(); ++i)
    {
        const auto& b = list->getReference(i);
        const auto where = prefix + "mapping " + std::to_string(i + 1) + " ";
        const auto source = b.getProperty("source", "cc").toString();
        if (source != "cc" && source != "note")
            issues.push_back(where + "has an unknown source '" + source.toStdString() + "'");
        const int number = b.getProperty("number", -1);
        if (number < 0 || number > 127)
            issues.push_back(where + "uses number " + std::to_string(number));
        const int channel = b.getProperty("channel", -1);
        if (channel < -1 || channel > 15)
            issues.push_back(where + "uses channel " + std::to_string(channel));
        const double low = b.getProperty("low", 0.0), high = b.getProperty("high", 1.0);
        if (low < 0.0 || low > 1.0 || high < 0.0 || high > 1.0)
            issues.push_back(where + "has a range outside 0 to 1");
        if (b.hasProperty("action"))
        {
            const int action = b["action"];
            if (action <= static_cast<int>(engine::MidiAction::None) || action > static_cast<int>(engine::MidiAction::InputFreezeToggle))
                issues.push_back(where + "names an unknown action");
            continue;
        }
        const auto id = b.getProperty("param", "").toString().toStdString();
        const auto index = registry.find(id);
        if (! index)
            issues.push_back(where + "names an unknown parameter '" + id + "'");
        else if ((registry.spec(*index).flags & engine::ParamFlag::kMidiLearnable) == 0)
            issues.push_back(where + "names '" + id + "', which MIDI cannot control");
        else if (source == "note")
            issues.push_back(where + "maps a note to a parameter; notes only trigger actions in templates");
    }
    return issues;
}

bool deviceMatches(const juce::String& templateDevice, const juce::String& deviceName)
{
    const auto name = tokens(deviceName);
    for (const auto& alternative : juce::StringArray::fromTokens(templateDevice, "|", {}))
    {
        const auto wanted = tokens(alternative);
        if (wanted.isEmpty() || wanted.size() > name.size())
            continue;
        for (int i = 0; i + wanted.size() <= name.size(); ++i)
        {
            bool same = true;
            for (int k = 0; k < wanted.size() && same; ++k)
                same = name[i + k] == wanted[k];
            const int next = i + wanted.size();
            if (same && (next == name.size() || ! isNumber(name[next])))
                return true;
        }
    }
    return false;
}

int countBindings(const juce::var& midi)
{
    const auto* list = bindingsOf(midi);
    return list != nullptr ? list->size() : 0;
}

juce::var mergeMidiJson(const juce::var& current, const juce::var& added, bool replace)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("noteChannel", current.getProperty("noteChannel", -1));
    root->setProperty("notesToDrone", current.getProperty("notesToDrone", false));
    root->setProperty("mpe", current.getProperty("mpe", false));
    juce::Array<juce::var> result;
    const auto* incoming = bindingsOf(added);
    if (! replace)
        if (const auto* existing = bindingsOf(current))
            for (const auto& b : *existing)
            {
                bool replaced = false;
                if (incoming != nullptr)
                    for (const auto& n : *incoming)
                        replaced = replaced || sameControl(b, n, false);
                if (! replaced)
                    result.add(b);
            }
    if (incoming != nullptr)
        result.addArray(*incoming);
    root->setProperty("bindings", result);
    return juce::var(root);
}

bool midiContains(const juce::var& current, const juce::var& wanted)
{
    const auto* want = bindingsOf(wanted);
    if (want == nullptr || want->isEmpty())
        return false;
    const auto* have = bindingsOf(current);
    if (have == nullptr)
        return false;
    for (const auto& w : *want)
    {
        bool found = false;
        for (const auto& h : *have)
            found = found || (sameControl(w, h, true) && sameTarget(w, h));
        if (! found)
            return false;
    }
    return true;
}

ControllerTemplateLibrary::ControllerTemplateLibrary(juce::File f, const engine::ParamRegistry& r)
    : folder(std::move(f)), registry(r), factory(factoryControllerTemplates(r))
{
}

std::vector<ControllerTemplate> ControllerTemplateLibrary::userTemplates() const
{
    std::vector<ControllerTemplate> list;
    if (! folder.isDirectory())
        return list;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        juce::String error;
        if (auto t = controllerTemplateFromJson(juce::JSON::parse(file), error))
        {
            t->factory = false;
            t->file = file;
            list.push_back(std::move(*t));
        }
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    return list;
}

std::vector<ControllerTemplate> ControllerTemplateLibrary::list() const
{
    auto all = factory;
    for (auto& t : userTemplates())
        all.push_back(std::move(t));
    return all;
}

bool ControllerTemplateLibrary::exists(const juce::String& name) const
{
    for (const auto& t : list())
        if (t.name.equalsIgnoreCase(name.trim()))
            return true;
    return false;
}

bool ControllerTemplateLibrary::save(const juce::String& rawName, const juce::String& device, const juce::var& midi, juce::String& error)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
    {
        error = "A template needs a name.";
        return false;
    }
    for (const auto& t : factory)
        if (t.name.equalsIgnoreCase(name))
        {
            error = "\"" + name + "\" is a factory template. Choose another name.";
            return false;
        }
    if (countBindings(midi) == 0)
    {
        error = "There are no MIDI mappings to save yet.";
        return false;
    }
    ControllerTemplate t;
    t.name = name;
    t.device = device.trim();
    t.description = "Saved from your mappings (" + juce::String(countBindings(midi)) + ").";
    t.midi = midi;
    if (! folder.createDirectory())
    {
        error = "Could not create " + folder.getFullPathName();
        return false;
    }
    for (const auto& existing : userTemplates())
        if (existing.name.equalsIgnoreCase(name))
            existing.file.deleteFile();
    const auto file = folder.getChildFile(safeFileName(name) + ".json");
    if (! file.replaceWithText(juce::JSON::toString(controllerTemplateToJson(t), false)))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    ++version;
    return true;
}

bool ControllerTemplateLibrary::remove(const juce::String& name)
{
    for (const auto& t : userTemplates())
        if (t.name == name && t.file.deleteFile())
        {
            ++version;
            return true;
        }
    return false;
}

std::optional<ControllerTemplate> ControllerTemplateLibrary::suggestFor(const juce::String& deviceName) const
{
    for (const auto& t : userTemplates())
        if (deviceMatches(t.device, deviceName))
            return t;
    for (const auto& t : factory)
        if (deviceMatches(t.device, deviceName))
            return t;
    return std::nullopt;
}

juce::String ControllerTemplateLibrary::safeFileName(const juce::String& name)
{
    const auto legal = juce::File::createLegalFileName(name.trim());
    return legal.isEmpty() ? juce::String("Template") : legal;
}
}
