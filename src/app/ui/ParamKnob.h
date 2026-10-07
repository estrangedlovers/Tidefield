#pragma once

#include <engine/Engine.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tf::app {

/** A rotary control bound to one engine parameter. It writes through Engine::post,
    follows the parameter's target from telemetry (so it moves with the terrain), and
    turns gold while the parameter is held in the live layer. Double-click resets to
    the default; Alt-click releases it back to the terrain. */
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
    juce::Slider slider;
    juce::Label label;
    bool wasLive = false;
};

} // namespace tf::app
