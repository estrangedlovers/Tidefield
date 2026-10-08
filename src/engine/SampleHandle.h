#pragma once

#include <dsp/core/SampleBuffer.h>

#include <array>
#include <memory>

namespace tf::engine {
struct SampleHandle
{
    std::shared_ptr<const dsp::SampleBuffer> buffer;
};

struct BloomZoneSet
{
    std::array<std::shared_ptr<const dsp::SampleBuffer>, 8> buffers;
    std::array<float, 8> roots {};
    int count = 0;
};

inline const dsp::SampleBuffer* rawBuffer(const SampleHandle* h) noexcept { return h != nullptr ? h->buffer.get() : nullptr; }
}
