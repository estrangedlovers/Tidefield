#pragma once

#include <engine/Engine.h>
#include <engine/midi/MidiManager.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tf::app {

/** Shared services every knob may use (set once by the main window). */
struct KnobContext
{
    engine::MidiManager* midi = nullptr;
};

inline KnobContext& knobContext()
{
    static KnobContext context;
    return context;
}

/** A rotary control bound to one engine parameter. It writes through Engine::post,
    follows the parameter's target from telemetry (so it moves with the terrain), and
    turns gold while the parameter is held in the live layer. Double-click resets to
    the default; Alt-click releases it back to the terrain; right-click for MIDI learn.
    While MIDI soft takeover waits for a controller, an arrow shows which way to turn
    it. */
class ParamKnob final : public juce::Component
{
public:
    ParamKnob(engine::Engine& engine, engine::P param);

    void update(const engine::TelemetryFrame& frame);

    /** For FX slot controls: the loaded processor decides name and formatting. */
    void setDisplay(const juce::String& name, std::function<juce::String(double)> format);

    engine::P getParam() const noexcept { return param; }
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    engine::Engine& engine;
    engine::P param;
    void showMenu();
    void refreshLabel();

    juce::Slider slider;
    juce::Label label;
    juce::String name;
    bool wasLive = false;
    bool wasLearning = false;
    std::int8_t pickup = 0;
    bool liveNow = false;
};

} // namespace tf::app
