#pragma once

#include <engine/control/ControlEvent.h>
#include <engine/params/ParamRegistry.h>

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace tf::tools {

/** A timed list of ControlEvents loaded from JSON. The same event stream the UI and
    MIDI produce, so a score is effectively a recorded performance.

    {
      "duration": 60, "sampleRate": 48000, "blockSize": 256, "seed": 1,
      "events": [
        { "t": 0,  "cmd": "fadeIn" },
        { "t": 0,  "param": "drone.root", "value": 36 },
        { "t": 30, "param": "drone.cutoff", "value": 3000 }
      ]
    }

    Optional: "randomBlockSizes": true renders with varying host block sizes. */
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
    std::vector<TimedEvent> events; // sorted by sample

    /** Throws std::runtime_error with a readable message on bad input. */
    static Score load(const juce::File& file, const engine::ParamRegistry& registry);
};

} // namespace tf::tools
