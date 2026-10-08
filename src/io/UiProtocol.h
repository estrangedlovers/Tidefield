#pragma once

#include <engine/control/Telemetry.h>

#include <juce_core/juce_core.h>

#include <vector>

namespace tf::engine {
class Engine;
class FxManager;
class SceneManager;
class SeasonManager;
}

namespace tf::io {
juce::var buildSchema(const engine::Engine& engine);

juce::var describeScenes(const engine::SceneManager& scenes);
juce::var describeFx(const engine::FxManager& fx);
juce::var describeSamples(const engine::Engine& engine);
juce::var describeSeasons(const engine::SeasonManager& seasons);

class TelemetryEncoder
{
public:
    void reset() noexcept { primed = false; }

    juce::var encode(const engine::TelemetryFrame& frame);

private:
    bool primed = false;
    std::vector<float> lastTargets;
    std::vector<std::uint8_t> lastLive;
    std::vector<std::int8_t> lastPickup;
};
}
