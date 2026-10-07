#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tf::app::theme {

// Placeholder palette for the JUCE panels (phases 1-5). The React UI defines the
// real design tokens in phase 6.
inline const juce::Colour background { 0xff12151a };
inline const juce::Colour panel { 0xff1b2028 };
inline const juce::Colour panelRaised { 0xff222833 };
inline const juce::Colour text { 0xffc9d1d9 };
inline const juce::Colour textDim { 0x99c9d1d9 };
inline const juce::Colour accent { 0xff7fb4c9 };
inline const juce::Colour live { 0xffd9c38f };
inline const juce::Colour warn { 0xffc97f7f };
inline const juce::Colour button { 0xff2a313c };

/** Rotary knobs and buttons in the placeholder palette. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, background);
        setColour(juce::TextButton::buttonColourId, button);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::ComboBox::backgroundColourId, button);
        setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::PopupMenu::backgroundColourId, panelRaised);
        setColour(juce::Label::textColourId, text);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::rotarySliderFillColourId, accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, panelRaised);
        setColour(juce::TabbedButtonBar::tabTextColourId, textDim);
        setColour(juce::TabbedButtonBar::frontTextColourId, text);
        setColour(juce::TabbedComponent::backgroundColourId, background);
        setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ScrollBar::thumbColourId, panelRaised);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, accent.withAlpha(0.5f));
        setColour(juce::TextEditor::backgroundColourId, panelRaised);
        setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float start, float end,
                          juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = start + pos * (end - start);
        const float thickness = std::max(2.5f, radius * 0.12f);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, start, end, true);
        g.setColour(s.findColour(juce::Slider::rotarySliderOutlineColourId));
        g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc(centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, start, angle, true);
        g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto tip = centre.getPointOnCircumference(radius - thickness * 2.6f, angle);
        g.fillEllipse(juce::Rectangle<float>(thickness * 1.6f, thickness * 1.6f).withCentre(tip));
    }

    void drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced(2.0f, 3.0f);
        const bool front = b.isFrontTab();
        g.setColour(front ? panelRaised : (over ? panel : background));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(front ? text : textDim);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(b.getButtonText(), r, juce::Justification::centred);
    }

    int getTabButtonBestWidth(juce::TabBarButton&, int) override { return 110; }

    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override {}
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool over, bool down) override
    {
        auto c = colour;
        if (down)
            c = c.brighter(0.15f);
        else if (over)
            c = c.brighter(0.07f);
        g.setColour(b.isEnabled() ? c : c.withAlpha(0.4f));
        g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(0.5f), 5.0f);
    }
};

} // namespace tf::app::theme
