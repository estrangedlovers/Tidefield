#include "ParamKnob.h"

#include "Theme.h"

namespace tf::app {

ParamKnob::ParamKnob(engine::Engine& e, engine::P p) : engine(e), param(p)
{
    const auto& spec = engine.getRegistry().spec(param);

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 16);
    slider.setRange(spec.minValue, spec.maxValue, (spec.flags & engine::ParamFlag::kDiscrete) != 0 ? 1.0 : 0.0);
    if (spec.taper == engine::Taper::Log)
        slider.setSkewFactorFromMidPoint(std::sqrt(spec.minValue * spec.maxValue));
    slider.setValue(spec.defaultValue, juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, spec.defaultValue);
    slider.setTextValueSuffix(spec.unit.empty() ? juce::String() : " " + juce::String(spec.unit));
    slider.setNumDecimalPlacesToDisplay(spec.maxValue - spec.minValue > 100.0f ? 0 : 2);
    slider.onValueChange = [this] { engine.setParam(param, static_cast<float>(slider.getValue())); };
    slider.addMouseListener(this, false);
    if ((spec.flags & engine::ParamFlag::kTerrainBound) != 0)
        slider.setTooltip("Follows the terrain. Turning it holds it in the live layer (gold). Alt-click to release.");

    label.setText(spec.name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, theme::textDim);
    label.setFont(juce::FontOptions(13.0f));

    addAndMakeVisible(slider);
    addAndMakeVisible(label);
}

void ParamKnob::setDisplay(const juce::String& name, std::function<juce::String(double)> format)
{
    label.setText(name, juce::dontSendNotification);
    slider.setTextValueSuffix({}); // the formatter owns units
    slider.textFromValueFunction = std::move(format);
    slider.updateText();
}

void ParamKnob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isAltDown())
        engine.post(engine::ControlEvent::releaseParam(engine::idx(param)));
}

void ParamKnob::update(const engine::TelemetryFrame& frame)
{
    const auto i = engine::idx(param);
    if (! slider.isMouseButtonDown())
        slider.setValue(frame.paramTargets[i], juce::dontSendNotification);

    const bool isLive = frame.live[i] != 0;
    if (isLive != wasLive)
    {
        wasLive = isLive;
        slider.setColour(juce::Slider::rotarySliderFillColourId, isLive ? theme::live : theme::accent);
        label.setColour(juce::Label::textColourId, isLive ? theme::live : theme::textDim);
    }
}

void ParamKnob::resized()
{
    auto b = getLocalBounds();
    label.setBounds(b.removeFromTop(16));
    slider.setBounds(b);
}

} // namespace tf::app
