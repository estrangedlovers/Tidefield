#pragma once

#include <engine/control/ControlEvent.h>
#include <engine/params/ParamRegistry.h>
#include <engine/scene/SceneManager.h>

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace tf::tools {
struct Score
{
    struct TimedEvent
    {
        std::uint64_t sample = 0;
        engine::ControlEvent event;
    };

    double durationSeconds = 30.0;
    double sampleRate = 48000.0;
    int blockSize = 256;
    std::uint64_t seed = 1;
    bool randomBlockSizes = false;
    std::vector<TimedEvent> events;
    std::vector<engine::Scene> scenes;
    std::vector<engine::ParamIndex> pins;

    struct SampleLoad { int cloud = 0; juce::File file; };
    struct FxLoad { int slot = 0; std::string type; };
    std::vector<SampleLoad> samples;
    std::vector<FxLoad> fx;
    bool defaultFx = true;
    juce::File input;
    juce::File bloomSample;
    juce::File session;

    static Score load(const juce::File& file, const engine::ParamRegistry& registry);
};
}
