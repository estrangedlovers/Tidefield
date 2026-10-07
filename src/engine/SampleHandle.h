#pragma once

#include <dsp/core/SampleBuffer.h>

#include <memory>

namespace tf::engine {

/** What travels through a sample SnapshotChannel. The message thread keeps its own
    shared_ptr to the same immutable buffer (for saving sessions), so the audio thread
    only ever sees a raw pointer: reference counts are touched exclusively on the
    message thread, when handles are created and when retired handles are deleted.
    An empty handle unloads the slot. */
struct SampleHandle
{
    std::shared_ptr<const dsp::SampleBuffer> buffer;
};

inline const dsp::SampleBuffer* rawBuffer(const SampleHandle* h) noexcept { return h != nullptr ? h->buffer.get() : nullptr; }

} // namespace tf::engine
