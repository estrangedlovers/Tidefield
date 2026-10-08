#include "Style.h"

#include "Logo.h"

#include <BinaryData.h>

namespace tf::app::gui {

namespace {

using C = juce::Colour;

const Palette kSlate {
    C(0xff394346), C(0xff465154), C(0xff556165), C(0xff4f5a5e), C(0xff1b2528), C(0xff2c3a3e), C(0xff2f383b), C(0xff1b2528),
    C(0xfff4f3ed), C(0xffc2ccca), C(0xff919f9e), C(0xfff4f3ed), C(0xffa9b8b6),
    C(0xffe2b47c), C(0xff86cfc5), C(0xfff0d48c), C(0xffe39fc0), C(0xffe7796b), C(0xffa8cf8c),
    { { C(0xffe2b47c), C(0xff86cfc5), C(0xffb3a6dc), C(0xffe8d390), C(0xff8fbadf),
        C(0xffe2a0b9), C(0xffa8c99b), C(0xffe4907e), C(0xffa6dbe3), C(0xffc8a587) } },
};

const Palette kPaper {
    C(0xffdfe2dd), C(0xffeef0eb), C(0xffd8ddd7), C(0xffe5e8e3), C(0xff1b2528), C(0xff2c3a3e), C(0xffc6ccc7), C(0xffc3cac5),
    C(0xff20333a), C(0xff4c6064), C(0xff7b8b8c), C(0xfff4f3ed), C(0xffa9b8b6),
    C(0xffc0843f), C(0xff3d8c84), C(0xffbf9431), C(0xffbf5c8b), C(0xffc65445), C(0xff63904a),
    { { C(0xffc98d4c), C(0xff4a9d94), C(0xff8573bf), C(0xffb9a043), C(0xff4f88bd),
        C(0xffc0688d), C(0xff6f9a5f), C(0xffc7664f), C(0xff4ca2b0), C(0xffa27b5a) } },
};

Theme current = Theme::slate;

} // namespace

const Palette& palette() { return current == Theme::paper ? kPaper : kSlate; }
const Palette& displayPalette() { return kSlate; }
Theme theme() { return current; }
void setTheme(Theme t) { current = t; }

juce::Font font(float size, int weight)
{
    static const auto regular = juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static const auto medium = juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    static const auto semibold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    const auto& face = weight >= 600 ? semibold : (weight >= 500 ? medium : regular);
    return juce::Font(juce::FontOptions(face).withHeight(size));
}

juce::Font brandFont(float size)
{
    static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::QuicksandMedium_ttf, BinaryData::QuicksandMedium_ttfSize);
    return juce::Font(juce::FontOptions(face).withHeight(size));
}

void drawWordmark(juce::Graphics& g, juce::Rectangle<float> r, float markSize, juce::Colour textColour)
{
    logo::drawMark(g, r.removeFromLeft(markSize).withSizeKeepingCentre(markSize, markSize));
    r.removeFromLeft(markSize * 0.42f);
    g.setColour(textColour);
    // Quicksand sits low in its box; nudge it up so the x-height centres on the mark.
    g.setFont(brandFont(markSize * 1.02f));
    g.drawText("tidefield", r.translated(0.0f, -markSize * 0.06f), juce::Justification::centredLeft, false);
}

void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour titleColour)
{
    g.setColour(colour::panel());
    g.fillRoundedRectangle(r, metric::radius);
    if (title.isNotEmpty())
    {
        auto bar = r.removeFromTop(static_cast<float>(metric::header));
        g.setColour(colour::header());
        juce::Path p;
        p.addRoundedRectangle(bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(), metric::radius, metric::radius, true, true, false, false);
        g.fillPath(p);
        g.setColour(titleColour);
        g.setFont(caps());
        g.drawText(title.toUpperCase(), bar.reduced(static_cast<float>(metric::pad), 0.0f), juce::Justification::centredLeft, true);
    }
}

void drawWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(colour::well());
    g.fillRoundedRectangle(r, metric::radius);
    g.setColour(colour::wellLine());
    g.drawRoundedRectangle(r.reduced(0.5f), metric::radius, 1.0f);
}

} // namespace tf::app::gui

