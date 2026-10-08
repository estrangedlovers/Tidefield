#pragma once

#include "SpscQueue.h"

#include <cstddef>
#include <memory>

namespace tf::engine {
template <typename T>
class SnapshotChannel
{
public:
    explicit SnapshotChannel(std::size_t capacity = 8) : toAudio(capacity), toRetire(capacity + 2), maxInFlight(capacity) {}

    ~SnapshotChannel()
    {
        T* p = nullptr;
        while (toAudio.pop(p))
            delete p;
        while (toRetire.pop(p))
            delete p;
        delete live;
    }

    SnapshotChannel(const SnapshotChannel&) = delete;
    SnapshotChannel& operator=(const SnapshotChannel&) = delete;

    bool publish(std::unique_ptr<T> snapshot)
    {
        collectGarbage();
        if (inFlight >= maxInFlight)
            return false;
        if (! toAudio.push(snapshot.get()))
            return false;
        snapshot.release();
        ++inFlight;
        return true;
    }

    void collectGarbage()
    {
        T* p = nullptr;
        while (toRetire.pop(p))
        {
            delete p;
            --inFlight;
        }
    }

    bool acquire() noexcept
    {
        T* incoming = nullptr;
        bool changed = false;
        while (toAudio.pop(incoming))
        {
            if (live != nullptr)
                toRetire.push(live);
            live = incoming;
            changed = true;
        }
        return changed;
    }

    bool hasPending() const noexcept { return toAudio.sizeApprox() > 0; }

    const T* current() const noexcept { return live; }

private:
    SpscQueue<T*> toAudio;
    SpscQueue<T*> toRetire;
    T* live = nullptr;
    std::size_t maxInFlight;
    std::size_t inFlight = 0;
};
}
