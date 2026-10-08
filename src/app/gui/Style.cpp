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

const Palette kNightSwim {
    C(0xff10171d), C(0xff18222a), C(0xff23303a), C(0xff1d2830), C(0xff070c10), C(0xff16222a), C(0xff0b1116), C(0xff070c10),
    C(0xffe6f1f2), C(0xffa7bcc2), C(0xff6f8790), C(0xffe6f1f2), C(0xff8aa3aa),
    C(0xff4fe0d2), C(0xff7fb8ff), C(0xfff2e27a), C(0xffff8ac6), C(0xffff6f61), C(0xff8fe39a),
    { { C(0xff4fe0d2), C(0xff7fb8ff), C(0xffb49cff), C(0xfff2e27a), C(0xff5fd3ff),
        C(0xffff8ac6), C(0xff8fe39a), C(0xffff9a6b), C(0xffa8f0ff), C(0xffd6b98a) } },
};

const Palette kKelp {
    C(0xff232a22), C(0xff2e372c), C(0xff3c4739), C(0xff354033), C(0xff131911), C(0xff24301f), C(0xff1c2219), C(0xff131911),
    C(0xffeef0e2), C(0xffc3c9b2), C(0xff8f977f), C(0xffeef0e2), C(0xffa5ae94),
    C(0xffe0a84f), C(0xff8fcfb0), C(0xfff0d77a), C(0xffe79bb5), C(0xffe2735f), C(0xffb6d77a),
    { { C(0xffe0a84f), C(0xff8fcfb0), C(0xffc0a6d6), C(0xffe6d27f), C(0xff8db8cf),
        C(0xffe3a0a6), C(0xffb6d77a), C(0xffe08c63), C(0xffa5d8c4), C(0xffc7a77a) } },
};

const Palette kEmber {
    C(0xff2b2523), C(0xff37302d), C(0xff463d39), C(0xff3f3633), C(0xff171210), C(0xff2c2320), C(0xff211b19), C(0xff171210),
    C(0xfff3ebe2), C(0xffcbbdb0), C(0xff978a7e), C(0xfff3ebe2), C(0xffb1a294),
    C(0xfff08a4b), C(0xffe7c27a), C(0xfff5d67a), C(0xffe98fa8), C(0xfff0604f), C(0xffc3cf78),
    { { C(0xfff08a4b), C(0xffe7c27a), C(0xffd99a8f), C(0xfff5d67a), C(0xffc9a98a),
        C(0xffe98fa8), C(0xffc3cf78), C(0xffff7a5c), C(0xfff2b48a), C(0xffb89a7a) } },
};

const Palette kGraphite {
    C(0xff3e3e3e), C(0xff4b4b4b), C(0xff5a5a5a), C(0xff545454), C(0xff1e1e1e), C(0xff313131), C(0xff333333), C(0xff1e1e1e),
    C(0xfff0f0f0), C(0xffc4c4c4), C(0xff969696), C(0xfff0f0f0), C(0xffababab),
    C(0xffffa64d), C(0xff5fc9e0), C(0xffffd75e), C(0xffff7ab6), C(0xffff5f57), C(0xff9be05a),
    { { C(0xffffa64d), C(0xff5fc9e0), C(0xffb08cff), C(0xffffd75e), C(0xff59b7ff),
        C(0xffff7ab6), C(0xff9be05a), C(0xffff6b5e), C(0xff7fe6ff), C(0xffe6b37f) } },
};

const Palette kHeather {
    C(0xff352f3b), C(0xff413a48), C(0xff504858), C(0xff494151), C(0xff1a161e), C(0xff2d2733), C(0xff2a2530), C(0xff1a161e),
    C(0xfff1edf3), C(0xffc9c0cf), C(0xff978d9e), C(0xfff1edf3), C(0xffada3b4),
    C(0xffe9a3c9), C(0xffa7b8e8), C(0xfff0d690), C(0xffd8a6ff), C(0xffec7b74), C(0xffb4d39a),
    { { C(0xffe9a3c9), C(0xffa7b8e8), C(0xffc4a9ef), C(0xfff0d690), C(0xff9ec9e6),
        C(0xfff2a6a0), C(0xffb4d39a), C(0xffe8a07e), C(0xffb8e0e0), C(0xffcbae94) } },
};

const Palette kPaper {
    C(0xffdfe2dd), C(0xffeef0eb), C(0xffd8ddd7), C(0xffe5e8e3), C(0xff1b2528), C(0xff2c3a3e), C(0xffc6ccc7), C(0xffc3cac5),
    C(0xff20333a), C(0xff4c6064), C(0xff7b8b8c), C(0xfff4f3ed), C(0xffa9b8b6),
    C(0xffc0843f), C(0xff3d8c84), C(0xffbf9431), C(0xffbf5c8b), C(0xffc65445), C(0xff63904a),
    { { C(0xffc98d4c), C(0xff4a9d94), C(0xff8573bf), C(0xffb9a043), C(0xff4f88bd),
        C(0xffc0688d), C(0xff6f9a5f), C(0xffc7664f), C(0xff4ca2b0), C(0xffa27b5a) } },
    true,
};