namespace tf::app::gui {

LookAndFeel::LookAndFeel() { applyPalette(); }

void LookAndFeel::applyPalette()
{
    using C = juce::LookAndFeel_V4::ColourScheme;
    setColourScheme(C(colour::panel(), colour::window(), colour::panelHi(), colour::line(), colour::text(), colour::panelHi(), colour::well(),
                      colour::accent(), colour::text()));
    setColour(juce::PopupMenu::backgroundColourId, colour::panelHi());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::accent());
    setColour(juce::PopupMenu::highlightedTextColourId, colour::well());
    setColour(juce::PopupMenu::textColourId, colour::text());
    setColour(juce::PopupMenu::headerTextColourId, colour::textFaint());
    setColour(juce::ComboBox::backgroundColourId, colour::panelHi());
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, colour::textDim());
    setColour(juce::ComboBox::textColourId, colour::text());
    setColour(juce::TextEditor::backgroundColourId, colour::well());
    setColour(juce::TextEditor::outlineColourId, colour::line());
    setColour(juce::TextEditor::focusedOutlineColourId, colour::accent());
    setColour(juce::TextEditor::highlightColourId, colour::accentDim());
    setColour(juce::TextButton::buttonColourId, colour::panelHi());
    setColour(juce::TextButton::buttonOnColourId, colour::accent());
    setColour(juce::TextButton::textColourOffId, colour::text());
    setColour(juce::TextButton::textColourOnId, colour::well());
    setColour(juce::AlertWindow::backgroundColourId, colour::panel());
    setColour(juce::AlertWindow::textColourId, colour::text());
    setColour(juce::ListBox::backgroundColourId, colour::well());
    setColour(juce::Slider::thumbColourId, colour::text());
    setColour(juce::Slider::trackColourId, colour::accent());
    setColour(juce::Slider::backgroundColourId, colour::well());
    setColour(juce::Slider::textBoxTextColourId, colour::text());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, colour::text());
    setColour(juce::ToggleButton::textColourId, colour::text());
    setColour(juce::ToggleButton::tickColourId, colour::accent());
    setColour(juce::ResizableWindow::backgroundColourId, colour::window());
    setColour(juce::ScrollBar::thumbColourId, colour::lift(colour::panelHi(), 0.2f));
    setColour(juce::TooltipWindow::backgroundColourId, colour::panelHi());
    setColour(juce::TooltipWindow::textColourId, colour::text());
    setColour(juce::DocumentWindow::textColourId, colour::text());
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    // Everything in Inter, whatever a stock component asks for.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return font(f.getHeight(), f.isBold() ? 600 : 400).getTypefacePtr();
    return juce::LookAndFeel_V4::getTypefaceForFont(f);
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
    g.setColour(box.isMouseOver(true) ? colour::lift(colour::panelHi(), 0.07f) : colour::panelHi());
    g.fillRoundedRectangle(r, metric::radius);
    juce::Path arrow;
    const float cx = r.getRight() - 11.0f, cy = r.getCentreY();
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour(colour::textDim());
    g.fillPath(arrow);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto c = b.getToggleState() ? colour::accent() : bg;
    if (down)
        c = colour::lift(c, 0.15f);
    else if (over)
        c = colour::lift(c, 0.07f);
    g.setColour(c);
    g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(0.5f), metric::radius);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style,
                                   juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    auto r = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
    auto track = r.withSizeKeepingCentre(r.getWidth(), 6.0f);
    g.setColour(colour::track());
    g.fillRoundedRectangle(track, 3.0f);
    g.setColour(colour::accent());
    g.fillRoundedRectangle(track.withRight(pos), 3.0f);
    g.setColour(colour::text());
    g.fillRoundedRectangle(juce::Rectangle<float>(4.0f, 12.0f).withCentre({ pos, r.getCentreY() }), 1.5f);
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(colour::panelHi());
    g.setColour(colour::line());
    g.drawRect(0, 0, w, h);
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize,
                                bool over, bool down)
{
    const auto thumb = vertical ? juce::Rectangle<int>(x, thumbStart, w, thumbSize) : juce::Rectangle<int>(thumbStart, y, thumbSize, h);
    g.setColour(colour::lift(colour::panelHi(), over || down ? 0.35f : 0.18f));
    g.fillRoundedRectangle(thumb.toFloat().reduced(1.5f), 3.0f);
}

} // namespace tf::app::gui
