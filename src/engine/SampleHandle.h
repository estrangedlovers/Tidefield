#pragma once

#include <dsp/core/SampleBuffer.h>

#include <memory>

namespace tf::engine {
struct SampleHandle
{
    std::shared_ptr<const dsp::SampleBuffer> buffer;
};

inline const dsp::SampleBuffer* rawBuffer(const SampleHandle* h) noexcept { return h != nullptr ? h->buffer.get() : nullptr; }
}
