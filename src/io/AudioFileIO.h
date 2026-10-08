#pragma once

#include <dsp/core/SampleBuffer.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace tf::io {
std::unique_ptr<dsp::SampleBuffer> loadSample(const juce::File& file, juce::String& error, double maxSeconds = 120.0);

bool writeSample(const dsp::SampleBuffer& buffer, const juce::File& file, juce::String& error);

std::unique_ptr<dsp::SampleBuffer> loadSample(std::unique_ptr<juce::InputStream> stream, const juce::String& name, juce::String& error,
                                              double maxSeconds = 600.0);
bool encodeFlac(const dsp::SampleBuffer& buffer, juce::MemoryBlock& out, juce::String& error);
}
