#pragma once

#include "Model.h"
#include "Style.h"

#include <functional>

namespace tf::app::gui {

/** Base for controls bound to one parameter. Shared behaviour, the same everywhere:
      drag            change (Shift: fine)
      double-click    default
      Alt/Opt-click   hand back to the terrain (when held in the live layer)
      right-click     MIDI learn / forget, release, reset
    Yellow means held in the live layer, pink means waiting for a MIDI controller, and
    an arrow shows which way to turn a controller that has not picked up yet. Hovering
    explains the control in the status bar. */
class ParamComponent : public juce::Component, public Animated
{
public:
    ParamComponent(Model& model, engine::P param, juce::String help = {});
    ~ParamComponent() override;

    engine::P getParam() const noexcept { return param; }
    void setLabel(const juce::String& l) { label = l; repaint(); }
    void tick() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

protected:
    /** Normalised 0..1 position of the current value (taper applied). */
    float norm() const { return model.toNorm(param, model.value(param)); }
    void setNorm(float n) { model.set(param, model.fromNorm(param, juce::jlimit(0.0f, 1.0f, n))); }
    juce::Colour valueColour() const;
    juce::String valueText() const { return model.format(param, model.value(param)); }
    bool dragging = false;
    float dragStartNorm = 0.0f;

    Model& model;
    engine::P param;
    juce::String label, help;

private:
    float lastValue = -1.0e9f;
    int lastFlags = -1;
};

/** Rotary: a 270-degree arc with the value under it. */
class Knob final : public ParamComponent
{
public:
    using ParamComponent::ParamComponent;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    /** Overrides the formatted value (FX slot controls format through their processor). */
    std::function<juce::String(float)> formatter;
    void tick() override;

private:
    juce::String lastText; // a formatter can change its text without the value moving (tempo sync)
};

/** Tall vertical fader for the performance macros. */
class Fader final : public ParamComponent
{
public:
    using ParamComponent::ParamComponent;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
};

/** On/off switch for a discrete 0/1 parameter. */
class Toggle final : public ParamComponent
{
public:
    Toggle(Model& m, engine::P p, juce::String text, juce::String help = {}, juce::Colour on = colour::accent);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::String text;
    juce::Colour onColour;
};

/** A row (or grid) of choices for a discrete parameter. */
class Choice final : public ParamComponent
{
public:
    Choice(Model& m, engine::P p, int columns = 0, juce::String help = {});
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void setCaptions(juce::StringArray c) { captions = std::move(c); }
    int preferredHeight(int width) const;

private:
    int indexAt(juce::Point<int> p) const;
    juce::Rectangle<int> cell(int i) const;
    juce::StringArray items, captions;
    int columns;
};

/** A performance pad: a big square-ish button, lit by a level, optionally held. */
class Pad final : public juce::Component, public Animated
{
public:
    Pad(Model& m, juce::String title, juce::String sub, juce::Colour colour, juce::String help);
    ~Pad() override;

    std::function<void()> onPress, onRelease;   // press/release (hold gestures)
    std::function<float()> level;               // 0..1 glow
    std::function<bool()> lit;                  // latched on
    std::function<juce::String()> subText;      // live sub-line

    void paint(juce::Graphics& g) override;
    void tick() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;

private:
    Model& model;
    juce::String title, sub, help;
    juce::Colour colour;
    float shownLevel = 0.0f;
    bool shownLit = false, pressed = false;
    juce::String shownSub;
};

/** A small flat text button. */
class FlatButton final : public juce::Button
{
public:
    explicit FlatButton(const juce::String& text, juce::Colour onColour = colour::accent);
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    void setHelp(Model* m, juce::String h) { model = m; help = std::move(h); }
    void mouseEnter(const juce::MouseEvent& e) override;
    juce::Colour onColour;

private:
    Model* model = nullptr;
    juce::String help;
};

} // namespace tf::app::gui
