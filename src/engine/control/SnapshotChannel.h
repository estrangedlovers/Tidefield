#pragma once

#include "SpscQueue.h"

#include <cstddef>
#include <memory>

namespace tf::engine {

/** Hands immutable snapshots from the message thread to the audio thread without
    the audio thread ever allocating or freeing.

    Message thread: publish() a new object, call collectGarbage() regularly.
    Audio thread:   acquire() at the top of each block, then read current().

    Retired snapshots travel back through a second queue and are deleted by
    collectGarbage() on the message thread. The publisher tracks how many objects are
    in flight and refuses to publish beyond the queue capacity, so neither queue can
    overflow and a retired pointer is never dropped (which would leak) or freed on the
    audio thread. */
template <typename T>
class SnapshotChannel
{
public:
    explicit SnapshotChannel(std::size_t capacity = 8) : toAudio(capacity), toRetire(capacity + 2), maxInFlight(capacity) {}

    ~SnapshotChannel()
    {
        // Both threads have stopped by the time the owner is destroyed.
        T* p = nullptr;
        while (toAudio.pop(p))
            delete p;
        while (toRetire.pop(p))
            delete p;
        delete live;
    }

    SnapshotChannel(const SnapshotChannel&) = delete;
    SnapshotChannel& operator=(const SnapshotChannel&) = delete;

    /** Message thread. Returns false (and keeps ownership with the caller's
        unique_ptr destroyed) if too many snapshots are still in flight. */
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

    /** Message thread. Frees snapshots the audio thread has finished with. */
    void collectGarbage()
    {
        T* p = nullptr;
        while (toRetire.pop(p))
        {
            delete p;
            --inFlight;
        }
    }

    /** Audio thread. Takes the newest published snapshot, retiring older ones.
        Returns true if the current snapshot changed. */
    bool acquire() noexcept
    {
        T* incoming = nullptr;
        bool changed = false;
        while (toAudio.pop(incoming))
        {
            if (live != nullptr)
                toRetire.push(live); // cannot fail: in-flight count is bounded
            live = incoming;
            changed = true;
        }
        return changed;
    }

    /** Audio thread. May be null before the first publish. */
    const T* current() const noexcept { return live; }

private:
    SpscQueue<T*> toAudio;
    SpscQueue<T*> toRetire;
    T* live = nullptr;          // owned by the audio side
    std::size_t maxInFlight;    // message side only
    std::size_t inFlight = 0;   // message side only: published and not yet deleted
};

} // namespace tf::engine
