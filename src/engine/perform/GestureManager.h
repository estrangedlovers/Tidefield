#pragma once

#include "Gesture.h"

#include <functional>

namespace tf::engine {
class Engine;

class GestureManager
{
public:
    explicit GestureManager(Engine& engine);

    void record();
    void play();
    void stop();
    void clear();
    void setLoop(bool loop, bool restart);
    bool isLooping() const noexcept { return take.loop; }

    bool isRecording() const noexcept { return recording; }
    bool hasTake() const noexcept { return take.length > 0; }
    const GestureTake& getTake() const noexcept { return take; }
    void setTake(GestureTake newTake);

    void tick();

    std::function<void()> onTakeFinished;

private:
    void publish();

    Engine& engine;
    GestureTake take;
    std::vector<GestureEvent> pending;
    bool recording = false;
    bool dirty = false;
    std::uint64_t version = 0;
    std::uint16_t generation = 0;
};
}
