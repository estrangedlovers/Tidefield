#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <functional>

namespace tf::app::gui {

enum class Theme { slate, paper };

struct Palette
{
    juce::Colour window, panel, panelHi, header, well, wellLine, line, track;
    juce::Colour text, textDim, textFaint, wellText, wellTextDim;
    juce::Colour accent, tide, live, learn, warn, good;
    std::array<juce::Colour, 10> scenes;
};

const Palette& palette();
const Palette& displayPalette();
Theme theme();
void setTheme(Theme t);

namespace colour {
inline juce::Colour window() { return palette().window; }
inline juce::Colour panel() { return palette().panel; }
inline juce::Colour panelHi() { return palette().panelHi; }
inline juce::Colour header() { return palette().header; }
inline juce::Colour well() { return palette().well; }
inline juce::Colour wellLine() { return palette().wellLine; }
inline juce::Colour line() { return palette().line; }
inline juce::Colour track() { return palette().track; }
inline juce::Colour text() { return palette().text; }
inline juce::Colour textDim() { return palette().textDim; }
inline juce::Colour textFaint() { return palette().textFaint; }
inline juce::Colour wellText() { return palette().wellText; }
inline juce::Colour wellTextDim() { return palette().wellTextDim; }
inline juce::Colour accent() { return palette().accent; }
inline juce::Colour accentDim() { return palette().accent.withAlpha(0.33f); }
inline juce::Colour tide() { return palette().tide; }
inline juce::Colour live() { return palette().live; }
inline juce::Colour learn() { return palette().learn; }
inline juce::Colour warn() { return palette().warn; }
inline juce::Colour good() { return palette().good; }
inline juce::Colour lift(juce::Colour c, float amount) { return theme() == Theme::paper ? c.darker(amount * 0.6f) : c.brighter(amount); }
inline juce::Colour forScene(int i) { return palette().scenes[static_cast<std::size_t>(((i % 10) + 10) % 10)]; }
} // namespace colour

namespace display {
inline juce::Colour window() { return displayPalette().window; }
inline juce::Colour panel() { return displayPalette().panel; }
inline juce::Colour panelHi() { return displayPalette().panelHi; }
inline juce::Colour header() { return displayPalette().header; }
inline juce::Colour well() { return displayPalette().well; }
inline juce::Colour wellLine() { return displayPalette().wellLine; }
inline juce::Colour line() { return displayPalette().line; }
inline juce::Colour track() { return displayPalette().track; }
inline juce::Colour text() { return displayPalette().text; }
inline juce::Colour textDim() { return displayPalette().textDim; }
inline juce::Colour textFaint() { return displayPalette().textFaint; }
inline juce::Colour wellText() { return displayPalette().wellText; }
inline juce::Colour wellTextDim() { return displayPalette().wellTextDim; }
inline juce::Colour accent() { return displayPalette().accent; }
inline juce::Colour accentDim() { return displayPalette().accent.withAlpha(0.33f); }
inline juce::Colour tide() { return displayPalette().tide; }
inline juce::Colour live() { return displayPalette().live; }
inline juce::Colour learn() { return displayPalette().learn; }
inline juce::Colour warn() { return displayPalette().warn; }
inline juce::Colour good() { return displayPalette().good; }
inline juce::Colour forScene(int i) { return displayPalette().scenes[static_cast<std::size_t>(((i % 10) + 10) % 10)]; }
} // namespace display

namespace metric {
inline constexpr int gap = 6;          // between panels
inline constexpr int pad = 10;         // inside panels
inline constexpr float radius = 3.0f;
inline constexpr float tileRadius = 0.225f;
inline constexpr int header = 22;      // panel title bar
inline constexpr int knobW = 58, knobH = 70;
} // namespace metric

inline float tileCorner(juce::Rectangle<float> r) { return std::min(12.0f, std::min(r.getWidth(), r.getHeight()) * metric::tileRadius); }

/** Inter, embedded (one family, three weights). */
juce::Font font(float size, int weight = 400);
inline juce::Font caps(float size = 10.5f) { return font(size, 600).withExtraKerningFactor(0.06f); }
/** Quicksand Medium, embedded, for the lowercase wordmark only. */
juce::Font brandFont(float size);
/** The mark and the lowercase name, left-aligned and vertically centred in r. */
void drawWordmark(juce::Graphics& g, juce::Rectangle<float> r, float markSize, juce::Colour textColour);

/** A flat panel with an optional title bar, the basic container everywhere. */
void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {}, juce::Colour titleColour = colour::textDim());

/** A dark display well (terrain, meters, readouts). */
void drawWell(juce::Graphics& g, juce::Rectangle<float> r);

/** Shows a menu at the mouse. The callback runs only if an item was chosen and
    `owner` still exists: a plugin window can close while a menu is open, and JUCE
    still calls back (with 0) after it is dismissed. */
inline void showMenu(juce::PopupMenu& menu, juce::Component* owner, std::function<void(int)> chosen)
{
    juce::Component::SafePointer<juce::Component> safe(owner);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(), [safe, chosen = std::move(chosen)](int r) {
        if (safe != nullptr && r != 0)
            chosen(r);
    });
}

/** Runs on the message thread later, only if `owner` still exists. */
inline void later(juce::Component* owner, std::function<void()> f)
{
    juce::Component::SafePointer<juce::Component> safe(owner);
    juce::MessageManager::callAsync([safe, f = std::move(f)] {
        if (safe != nullptr)
            f();
    });
}

} // namespace tf::app::gui

namespace tf::app::gui {

/** Menus, combo boxes, sliders, text fields, scrollbars and dialogs in the same
    flat style as the custom controls. Installed as the default for the whole app. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void applyPalette();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& f) override;
    juce::Font getPopupMenuFont() override { return font(13.0f, 500); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(12.0f, 500); }
    juce::Font getLabelFont(juce::Label&) override { return font(12.0f, 500); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(12.0f, 600); }
    juce::Font getAlertWindowTitleFont() override { return font(15.0f, 600); }
    juce::Font getAlertWindowMessageFont() override { return font(13.0f); }
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
};

} // namespace tf::app::gui
