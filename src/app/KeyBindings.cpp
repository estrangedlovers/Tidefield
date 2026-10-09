#include "KeyBindings.h"

namespace tf::app {
namespace {
constexpr int kModifierMask = juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier
                              | juce::ModifierKeys::commandModifier;

juce::KeyPress chord(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

struct KeyName
{
    int code;
    const char* text;
    const char* word;
    const char* symbol;
};

const std::vector<KeyName>& keyNames()
{
    static const std::vector<KeyName> names = [] {
        std::vector<KeyName> n {
            { juce::KeyPress::spaceKey, "space", "Space", "Space" },
            { juce::KeyPress::escapeKey, "escape", "Esc", "Esc" },
            { juce::KeyPress::returnKey, "return", "Return", "\xe2\x86\xa9" },
            { juce::KeyPress::tabKey, "tab", "Tab", "\xe2\x87\xa5" },
            { juce::KeyPress::deleteKey, "delete", "Delete", "\xe2\x8c\xa6" },
            { juce::KeyPress::backspaceKey, "backspace", "Backspace", "\xe2\x8c\xab" },
            { juce::KeyPress::insertKey, "insert", "Insert", "Insert" },
            { juce::KeyPress::homeKey, "home", "Home", "\xe2\x86\x96" },
            { juce::KeyPress::endKey, "end", "End", "\xe2\x86\x98" },
            { juce::KeyPress::pageUpKey, "pageup", "Page Up", "\xe2\x87\x9e" },
            { juce::KeyPress::pageDownKey, "pagedown", "Page Down", "\xe2\x87\x9f" },
            { juce::KeyPress::leftKey, "left", "Left", "\xe2\x86\x90" },
            { juce::KeyPress::rightKey, "right", "Right", "\xe2\x86\x92" },
            { juce::KeyPress::upKey, "up", "Up", "\xe2\x86\x91" },
            { juce::KeyPress::downKey, "down", "Down", "\xe2\x86\x93" },
        };
        static const std::array<int, 16> functionKeys { juce::KeyPress::F1Key,  juce::KeyPress::F2Key,  juce::KeyPress::F3Key,  juce::KeyPress::F4Key,
                                                        juce::KeyPress::F5Key,  juce::KeyPress::F6Key,  juce::KeyPress::F7Key,  juce::KeyPress::F8Key,
                                                        juce::KeyPress::F9Key,  juce::KeyPress::F10Key, juce::KeyPress::F11Key, juce::KeyPress::F12Key,
                                                        juce::KeyPress::F13Key, juce::KeyPress::F14Key, juce::KeyPress::F15Key, juce::KeyPress::F16Key };
        static const std::array<const char*, 16> functionNames { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
                                                                 "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16" };
        for (std::size_t i = 0; i < functionKeys.size(); ++i)
            n.push_back({ functionKeys[i], functionNames[i], functionNames[i], functionNames[i] });
        return n;
    }();
    return names;
}

const KeyName* nameFor(int code)
{
    for (const auto& n : keyNames())
        if (n.code == code)
            return &n;
    return nullptr;
}

bool printable(int code) { return code > 32 && code < 0x7f; }
}

const std::vector<KeyActionInfo>& keyActions()
{
    using M = juce::ModifierKeys;
    constexpr int cmd = M::commandModifier, shift = M::shiftModifier;
    static const std::vector<KeyActionInfo> table {
        { KeyAction::NewSession, "newSession", "New session", "File", chord('N', cmd) },
        { KeyAction::Open, "open", "Open", "File", chord('O', cmd) },
        { KeyAction::Save, "save", "Save", "File", chord('S', cmd) },
        { KeyAction::SaveAs, "saveAs", "Save as", "File", chord('S', cmd | shift) },
        { KeyAction::Undo, "undo", "Undo", "Edit", chord('Z', cmd) },
        { KeyAction::Redo, "redo", "Redo", "Edit", chord('Z', cmd | shift) },
        { KeyAction::Settings, "settings", "Settings", "Edit", chord(',', cmd) },
        { KeyAction::ZoomIn, "zoomIn", "Zoom in", "View", chord('+', cmd) },
        { KeyAction::ZoomOut, "zoomOut", "Zoom out", "View", chord('-', cmd) },
        { KeyAction::ZoomReset, "zoomReset", "Actual size", "View", chord('0', cmd) },
        { KeyAction::Projector, "projector", "Projector window", "View", chord('P', cmd) },
        { KeyAction::Fade, "fade", "Fade in or out", "Play", chord(juce::KeyPress::spaceKey) },
        { KeyAction::Panic, "panic", "Panic or resume", "Play", chord(juce::KeyPress::escapeKey) },
        { KeyAction::Capture, "capture", "Capture scene", "Play", chord('C') },
        { KeyAction::Release, "release", "Release held controls", "Play", chord('R') },
        { KeyAction::Record, "record", "Record", "Play", chord('R', shift) },
        { KeyAction::NoteMode, "noteMode", "Keys play Bloom", "Play", chord('M') },
        { KeyAction::Swell, "swell", "Swell (hold)", "Gestures", chord('S'), true },
        { KeyAction::Hush, "hush", "Hush (hold)", "Gestures", chord('H'), true },
        { KeyAction::Slow, "slow", "Slow (hold)", "Gestures", chord('T'), true },
        { KeyAction::Freeze, "freeze", "Freeze all", "Gestures", chord('F') },
        { KeyAction::InputFreeze, "inputFreeze", "Freeze input", "Gestures", chord('I') },
        { KeyAction::Cycles, "cycles", "Cycles on or off", "Gestures", chord('E') },
        { KeyAction::LoopRecord, "loopRecord", "Loop: record, overdub", "Gestures", chord('L') },
        { KeyAction::LoopClear, "loopClear", "Loop: clear", "Gestures", chord('L', shift) },
        { KeyAction::Catch, "catch", "Catch the last seconds", "Gestures", chord('K') },
        { KeyAction::Take, "take", "Take: record, stop, play", "Gestures", chord('G') },
        { KeyAction::NewTake, "newTake", "Take: record new", "Gestures", chord('G', shift) },
        { KeyAction::DrawPath, "drawPath", "Draw a path", "Gestures", chord('P') },
    };
    return table;
}

const KeyActionInfo& keyActionInfo(KeyAction action)
{
    for (const auto& info : keyActions())
        if (info.action == action)
            return info;
    return keyActions().front();
}

std::optional<KeyAction> keyActionFromId(const juce::String& id)
{
    for (const auto& info : keyActions())
        if (id == info.id)
            return info.action;
    return std::nullopt;
}

KeyBindings::KeyBindings() { resetAll(); }

juce::KeyPress KeyBindings::get(KeyAction action) const { return keys[static_cast<std::size_t>(action)]; }

void KeyBindings::set(KeyAction action, const juce::KeyPress& key)
{
    if (action == KeyAction::Count)
        return;
    keys[static_cast<std::size_t>(action)] = key.isValid() ? normalise(key) : juce::KeyPress();
    ++version;
}

void KeyBindings::reset(KeyAction action) { set(action, keyActionInfo(action).defaultKey); }

void KeyBindings::resetAll()
{
    for (const auto& info : keyActions())
        keys[static_cast<std::size_t>(info.action)] = normalise(info.defaultKey);
    ++version;
}

bool KeyBindings::isDefault(KeyAction action) const
{
    const auto& d = keyActionInfo(action).defaultKey;
    const auto& k = get(action);
    return k.isValid() == d.isValid() && (! k.isValid() || same(k, d));
}

std::optional<KeyAction> KeyBindings::find(const juce::KeyPress& pressed) const
{
    if (! pressed.isValid())
        return std::nullopt;
    auto key = normalise(pressed);
    auto lookup = [this](const juce::KeyPress& k) -> std::optional<KeyAction> {
        for (const auto& info : keyActions())
            if (const auto& bound = get(info.action); bound.isValid() && same(bound, k))
                return info.action;
        return std::nullopt;
    };
    if (auto found = lookup(key))
        return found;
    const int flags = key.getModifiers().getRawFlags();
    if ((flags & juce::ModifierKeys::shiftModifier) != 0)
        if (auto found = lookup(chord(key.getKeyCode(), flags & ~juce::ModifierKeys::shiftModifier)))
            return found;
    if ((flags & juce::ModifierKeys::commandModifier) == 0 && (flags & kModifierMask) != 0)
        return lookup(chord(key.getKeyCode()));
    return std::nullopt;
}

std::vector<KeyAction> KeyBindings::usersOf(const juce::KeyPress& key, std::optional<KeyAction> except) const
{
    std::vector<KeyAction> out;
    if (! key.isValid())
        return out;
    for (const auto& info : keyActions())
        if (info.action != except && get(info.action).isValid() && same(get(info.action), key))
            out.push_back(info.action);
    return out;
}

std::vector<KeyAction> KeyBindings::conflicts(KeyAction action) const { return usersOf(get(action), action); }

juce::var KeyBindings::toVar() const
{
    auto* o = new juce::DynamicObject();
    for (const auto& info : keyActions())
        if (! isDefault(info.action))
            o->setProperty(info.id, toText(get(info.action)));
    return juce::var(o);
}

void KeyBindings::fromVar(const juce::var& stored)
{
    resetAll();
    if (const auto* o = stored.getDynamicObject())
        for (const auto& prop : o->getProperties())
            if (const auto action = keyActionFromId(prop.name.toString()))
                if (const auto key = fromText(prop.value.toString()); key.isValid() || prop.value.toString().trim() == "none")
                    keys[static_cast<std::size_t>(*action)] = key;
    ++version;
}

void KeyBindings::load(juce::PropertiesFile& settings) { fromVar(juce::JSON::parse(settings.getValue(kSettingsKey))); }

void KeyBindings::save(juce::PropertiesFile& settings) const
{
    settings.setValue(kSettingsKey, juce::JSON::toString(toVar(), true));
    settings.saveIfNeeded();
}

juce::String KeyBindings::label(KeyAction action) const
{
#if JUCE_MAC
    return describe(get(action), true);
#else
    return describe(get(action), false);
#endif
}

juce::String KeyBindings::text(KeyAction action) const
{
    const auto& k = get(action);
    return k.isValid() ? describe(k, false) : juce::String();
}

juce::String KeyBindings::hint(KeyAction action) const
{
    const auto& k = get(action);
    return k.isValid() ? " (" + describe(k, false) + ")" : juce::String();
}

juce::KeyPress KeyBindings::normalise(const juce::KeyPress& key)
{
    if (! key.isValid())
        return {};
    int code = key.getKeyCode();
    if (code >= 'a' && code <= 'z')
        code = code - 'a' + 'A';
    if (code == '=')
        code = '+';
    return chord(code, key.getModifiers().getRawFlags() & kModifierMask);
}

bool KeyBindings::same(const juce::KeyPress& a, const juce::KeyPress& b)
{
    const auto x = normalise(a), y = normalise(b);
    return x.getKeyCode() == y.getKeyCode() && x.getModifiers().getRawFlags() == y.getModifiers().getRawFlags();
}

juce::String KeyBindings::reservedUse(const juce::KeyPress& key)
{
    const auto k = normalise(key);
    const int flags = k.getModifiers().getRawFlags() & ~juce::ModifierKeys::shiftModifier;
    if (flags != 0)
        return {};
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::tabKey)
        return "Tab moves between the device tabs";
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        return "The arrow keys move the terrain cursor";
    if (code >= '1' && code <= '9')
        return "1 to 9 glide to the scenes";
    return {};
}

juce::String KeyBindings::describe(const juce::KeyPress& key, bool symbols)
{
    if (! key.isValid())
        return symbols ? juce::String() : juce::String("None");
    const auto k = normalise(key);
    const auto mods = k.getModifiers();
    juce::StringArray parts;
#if JUCE_MAC
    if (mods.isCtrlDown())
        parts.add(symbols ? juce::String::fromUTF8("\xe2\x8c\x83") : juce::String("Ctrl"));
    if (mods.isAltDown())
        parts.add(symbols ? juce::String::fromUTF8("\xe2\x8c\xa5") : juce::String("Option"));
    if (mods.isShiftDown())
        parts.add(symbols ? juce::String::fromUTF8("\xe2\x87\xa7") : juce::String("Shift"));
    if (mods.isCommandDown())
        parts.add(symbols ? juce::String::fromUTF8("\xe2\x8c\x98") : juce::String("Cmd"));
#else
    if (mods.isShiftDown())
        parts.add("Shift");
    if (mods.isCtrlDown())
        parts.add("Ctrl");
    if (mods.isAltDown())
        parts.add("Alt");
#endif
    juce::String name;
    if (const auto* n = nameFor(k.getKeyCode()))
        name = juce::String::fromUTF8(symbols ? n->symbol : n->word);
    else if (printable(k.getKeyCode()))
        name = juce::String::charToString(static_cast<juce::juce_wchar>(k.getKeyCode()));
    else
        name = k.getTextDescription();
    parts.add(name);
    return parts.joinIntoString(symbols ? "" : "+");
}

juce::String KeyBindings::toText(const juce::KeyPress& key)
{
    if (! key.isValid())
        return "none";
    const auto k = normalise(key);
    const auto mods = k.getModifiers();
    juce::StringArray parts;
    if (mods.isCommandDown())
        parts.add("cmd");
    if (mods.isCtrlDown() && ! mods.isCommandDown())
        parts.add("ctrl");
    if (mods.isAltDown())
        parts.add("alt");
    if (mods.isShiftDown())
        parts.add("shift");
    if (const auto* n = nameFor(k.getKeyCode()))
        parts.add(n->text);
    else if (printable(k.getKeyCode()))
        parts.add(juce::String::charToString(static_cast<juce::juce_wchar>(k.getKeyCode())));
    else
        parts.add("code" + juce::String(k.getKeyCode()));
    return parts.joinIntoString(" ");
}

juce::KeyPress KeyBindings::fromText(const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens(text.trim(), " ", {});
    if (parts.isEmpty() || parts[parts.size() - 1] == "none")
        return {};
    int mods = 0;
    for (int i = 0; i < parts.size() - 1; ++i)
    {
        if (parts[i] == "cmd")
            mods |= juce::ModifierKeys::commandModifier;
        else if (parts[i] == "ctrl")
            mods |= juce::ModifierKeys::ctrlModifier;
        else if (parts[i] == "alt")
            mods |= juce::ModifierKeys::altModifier;
        else if (parts[i] == "shift")
            mods |= juce::ModifierKeys::shiftModifier;
        else
            return {};
    }
    const auto last = parts[parts.size() - 1];
    for (const auto& n : keyNames())
        if (last == n.text)
            return normalise(chord(n.code, mods));
    if (last.startsWith("code") && last.length() > 4 && last.substring(4).containsOnly("0123456789"))
        return normalise(chord(last.substring(4).getIntValue(), mods));
    if (last.length() == 1 && printable(static_cast<int>(last[0])))
        return normalise(chord(static_cast<int>(last[0]), mods));
    return {};
}
}
