#include "GestureManager.h"

#include "../Engine.h"

#include <memory>

namespace tf::engine {

GestureManager::GestureManager(Engine& e) : engine(e) {}

void GestureManager::record()
{
    pending.clear();
    recording = true;
    engine.command(Command::GestureRecord);
}

void GestureManager::play()
{
    if (! hasTake())
        return;
    if (dirty)
        publish();
    engine.command(Command::GesturePlay);
}

void GestureManager::stop() { engine.command(Command::GestureStop); }

void GestureManager::clear()
{
    stop();
    setTake({});
}

void GestureManager::setLoop(bool loop, bool restart)
{
    take.loop = loop;
    publish(); // a playing engine sees a new version and stops
    if (restart && hasTake())
        engine.command(Command::GesturePlay);
}

void GestureManager::setTake(GestureTake newTake)
{
    stop();
    recording = false;
    take = std::move(newTake);
    publish();
}

void GestureManager::tick()
{
    GestureEvent e;
    while (engine.popGesture(e))
    {
        if (! e.isEnd())
        {
            if (pending.size() < 200000) // about an hour of busy playing
                pending.push_back(e);
            continue;
        }
        // The end marker: the take is complete.
        recording = false;
        take.events = std::move(pending);
        pending = {};
        take.length = std::max<std::uint64_t>(e.time, 1);
        take.sampleRate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
        publish();
        if (onTakeFinished)
            onTakeFinished();
    }
    if (dirty)
        publish();
}

void GestureManager::publish()
{
    auto copy = std::make_unique<GestureTake>(take);
    copy->version = ++version;
    dirty = ! engine.publishGesture(std::move(copy));
}

} // namespace tf::engine