const Palette kDune {
    C(0xffe6ddd0), C(0xfff3ece2), C(0xffe2d6c6), C(0xffece3d7), C(0xff171210), C(0xff2c2320), C(0xffd3c7b7), C(0xffd6cab9),
    C(0xff3a2c22), C(0xff6b5848), C(0xff9a8775), C(0xfff3ebe2), C(0xffb1a294),
    C(0xffc2643c), C(0xff3f8a83), C(0xffb88d2c), C(0xffb85a83), C(0xffc0473b), C(0xff6a8b42),
    { { C(0xffc2643c), C(0xff3f8a83), C(0xff8a6bb0), C(0xffb88d2c), C(0xff4c7fae),
        C(0xffb85a83), C(0xff6a8b42), C(0xffa35a3a), C(0xff3c95a0), C(0xff8f6d4f) } },
    true,
};

const Palette kSeaGlass {
    C(0xffd7e6e2), C(0xffe8f2ef), C(0xffcfe1dc), C(0xffddeae7), C(0xff070c10), C(0xff16222a), C(0xffbdd2cd), C(0xffbfd4cf),
    C(0xff163a3a), C(0xff3f6463), C(0xff6f908e), C(0xffe6f1f2), C(0xff8aa3aa),
    C(0xff1f8f86), C(0xff3a72b0), C(0xffb38f20), C(0xffb0508a), C(0xffc4503f), C(0xff4f8f45),
    { { C(0xff1f8f86), C(0xff3a72b0), C(0xff7b62b8), C(0xffb38f20), C(0xff2e86b8),
        C(0xffb0508a), C(0xff4f8f45), C(0xffc06a3f), C(0xff2f9fb0), C(0xff8d7350) } },
    true,
};

const Palette kDaylight {
    C(0xffe9e9e6), C(0xffffffff), C(0xffe4e4e0), C(0xfff2f2ef), C(0xff1e1e1e), C(0xff313131), C(0xffb8b8b2), C(0xffc8c8c2),
    C(0xff0d0d0d), C(0xff333333), C(0xff5c5c5c), C(0xfff0f0f0), C(0xffababab),
    C(0xffc25700), C(0xff006d77), C(0xff8a6a00), C(0xffa3006b), C(0xffc4001a), C(0xff2f7a00),
    { { C(0xffc25700), C(0xff006d77), C(0xff5b3fb0), C(0xff8a6a00), C(0xff005fae),
        C(0xffa3006b), C(0xff2f7a00), C(0xffb3261e), C(0xff00808f), C(0xff7a5230) } },
    true,
};

struct ThemeInfo
{
    Theme theme;
    const char* id;
    const char* name;
    const Palette& ui;
    const Palette& display;
};

const std::array<ThemeInfo, kThemes.size()> kThemeInfo { {
    { Theme::slate, "slate", "Slate", kSlate, kSlate },
    { Theme::nightSwim, "nightSwim", "Night swim", kNightSwim, kNightSwim },
    { Theme::kelp, "kelp", "Kelp", kKelp, kKelp },
    { Theme::ember, "ember", "Ember", kEmber, kEmber },
    { Theme::graphite, "graphite", "Graphite", kGraphite, kGraphite },
    { Theme::heather, "heather", "Heather", kHeather, kHeather },
    { Theme::paper, "paper", "Paper", kPaper, kSlate },
    { Theme::dune, "dune", "Dune", kDune, kEmber },
    { Theme::seaGlass, "seaGlass", "Sea glass", kSeaGlass, kNightSwim },
    { Theme::daylight, "daylight", "Daylight", kDaylight, kGraphite },
} };

const ThemeInfo& info(Theme t)
{
    for (const auto& i : kThemeInfo)
        if (i.theme == t)
            return i;
    return kThemeInfo.front();
}

Theme current = Theme::slate;
}

const Palette& palette() { return info(current).ui; }
const Palette& displayPalette() { return info(current).display; }
Theme theme() { return current; }
void setTheme(Theme t) { current = t; }
juce::String themeName(Theme t) { return info(t).name; }
juce::String themeId(Theme t) { return info(t).id; }
bool themeIsLight(Theme t) { return info(t).ui.light; }

Theme themeFromId(const juce::String& id)
{
    for (const auto& i : kThemeInfo)
        if (id == i.id)
            return i.theme;
    return Theme::slate;
}

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
}

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
}
