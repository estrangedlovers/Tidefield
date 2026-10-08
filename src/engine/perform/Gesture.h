#pragma once

#include "../control/ControlEvent.h"

#include <cstdint>
#include <vector>

namespace tf::engine {
struct GestureEvent
{
    std::uint64_t time = 0;
    ControlEvent event;
    std::uint16_t generation = 0;

    bool isEnd() const noexcept { return event.type == ControlEvent::Type::Command && event.command == Command::None; }
};

struct GestureTake
{
    std::vector<GestureEvent> events;
    std::uint64_t length = 0;
    double sampleRate = 48000.0;
    bool loop = true;
    std::uint64_t version = 0;
};

enum class GestureState : std::uint8_t { Idle, Recording, Playing };
}
