#include "AudioFileIO.h"

#include <cmath>
#include <new>

namespace tf::io {

namespace {

std::unique_ptr<dsp::SampleBuffer> readAll(std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& name,
                                           juce::String& error, double maxSeconds)
{
    if (reader == nullptr)
    {
        error = "Could not read " + name;
        return nullptr;
    }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Empty or invalid audio: " + name;
        return nullptr;
    }

    if (reader->sampleRate < 4000.0 || reader->sampleRate > 768000.0 || reader->numChannels == 0)
    {
        error = "Unsupported audio format: " + name;
        return nullptr;
    }

    // A length limit in frames as well as seconds: a header claiming an absurd rate
    // must not make us allocate gigabytes.
    constexpr juce::int64 kMaxFrames = 24 * 1000 * 1000; // ~8 minutes at 48 kHz
    const auto maxLength = std::min(kMaxFrames, static_cast<juce::int64>(maxSeconds * reader->sampleRate));
    const int length = static_cast<int>(std::min(reader->lengthInSamples, maxLength));
    const int channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2));

    try
    {
        juce::AudioBuffer<float> temp(channels, length);
        if (! reader->read(&temp, 0, length, 0, true, channels > 1))
        {
            error = "The audio in " + name + " is damaged or cut short";
            return nullptr;
        }
        auto buffer = std::make_unique<dsp::SampleBuffer>();
        buffer->sampleRate = reader->sampleRate;
        buffer->name = name.toStdString();
        buffer->left.assign(temp.getReadPointer(0), temp.getReadPointer(0) + length);
        if (channels > 1)
            buffer->right.assign(temp.getReadPointer(1), temp.getReadPointer(1) + length);
        for (auto* ch : { &buffer->left, &buffer->right })
            for (auto& v : *ch)
                if (! std::isfinite(v))
                    v = 0.0f; // float files can carry NaN
        dsp::buildMips(*buffer); // band-limited copies for pitched-up playback
        return buffer;
    }
    catch (const std::bad_alloc&)
    {
        error = "Not enough memory to load " + name;
        return nullptr;
    }
}

} // namespace

std::unique_ptr<dsp::SampleBuffer> loadSample(const juce::File& file, juce::String& error, double maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return readAll(std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file)), file.getFileNameWithoutExtension(), error,
                   maxSeconds);
}

std::unique_ptr<dsp::SampleBuffer> loadSample(std::unique_ptr<juce::InputStream> stream, const juce::String& name, juce::String& error,
                                              double maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return readAll(std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(std::move(stream))), name, error, maxSeconds);
}

bool encodeFlac(const dsp::SampleBuffer& buffer, juce::MemoryBlock& out, juce::String& error)
{
    out.reset();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(out, false);
    juce::FlacAudioFormat flac;
    const int channels = buffer.isStereo() ? 2 : 1;
    const auto options = juce::AudioFormatWriterOptions {}.withSampleRate(buffer.sampleRate).withNumChannels(channels).withBitsPerSample(24);
    auto writer = flac.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        error = "FLAC encoder unavailable for this sample rate";
        return false;
    }
    const float* ptrs[2] = { buffer.left.data(), buffer.isStereo() ? buffer.right.data() : buffer.left.data() };
    if (! writer->writeFromFloatArrays(ptrs, channels, static_cast<int>(buffer.size())))
    {
        error = "FLAC encoding failed";
        return false;
    }
    writer.reset(); // flushes into `out`
    return true;
}

bool writeSample(const dsp::SampleBuffer& buffer, const juce::File& file, juce::String& error)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen())
    {
        error = "Could not open " + file.getFullPathName() + " for writing";
        return false;
    }

    const bool flac = file.hasFileExtension("flac");
    std::unique_ptr<juce::AudioFormat> format;
    if (flac)
        format = std::make_unique<juce::FlacAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    const int channels = buffer.isStereo() ? 2 : 1;
    auto options = juce::AudioFormatWriterOptions {}.withSampleRate(buffer.sampleRate).withNumChannels(channels);
    options = flac ? options.withBitsPerSample(24)
                   : options.withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format->createWriterFor(stream, options);
    if (writer == nullptr)
    {
        error = "No writer for " + file.getFileName();
        return false;
    }

    const float* ptrs[2] = { buffer.left.data(), buffer.isStereo() ? buffer.right.data() : buffer.left.data() };
    if (! writer->writeFromFloatArrays(ptrs, channels, static_cast<int>(buffer.size())))
    {
        error = "Write failed: " + file.getFileName();
        return false;
    }
    return true;
}

} // namespace tf::io
