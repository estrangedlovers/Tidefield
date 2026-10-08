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
    ~Model();

    AppCore& core;
    engine::Engine& engine;
    const engine::ParamRegistry& registry;

    const engine::TelemetryFrame& frame() const noexcept { return core.latest(); }

    /** Once per display frame: ticks every registered Animated. */
    void tick();
    void add(Animated* a) { animated.push_back(a); }
    void remove(Animated* a)
    {
        animated.erase(std::remove(animated.begin(), animated.end(), a), animated.end());
        std::replace(ticking.begin(), ticking.end(), a, static_cast<Animated*>(nullptr)); // removed mid-frame: skip it
    }

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
    /** Moves the sound to `to` at once (a near-zero glide, then the glide comes back).
        Jumps in quick succession keep the performer's glide, not the jump's. */
    void jumpTerrain(engine::Point2 to);

    float toNorm(P p, float v) const noexcept { return spec(p).toNormalised(v); }
    float fromNorm(P p, float n) const noexcept { return spec(p).fromNormalised(n); }

    juce::String name(P p) const { return spec(p).name; }
    juce::String format(P p, float v) const;
    juce::StringArray choices(P p) const;

    /** Shows the parameter's MIDI / layer menu at the mouse. */
    /** owner: the control asking; the menu does nothing if it is gone by then. */
    void showParamMenu(P p, juce::Component* owner);

    // --- Help line (the status bar shows what is under the mouse) ---------------------
    std::function<void(const juce::String&)> onHover;

    static juce::String noteName(float midi);

private:
    std::vector<Animated*> animated;
    std::vector<std::uint8_t> touching;
    std::vector<float> local;
    std::vector<int> holdFrames; // frames to keep the local value after a set (telemetry lags)
    std::vector<std::uint8_t> setHere;
    std::vector<Animated*> ticking; // reused each frame
    float savedGlide = 1.5f;
    bool jumpPending = false;
    int jumpToken = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

} // namespace tf::app::gui
