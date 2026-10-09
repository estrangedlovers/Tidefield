#include <app/KeyBindings.h>

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace tf;
using app::KeyAction;
using app::KeyBindings;

namespace {
constexpr int cmd = juce::ModifierKeys::commandModifier;
constexpr int shift = juce::ModifierKeys::shiftModifier;
constexpr int alt = juce::ModifierKeys::altModifier;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }
}

TEST_CASE("Every action has a unique id and the defaults never clash", "[keys]")
{
    KeyBindings keys;
    REQUIRE(static_cast<int>(app::keyActions().size()) == app::kNumKeyActions);
    std::set<std::string> ids;
    for (int i = 0; i < app::kNumKeyActions; ++i)
    {
        const auto& info = app::keyActions()[static_cast<std::size_t>(i)];
        CHECK(static_cast<int>(info.action) == i);
        CHECK(ids.insert(info.id).second);
        CHECK(app::keyActionFromId(info.id) == info.action);
        CHECK(keys.get(info.action).isValid());
        CHECK(keys.isDefault(info.action));
        CHECK(keys.conflicts(info.action).empty());
        CHECK(KeyBindings::reservedUse(keys.get(info.action)).isEmpty());
    }
}

TEST_CASE("The default keys are the ones Tidefield has always used", "[keys]")
{
    KeyBindings keys;
    CHECK(keys.find(key(juce::KeyPress::spaceKey)) == KeyAction::Fade);
    CHECK(keys.find(key(juce::KeyPress::escapeKey)) == KeyAction::Panic);
    CHECK(keys.find(key('c')) == KeyAction::Capture);
    CHECK(keys.find(key('C', shift)) == KeyAction::Capture);
    CHECK(keys.find(key('r')) == KeyAction::Release);
    CHECK(keys.find(key('R', shift)) == KeyAction::Record);
    CHECK(keys.find(key('l')) == KeyAction::LoopRecord);
    CHECK(keys.find(key('L', shift)) == KeyAction::LoopClear);
    CHECK(keys.find(key('g')) == KeyAction::Take);
    CHECK(keys.find(key('G', shift)) == KeyAction::NewTake);
    CHECK(keys.find(key('s')) == KeyAction::Swell);
    CHECK(keys.find(key('F', shift)) == KeyAction::Freeze);
    CHECK(keys.find(key('m')) == KeyAction::NoteMode);
    CHECK(keys.find(key('p')) == KeyAction::DrawPath);
    CHECK(keys.find(key('p', cmd)) == KeyAction::Projector);
    CHECK(keys.find(key('z', cmd)) == KeyAction::Undo);
    CHECK(keys.find(key('Z', cmd | shift)) == KeyAction::Redo);
    CHECK(keys.find(key('s', cmd)) == KeyAction::Save);
    CHECK(keys.find(key('S', cmd | shift)) == KeyAction::SaveAs);
    CHECK(keys.find(key('O', cmd | shift)) == KeyAction::Open);
    CHECK(keys.find(key(',', cmd)) == KeyAction::Settings);
    CHECK(keys.find(key('=', cmd)) == KeyAction::ZoomIn);
    CHECK(keys.find(key('+', cmd | shift)) == KeyAction::ZoomIn);
    CHECK(keys.find(key('-', cmd)) == KeyAction::ZoomOut);
    CHECK(keys.find(key('0', cmd)) == KeyAction::ZoomReset);
    CHECK(keys.find(key('f', alt)) == KeyAction::Freeze);
    CHECK_FALSE(keys.find(key('q')).has_value());
    CHECK_FALSE(keys.find(key('q', cmd)).has_value());
    CHECK_FALSE(keys.find(key('f', cmd)).has_value());
    CHECK_FALSE(keys.find(juce::KeyPress()).has_value());
}

TEST_CASE("A new key replaces the old one and a shared key is reported as a clash", "[keys]")
{
    KeyBindings keys;
    const int before = keys.getVersion();
    keys.set(KeyAction::Freeze, key('q'));
    CHECK(keys.getVersion() > before);
    CHECK(keys.find(key('Q')) == KeyAction::Freeze);
    CHECK_FALSE(keys.find(key('f')).has_value());
    CHECK_FALSE(keys.isDefault(KeyAction::Freeze));

    keys.set(KeyAction::Cycles, key('c'));
    CHECK(keys.conflicts(KeyAction::Cycles) == std::vector<KeyAction> { KeyAction::Capture });
    CHECK(keys.conflicts(KeyAction::Capture) == std::vector<KeyAction> { KeyAction::Cycles });
    CHECK(keys.usersOf(key('C')).size() == 2);
    CHECK(keys.find(key('c')) == KeyAction::Capture);

    keys.set(KeyAction::Capture, {});
    CHECK_FALSE(keys.get(KeyAction::Capture).isValid());
    CHECK(keys.conflicts(KeyAction::Cycles).empty());
    CHECK(keys.find(key('c')) == KeyAction::Cycles);

    keys.reset(KeyAction::Capture);
    CHECK(keys.isDefault(KeyAction::Capture));
    keys.resetAll();
    for (const auto& info : app::keyActions())
        CHECK(keys.isDefault(info.action));
}

