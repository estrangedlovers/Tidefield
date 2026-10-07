#pragma once

#include "../AppCore.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace tf::app::gui {

/** Anything that redraws from telemetry: ticked once per display frame. */
class Animated
{
public:
    virtual ~Animated() = default;
    virtual void tick() = 0;
};

/** The native UI's view of the instrument: parameter values (following the terrain,
    except while the performer's hand is on a control), formatting, actions, and the
    once-per-frame tick that drives every visual. Message thread only. */
class Model
{
public:
    explicit Model(AppCore& core);

    AppCore& core;
    engine::Engine& engine;
    const engine::ParamRegistry& registry;

    const engine::TelemetryFrame& frame() const noexcept { return core.latest(); }

    /** Once per display frame: ticks every registered Animated. */
    void tick();
    void add(Animated* a) { animated.push_back(a); }
    void remove(Animated* a) { animated.erase(std::remove(animated.begin(), animated.end(), a), animated.end()); }

    // --- Parameters -------------------------------------------------------------------
    using P = engine::P;
    float value(P p) const noexcept;
    float value(engine::ParamIndex i) const noexcept { return value(static_cast<P>(i)); }
    bool isLive(P p) const noexcept { return frame().live[engine::idx(p)] != 0; }
    int pickup(P p) const noexcept { return frame().midiPickup[engine::idx(p)]; }
    bool isLearning(P p) const;
    const engine::ParamSpec& spec(P p) const noexcept { return registry.spec(engine::idx(p)); }

    void set(P p, float v);
    void beginTouch(P p) { touching[engine::idx(p)] = 1; }
    void endTouch(P p) { touching[engine::idx(p)] = 0; }
    void release(P p);
    void resetToDefault(P p) { set(p, spec(p).defaultValue); }
    void toggle(P p) { set(p, value(p) > 0.5f ? 0.0f : 1.0f); }

    float toNorm(P p, float v) const noexcept { return spec(p).toNormalised(v); }
    float fromNorm(P p, float n) const noexcept { return spec(p).fromNormalised(n); }

    juce::String name(P p) const { return spec(p).name; }
    juce::String format(P p, float v) const;
    juce::StringArray choices(P p) const;

    /** Shows the parameter's MIDI / layer menu at the mouse. */
    void showParamMenu(P p);

    // --- Help line (the status bar shows what is under the mouse) ---------------------
    std::function<void(const juce::String&)> onHover;

    static juce::String noteName(float midi);

private:
    std::vector<Animated*> animated;
    std::vector<std::uint8_t> touching;
    std::vector<float> local;
    std::vector<int> holdFrames; // frames to keep the local value after a set (telemetry lags)
};

} // namespace tf::app::gui
