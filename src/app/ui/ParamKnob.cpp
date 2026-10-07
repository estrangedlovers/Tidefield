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

    name = spec.name;
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, theme::textDim);
    label.setFont(juce::FontOptions(13.0f));

    addAndMakeVisible(slider);
    addAndMakeVisible(label);
}

void ParamKnob::setDisplay(const juce::String& newName, std::function<juce::String(double)> format)
{
    name = newName;
    refreshLabel();
    slider.setTextValueSuffix({}); // the formatter owns units
    slider.textFromValueFunction = std::move(format);
    slider.updateText();
}

void ParamKnob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
    else if (e.mods.isAltDown())
        engine.post(engine::ControlEvent::releaseParam(engine::idx(param)));
}

void ParamKnob::showMenu()
{
    const auto i = engine::idx(param);
    auto* midi = knobContext().midi;
    juce::PopupMenu menu;
    if (midi != nullptr)
    {
        const bool learningThis = midi->getLearnParam() == i;
        menu.addItem(learningThis ? "Cancel MIDI learn" : "MIDI learn (move a controller)", [midi, i, learningThis] {
            learningThis ? midi->cancelLearn() : midi->learnParam(i);
        });
        for (int b : midi->bindingsFor(i))
        {
            const auto text = midi->describe(midi->getBindings()[static_cast<std::size_t>(b)]);
            menu.addItem("Forget " + juce::String(text.substr(0, text.find(" ->"))), [midi, i] { midi->clearParam(i); });
            break; // one entry clears them all
        }
        menu.addSeparator();
    }
    if (liveNow)
        menu.addItem("Release to terrain", [this, i] { engine.post(engine::ControlEvent::releaseParam(i)); });
    menu.addItem("Reset to default", [this] {
        engine.setParam(param, engine.getRegistry().spec(param).defaultValue);
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void ParamKnob::refreshLabel()
{
    juce::String text = name;
    if (pickup > 0)
        text << " " << juce::String(juce::CharPointer_UTF8("\xe2\x96\xb2")); // controller is above: turn it down
    else if (pickup < 0)
        text << " " << juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc"));
    label.setText(text, juce::dontSendNotification);
}

void ParamKnob::update(const engine::TelemetryFrame& frame)
{
    const auto i = engine::idx(param);
    if (! slider.isMouseButtonDown())
        slider.setValue(frame.paramTargets[i], juce::dontSendNotification);

    const bool isLive = frame.live[i] != 0;
    liveNow = isLive;
    const bool learning = knobContext().midi != nullptr && knobContext().midi->getLearnParam() == i;
    if (isLive != wasLive || learning != wasLearning)
    {
        wasLive = isLive;
        wasLearning = learning;
        const auto fill = learning ? theme::warn : (isLive ? theme::live : theme::accent);
        slider.setColour(juce::Slider::rotarySliderFillColourId, fill);
        label.setColour(juce::Label::textColourId, learning ? theme::warn : (isLive ? theme::live : theme::textDim));
    }
    if (frame.midiPickup[i] != pickup)
    {
        pickup = frame.midiPickup[i];
        refreshLabel();
    }
}

void ParamKnob::resized()
{
    auto b = getLocalBounds();
    label.setBounds(b.removeFromTop(16));
    slider.setBounds(b);
}

} // namespace tf::app