TEST_CASE("Only changed shortcuts are stored, and they load back", "[keys]")
{
    KeyBindings keys;
    CHECK(keys.toVar().getDynamicObject()->getProperties().isEmpty());
    keys.set(KeyAction::Fade, key(juce::KeyPress::F5Key, shift));
    keys.set(KeyAction::Catch, {});
    keys.set(KeyAction::Settings, key(';', cmd | alt));
    const auto stored = juce::JSON::toString(keys.toVar());

    KeyBindings loaded;
    loaded.fromVar(juce::JSON::parse(stored));
    for (const auto& info : app::keyActions())
    {
        INFO(info.id);
        CHECK(loaded.get(info.action).isValid() == keys.get(info.action).isValid());
        if (keys.get(info.action).isValid())
            CHECK(KeyBindings::same(loaded.get(info.action), keys.get(info.action)));
    }
    CHECK(loaded.toVar().getDynamicObject()->getProperties().size() == 3);

    KeyBindings garbled;
    garbled.fromVar(juce::JSON::parse(R"({"fade": "hyper Q", "freeze": "", "nonsense": "cmd K", "panic": "cmd shift"})"));
    for (const auto& info : app::keyActions())
        CHECK(garbled.isDefault(info.action));
}

TEST_CASE("Shortcuts survive the settings file", "[keys]")
{
    const juce::ScopedJuceInitialiser_GUI messageLoop;
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tidefield_keys", ".settings", false);
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    {
        juce::PropertiesFile settings(file, options);
        KeyBindings keys;
        keys.set(KeyAction::Panic, key(juce::KeyPress::deleteKey));
        keys.save(settings);
    }
    juce::PropertiesFile settings(file, options);
    KeyBindings keys;
    keys.load(settings);
    CHECK(keys.find(key(juce::KeyPress::deleteKey)) == KeyAction::Panic);
    CHECK_FALSE(keys.find(key(juce::KeyPress::escapeKey)).has_value());
    file.deleteFile();
}

TEST_CASE("Key text round-trips and reads well", "[keys]")
{
    for (const auto& k : { key(juce::KeyPress::spaceKey), key(juce::KeyPress::escapeKey), key(',', cmd), key('+', cmd), key('-', cmd | shift),
                           key(juce::KeyPress::F12Key), key(juce::KeyPress::leftKey, shift), key('Q', alt), key('/'), key(';') })
    {
        INFO(KeyBindings::toText(k).toStdString());
        CHECK(KeyBindings::same(KeyBindings::fromText(KeyBindings::toText(k)), k));
    }
    CHECK_FALSE(KeyBindings::fromText("none").isValid());
    CHECK_FALSE(KeyBindings::fromText("").isValid());
    CHECK_FALSE(KeyBindings::fromText("cmd").isValid());

    CHECK(KeyBindings::describe(key(juce::KeyPress::spaceKey), false) == "Space");
    CHECK(KeyBindings::describe(key('r', shift), false) == "Shift+R");
    CHECK(KeyBindings::describe({}, false) == "None");
    CHECK(KeyBindings::describe({}, true).isEmpty());
    CHECK(KeyBindings::describe(key('=', cmd), false).endsWith("+"));
    KeyBindings keys;
    CHECK(keys.hint(KeyAction::Catch) == " (K)");
    keys.set(KeyAction::Catch, {});
    CHECK(keys.hint(KeyAction::Catch).isEmpty());
}

TEST_CASE("Tab, the arrows and the scene numbers are reserved", "[keys]")
{
    CHECK(KeyBindings::reservedUse(key(juce::KeyPress::tabKey)).isNotEmpty());
    CHECK(KeyBindings::reservedUse(key(juce::KeyPress::tabKey, shift)).isNotEmpty());
    CHECK(KeyBindings::reservedUse(key(juce::KeyPress::upKey)).isNotEmpty());
    CHECK(KeyBindings::reservedUse(key('5')).isNotEmpty());
    CHECK(KeyBindings::reservedUse(key('3', shift)).isNotEmpty());
    CHECK(KeyBindings::reservedUse(key('5', cmd)).isEmpty());
    CHECK(KeyBindings::reservedUse(key('0')).isEmpty());
    CHECK(KeyBindings::reservedUse(key('Q')).isEmpty());
}
