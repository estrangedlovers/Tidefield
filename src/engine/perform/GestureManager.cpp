#include "GestureManager.h"

#include "../Engine.h"

#include <memory>

namespace tf::engine {
GestureManager::GestureManager(Engine& e) : engine(e) {}

void GestureManager::record()
{
    ++generation;
    pending.clear();
    recording = true;
    auto e = ControlEvent::makeCommand(Command::GestureRecord);
    e.param = generation;
    engine.post(e);
}

void GestureManager::play()
{
    if (! hasTake())
        return;
    if (dirty)
        publish();
    auto e = ControlEvent::makeCommand(Command::GesturePlay);
    e.value = static_cast<float>(version);
    engine.post(e);
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
    publish();
    if (restart)
        play();
}

void GestureManager::setTake(GestureTake newTake)
{
    stop();
    ++generation;
    recording = false;
    pending.clear();
    take = std::move(newTake);
    publish();
}

void GestureManager::tick()
{
    GestureEvent e;
    while (engine.popGesture(e))
    {
        if (e.generation != generation || ! recording)
            continue;
        if (! e.isEnd())
        {
            if (pending.size() < 200000)
                pending.push_back(e);
            continue;
        }
        recording = false;
        take.events = std::move(pending);
        pending = {};
        take.length = std::max<std::uint64_t>(e.time, 1);
        take.sampleRate = e.event.value > 0.0f ? static_cast<double>(e.event.value) : 48000.0;
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
}
