#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tf::app::gui {

/** Tidefield's native look: mid-grey working surfaces in the spirit of a studio
    instrument (flat panels, 1 px lines, compact type, one bright accent), a dark
    "well" for the terrain and displays so light and colour read against it, and a
    vivid colour per scene, the way clips are coloured in a session view. */
namespace colour {
inline const juce::Colour window { 0xff46474b };     // the frame between panels
inline const juce::Colour panel { 0xff55565b };      // panels and device boxes
inline const juce::Colour panelHi { 0xff65666c };    // raised: buttons, hovered rows
inline const juce::Colour header { 0xff5e5f64 };     // panel title bars
inline const juce::Colour well { 0xff1d2429 };       // terrain, meters, displays
inline const juce::Colour wellLine { 0xff303b42 };
inline const juce::Colour line { 0xff3a3b3f };       // separators
inline const juce::Colour text { 0xfff0efea };
inline const juce::Colour textDim { 0xffc4c3bd };
inline const juce::Colour textFaint { 0xff9a9993 };

inline const juce::Colour accent { 0xffff9f43 };     // orange: selection, values, on
inline const juce::Colour accentDim { 0x55ff9f43 };
inline const juce::Colour tide { 0xff3fd0c5 };       // teal: motion, the terrain cursor
inline const juce::Colour live { 0xffffd75e };       // yellow: held in the live layer
inline const juce::Colour learn { 0xffff6fb5 };      // pink: MIDI learn
inline const juce::Colour warn { 0xffff6b5e };       // red: panic, record, warnings
inline const juce::Colour good { 0xff9be05a };

/** Scene colours, in order (like clip colours). */
inline const std::array<juce::Colour, 10> scenes { {
    juce::Colour(0xffff9f43), juce::Colour(0xff3fd0c5), juce::Colour(0xffb08cff), juce::Colour(0xffffd75e),
    juce::Colour(0xff59b7ff), juce::Colour(0xffff7ab6), juce::Colour(0xff9be05a), juce::Colour(0xffff6b5e),
    juce::Colour(0xff7fe6ff), juce::Colour(0xffe6b37f),
} };

inline juce::Colour forScene(int i) { return scenes[static_cast<std::size_t>(((i % 10) + 10) % 10)]; }
} // namespace colour

namespace metric {
inline constexpr int gap = 6;          // between panels
inline constexpr int pad = 10;         // inside panels
inline constexpr float radius = 3.0f;  // corner radius: nearly square, like the rest of the studio
inline constexpr int header = 22;      // panel title bar
inline constexpr int knobW = 58, knobH = 70;
} // namespace metric

/** Inter, embedded (one family, three weights). */
juce::Font font(float size, int weight = 400);
inline juce::Font caps(float size = 10.5f) { return font(size, 600).withExtraKerningFactor(0.06f); }

/** A flat panel with an optional title bar, the basic container everywhere. */
void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {}, juce::Colour titleColour = colour::textDim);

/** A dark display well (terrain, meters, readouts). */
void drawWell(juce::Graphics& g, juce::Rectangle<float> r);

} // namespace tf::app::gui

namespace tf::app::gui {

/** Menus, combo boxes, sliders, text fields, scrollbars and dialogs in the same
    flat style as the custom controls. Installed as the default for the whole app. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
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
