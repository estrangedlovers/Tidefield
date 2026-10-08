#pragma once

#include "Model.h"
#include "Style.h"

#include <functional>

namespace tf::app::gui {
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
    int framesSincePaint = 0;
};

class Knob final : public ParamComponent
{
public:
    using ParamComponent::ParamComponent;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    std::function<juce::String(float)> formatter;
    void tick() override;

private:
    juce::String lastText;
    float shownMod = 0.0f;
};

class Fader final : public ParamComponent
{
public:
    using ParamComponent::ParamComponent;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
};

class Toggle final : public ParamComponent
{
public:
    Toggle(Model& m, engine::P p, juce::String text, juce::String help = {}, juce::Colour on = colour::accent());
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::String text;
    juce::Colour onColour;
};

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

class Pad final : public juce::Component, public Animated
{
public:
    Pad(Model& m, juce::String title, juce::String sub, juce::Colour colour, juce::String help);
    ~Pad() override;

    std::function<void()> onPress, onRelease;
    std::function<void()> onMenu;
    std::function<float()> level;
    std::function<bool()> lit;
    std::function<juce::String()> subText;
    juce::String keyCap;

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

class FlatButton final : public juce::Button
{
public:
    explicit FlatButton(const juce::String& text, juce::Colour onColour = colour::accent());
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    void setHelp(Model* m, juce::String h) { model = m; help = std::move(h); }
    void mouseEnter(const juce::MouseEvent& e) override;
    juce::Colour onColour;

private:
    Model* model = nullptr;
    juce::String help;
};
}
