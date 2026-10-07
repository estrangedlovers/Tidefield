#pragma once

#include "Gesture.h"

#include <functional>

namespace tf::engine {

class Engine;

/** Message-thread side of gesture recording: starts and stops the engine, collects
    the moves it reports into a take, and publishes the take for playback. */
class GestureManager
{
public:
    explicit GestureManager(Engine& engine);

    void record();
    void play();
    void stop();
    void clear();
    /** Loop or play once; restart = the take is playing now and should go on. */
    void setLoop(bool loop, bool restart);
    bool isLooping() const noexcept { return take.loop; }

    bool isRecording() const noexcept { return recording; }
    bool hasTake() const noexcept { return take.length > 0; }
    const GestureTake& getTake() const noexcept { return take; }
    /** Replaces the take (session recall). Stops anything running. */
    void setTake(GestureTake newTake);

    /** Call regularly: collects recorded moves, finishes a take, retries publishing. */
    void tick();

    /** A recording finished (message thread); the take is ready to play. */
    std::function<void()> onTakeFinished;

private:
    void publish();

    Engine& engine;
    GestureTake take;
    std::vector<GestureEvent> pending;
    bool recording = false;
    bool dirty = false;
    std::uint64_t version = 0;
};

} // namespace tf::engine
