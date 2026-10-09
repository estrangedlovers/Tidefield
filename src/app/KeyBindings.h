#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>
#include <vector>

namespace tf::app {
enum class KeyAction
{
    NewSession, Open, Save, SaveAs, Undo, Redo, Settings, ZoomIn, ZoomOut, ZoomReset, Projector,
    Fade, Panic, Capture, Release, Record, NoteMode, Swell, Hush, Slow, Freeze, InputFreeze, Cycles,
    LoopRecord, LoopClear, Catch, Take, NewTake, DrawPath, Count
};

inline constexpr int kNumKeyActions = static_cast<int>(KeyAction::Count);

struct KeyActionInfo
{
    KeyAction action;
    const char* id;
    const char* name;
    const char* group;
    juce::KeyPress defaultKey;
    bool hold = false;
};

const std::vector<KeyActionInfo>& keyActions();
const KeyActionInfo& keyActionInfo(KeyAction action);
std::optional<KeyAction> keyActionFromId(const juce::String& id);

class KeyBindings
{
public:
    KeyBindings();

    juce::KeyPress get(KeyAction action) const;
    void set(KeyAction action, const juce::KeyPress& key);
    void reset(KeyAction action);
    void resetAll();
    bool isDefault(KeyAction action) const;

    std::optional<KeyAction> find(const juce::KeyPress& pressed) const;
    std::vector<KeyAction> conflicts(KeyAction action) const;
    std::vector<KeyAction> usersOf(const juce::KeyPress& key, std::optional<KeyAction> except = std::nullopt) const;

    juce::var toVar() const;
    void fromVar(const juce::var& stored);
    void load(juce::PropertiesFile& settings);
    void save(juce::PropertiesFile& settings) const;

    juce::String label(KeyAction action) const;
    juce::String hint(KeyAction action) const;
    juce::String text(KeyAction action) const;
    int getVersion() const noexcept { return version; }

    static juce::KeyPress normalise(const juce::KeyPress& key);
    static bool same(const juce::KeyPress& a, const juce::KeyPress& b);
    static juce::String reservedUse(const juce::KeyPress& key);
    static juce::String describe(const juce::KeyPress& key, bool symbols);
    static juce::String toText(const juce::KeyPress& key);
    static juce::KeyPress fromText(const juce::String& text);

    static constexpr const char* kSettingsKey = "shortcuts";

private:
    std::array<juce::KeyPress, kNumKeyActions> keys;
    int version = 0;
};
}
