#pragma once

#include "../control/ControlEvent.h"

#include <cstdint>
#include <vector>

namespace tf::engine {

/** One performer action, stamped with its time in samples from the start of the take
    and the recording it belongs to. A take's end is marked by an event with
    Type::Command and Command::None whose value is the sample rate `time` counts in. */
struct GestureEvent
{
    std::uint64_t time = 0;
    ControlEvent event;
    std::uint16_t generation = 0; // which recording (GestureRecord's param)

    bool isEnd() const noexcept { return event.type == ControlEvent::Type::Command && event.command == Command::None; }
};

/** A recorded performance, immutable once published to the engine. */
struct GestureTake
{
    std::vector<GestureEvent> events; // sorted by time, no end marker
    std::uint64_t length = 0;         // samples
    double sampleRate = 48000.0;      // the rate `time` and `length` were measured in
    bool loop = true;
    std::uint64_t version = 0;
};

/** Gesture state, as telemetry reports it. */
enum class GestureState : std::uint8_t { Idle, Recording, Playing };

} // namespace tf::engine
