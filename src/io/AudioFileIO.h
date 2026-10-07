#pragma once

#include <dsp/core/SampleBuffer.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace tf::io {

/** Decodes an audio file (WAV, AIFF, FLAC, Ogg; MP3 on macOS) into a SampleBuffer.
    Off the audio thread only. Keeps the file's own sample rate: sources resample on
    read. Returns nullptr and fills `error` on failure. Files longer than
    `maxSeconds` are truncated (a cloud does not need an hour of audio). */
std::unique_ptr<dsp::SampleBuffer> loadSample(const juce::File& file, juce::String& error, double maxSeconds = 120.0);

/** Writes a SampleBuffer as 32-bit float WAV (or FLAC when the extension is .flac). */
bool writeSample(const dsp::SampleBuffer& buffer, const juce::File& file, juce::String& error);

/** Stream variants (used for audio stored inside session files). */
std::unique_ptr<dsp::SampleBuffer> loadSample(std::unique_ptr<juce::InputStream> stream, const juce::String& name, juce::String& error,
                                              double maxSeconds = 600.0);
/** Encodes as 24-bit FLAC into memory. */
bool encodeFlac(const dsp::SampleBuffer& buffer, juce::MemoryBlock& out, juce::String& error);

} // namespace tf::io
