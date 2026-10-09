#include <app/ControllerTemplates.h>
#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>
#include <io/Session.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>

using namespace tf;

namespace {
struct TempFolder
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tidefield_templates", "", false);
    ~TempFolder() { root.deleteRecursively(); }
};

juce::var binding(const juce::String& source, int channel, int number, const juce::String& param)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("source", source);
    o->setProperty("channel", channel);
    o->setProperty("number", number);
    o->setProperty("param", param);
    o->setProperty("low", 0.0);
    o->setProperty("high", 1.0);
    return juce::var(o);
}

juce::var midiWith(std::initializer_list<juce::var> bindings, int noteChannel = -1)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("noteChannel", noteChannel);
    juce::Array<juce::var> list;
    for (const auto& b : bindings)
        list.add(b);
    o->setProperty("bindings", list);
    return juce::var(o);
}
}

TEST_CASE("Every factory controller template names only controls MIDI can move", "[templates]")
{
    engine::Engine e;
    const auto templates = app::factoryControllerTemplates(e.getRegistry());
    REQUIRE(templates.size() >= 4);
    std::set<juce::String> names;
    for (const auto& t : templates)
    {
        INFO(t.name.toStdString());
        CHECK(t.factory);
        CHECK(names.insert(t.name).second);
        CHECK(app::countBindings(t.midi) >= 8);
        CHECK(app::validateControllerTemplate(t, e.getRegistry()).empty());
    }
}

TEST_CASE("A factory template applies through the session MIDI format without warnings", "[templates]")
{
    engine::Engine e;
    e.prepare(48000.0, 256);
    engine::MidiManager midi(e);
    const auto templates = app::factoryControllerTemplates(e.getRegistry());
    const auto& nano = templates.front();
    REQUIRE(nano.name.contains("nanoKONTROL2"));
    const auto warnings = io::applyMidiJson(app::mergeMidiJson(io::midiToJson(midi, e.getRegistry()), nano.midi, true), midi, e.getRegistry());
    CHECK(warnings.empty());
    REQUIRE(static_cast<int>(midi.getBindings().size()) == app::countBindings(nano.midi));
    int faders = 0, actions = 0;
    for (const auto& b : midi.getBindings())
    {
        if (b.action != engine::MidiAction::None)
        {
            ++actions;
            continue;
        }
        if (b.cc <= 7)
        {
            ++faders;
            CHECK_THAT(b.high, Catch::Matchers::WithinAbs(e.getRegistry().spec(b.param).toNormalised(0.0f), 1.0e-5));
        }
    }
    CHECK(faders == 8);
    CHECK(actions >= 5);
}

TEST_CASE("Controller templates round-trip through JSON and reject what is not one", "[templates]")
{
    engine::Engine e;
    const auto original = app::factoryControllerTemplates(e.getRegistry())[1];
    juce::String error;
    const auto back = app::controllerTemplateFromJson(juce::JSON::parse(juce::JSON::toString(app::controllerTemplateToJson(original))), error);
    REQUIRE(back.has_value());
    CHECK(back->name == original.name);
    CHECK(back->device == original.device);
    CHECK(app::countBindings(back->midi) == app::countBindings(original.midi));
    CHECK(app::validateControllerTemplate(*back, e.getRegistry()).empty());

    CHECK_FALSE(app::controllerTemplateFromJson(juce::JSON::parse("[1, 2]"), error).has_value());
    CHECK_FALSE(app::controllerTemplateFromJson(juce::JSON::parse(R"({"midi": {"bindings": []}})"), error).has_value());
    CHECK_FALSE(app::controllerTemplateFromJson(juce::JSON::parse(R"({"name": "x"})"), error).has_value());
    CHECK_FALSE(app::controllerTemplateFromJson(juce::JSON::parse(R"({"format": 99, "name": "x", "midi": {"bindings": []}})"), error).has_value());
}

TEST_CASE("Template validation names each bad mapping", "[templates]")
{
    engine::Engine e;
    app::ControllerTemplate t;
    t.name = "Broken";
    auto* action = new juce::DynamicObject();
    action->setProperty("source", "cc");
    action->setProperty("number", 10);
    action->setProperty("action", 99);
    t.midi = midiWith({ binding("cc", -1, 1, "macro.1"), binding("cc", -1, 2, "no.such"), binding("cc", 3, 200, "macro.2"),
                        binding("cc", 17, 4, "macro.3"), binding("note", 0, 36, "macro.4"), binding("knob", -1, 5, "macro.5"), juce::var(action) });
    const auto issues = app::validateControllerTemplate(t, e.getRegistry());
    CHECK(issues.size() == 6);
    t.midi = midiWith({});
    CHECK(app::validateControllerTemplate(t, e.getRegistry()).size() == 1);
}

