#pragma once

#include "../control/ControlEvent.h"

#include <cstdint>
#include <vector>

namespace tf::engine {

/** One performer action, stamped with its time in samples from the start of the take.
    A take's end is marked by an event with Type::Command and Command::None. */
struct GestureEvent
{
    std::uint64_t time = 0;
    ControlEvent event;

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
