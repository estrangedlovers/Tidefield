#pragma once

#include <engine/control/Telemetry.h>

#include <juce_core/juce_core.h>

#include <vector>

namespace tf::engine {
class Engine;
class FxManager;
class SceneManager;
} // namespace tf::engine

namespace tf::io {

/** JSON messages between the engine side and the web UI. The schema describes every
    parameter, strip, slot and processor, so the front end has no hard-coded tables;
    telemetry carries only what changed since the last frame for parameters. */
juce::var buildSchema(const engine::Engine& engine);

juce::var describeScenes(const engine::SceneManager& scenes);
juce::var describeFx(const engine::FxManager& fx);
juce::var describeSamples(const engine::Engine& engine);

class TelemetryEncoder
{
public:
    /** Next frame includes every parameter (call when the page (re)loads). */
    void reset() noexcept { primed = false; }

    juce::var encode(const engine::TelemetryFrame& frame);

private:
    bool primed = false;
    std::vector<float> lastTargets;
    std::vector<std::uint8_t> lastLive;
    std::vector<std::int8_t> lastPickup;
};

} // namespace tf::io
