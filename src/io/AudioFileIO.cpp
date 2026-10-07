#include "AudioFileIO.h"

namespace tf::io {

std::unique_ptr<dsp::SampleBuffer> loadSample(const juce::File& file, juce::String& error, double maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "Could not read " + file.getFullPathName();
        return nullptr;
    }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Empty or invalid audio file: " + file.getFileName();
        return nullptr;
    }

    const auto maxLength = static_cast<juce::int64>(maxSeconds * reader->sampleRate);
    const int length = static_cast<int>(std::min(reader->lengthInSamples, maxLength));
    const int channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2));

    juce::AudioBuffer<float> temp(channels, length);
    reader->read(&temp, 0, length, 0, true, channels > 1);

    auto buffer = std::make_unique<dsp::SampleBuffer>();
    buffer->sampleRate = reader->sampleRate;
    buffer->name = file.getFileNameWithoutExtension().toStdString();
    buffer->left.assign(temp.getReadPointer(0), temp.getReadPointer(0) + length);
    if (channels > 1)
        buffer->right.assign(temp.getReadPointer(1), temp.getReadPointer(1) + length);
    return buffer;
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
