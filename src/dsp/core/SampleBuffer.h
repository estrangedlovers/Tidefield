#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tf::dsp {

/** Immutable stereo audio held by granular clouds and Bloom. Built off the audio
    thread (Catch, sample import) and only read on it. A mono source stores the same
    data in both channels' slots by keeping `right` empty. */
struct SampleBuffer
{
    std::vector<float> left;
    std::vector<float> right; // empty = mono
    double sampleRate = 48000.0;
    std::string name;

    std::size_t size() const noexcept { return left.size(); }
    bool isStereo() const noexcept { return ! right.empty(); }
    const float* channel(int ch) const noexcept { return ch == 1 && isStereo() ? right.data() : left.data(); }
    double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double>(size()) / sampleRate : 0.0; }
};

} // namespace tf::dsp