TEST_CASE("Device names match a template's controller by whole words", "[templates]")
{
    CHECK(app::deviceMatches("nanoKONTROL2", "nanoKONTROL2"));
    CHECK(app::deviceMatches("nanoKONTROL2", "nanoKONTROL2 MIDI 1"));
    CHECK(app::deviceMatches("nanoKONTROL2", "nanoKONTROL2:nanoKONTROL2 _ CTRL 20:0"));
    CHECK_FALSE(app::deviceMatches("nanoKONTROL2", "nanoKONTROL"));
    CHECK_FALSE(app::deviceMatches("nanoKONTROL2", "nanoKONTROL Studio"));
    CHECK(app::deviceMatches("MIDI Mix", "MIDI Mix"));
    CHECK(app::deviceMatches("midi mix", "MIDI Mix MIDI 1"));
    CHECK(app::deviceMatches("Launch Control XL", "Launch Control XL"));
    CHECK_FALSE(app::deviceMatches("Launch Control XL", "Launch Control XL 3"));
    CHECK_FALSE(app::deviceMatches("Launch Control XL", "Launch Control"));
    CHECK(app::deviceMatches("Other|MIDI Mix", "MIDI Mix"));
    CHECK_FALSE(app::deviceMatches("", "MIDI Mix"));
}

TEST_CASE("Merging a template replaces the controls it uses and keeps the rest", "[templates]")
{
    const auto current = midiWith({ binding("cc", -1, 1, "macro.1"), binding("cc", 0, 2, "macro.2"), binding("cc", 5, 3, "macro.3"),
                                    binding("note", -1, 2, "macro.4") },
                                  4);
    const auto added = midiWith({ binding("cc", -1, 2, "drone.level"), binding("cc", 0, 3, "cloud1.level") });

    const auto merged = app::mergeMidiJson(current, added, false);
    CHECK(app::countBindings(merged) == 5);
    CHECK(static_cast<int>(merged["noteChannel"]) == 4);
    CHECK(app::midiContains(merged, added));
    CHECK(app::midiContains(merged, midiWith({ binding("cc", -1, 1, "macro.1"), binding("cc", 5, 3, "macro.3"), binding("note", -1, 2, "macro.4") })));
    CHECK_FALSE(app::midiContains(merged, midiWith({ binding("cc", 0, 2, "macro.2") })));

    const auto replaced = app::mergeMidiJson(current, added, true);
    CHECK(app::countBindings(replaced) == 2);
    CHECK(static_cast<int>(replaced["noteChannel"]) == 4);
    CHECK_FALSE(app::midiContains(replaced, midiWith({ binding("cc", -1, 1, "macro.1") })));
    CHECK_FALSE(app::midiContains(current, midiWith({})));
}

TEST_CASE("User templates are saved, listed, suggested and deleted", "[templates]")
{
    engine::Engine e;
    TempFolder temp;
    app::ControllerTemplateLibrary library(temp.root, e.getRegistry());
    const auto factoryCount = library.list().size();
    CHECK(library.userTemplates().empty());

    const auto mine = midiWith({ binding("cc", -1, 16, "macro.1"), binding("cc", -1, 0, "drone.level") });
    juce::String error;
    CHECK_FALSE(library.save("Korg nanoKONTROL2", "nanoKONTROL2", mine, error));
    CHECK_FALSE(library.save("  ", "", mine, error));
    CHECK_FALSE(library.save("Empty", "", midiWith({}), error));

    const int before = library.getVersion();
    REQUIRE(library.save("Studio/nano", "nanoKONTROL2", mine, error));
    CHECK(library.getVersion() > before);
    CHECK(library.exists("studio/NANO"));
    REQUIRE(library.userTemplates().size() == 1);
    CHECK(library.list().size() == factoryCount + 1);
    CHECK(library.userTemplates().front().file.getParentDirectory() == temp.root);

    REQUIRE(library.save("Studio/nano", "nanoKONTROL2", midiWith({ binding("cc", -1, 17, "macro.2") }), error));
    REQUIRE(library.userTemplates().size() == 1);
    CHECK(app::countBindings(library.userTemplates().front().midi) == 1);

    const auto suggested = library.suggestFor("nanoKONTROL2 MIDI 1");
    REQUIRE(suggested.has_value());
    CHECK(suggested->name == "Studio/nano");
    CHECK(library.suggestFor("MIDI Mix")->name == "Akai MIDImix");
    CHECK_FALSE(library.suggestFor("Some Keyboard").has_value());

    CHECK_FALSE(library.remove("Akai MIDImix"));
    CHECK(library.remove("Studio/nano"));
    CHECK(library.userTemplates().empty());
    CHECK(library.suggestFor("nanoKONTROL2")->factory);
}
