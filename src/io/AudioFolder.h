#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace tf::io {
inline constexpr const char* kAudioFileWildcard = "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
inline constexpr const char* kAudioFileExtensions = "wav;wave;aif;aiff;flac;ogg;mp3";

bool isAudioFile(const juce::File& file);

struct AudioFolderListing
{
    struct Entry
    {
        juce::File file;
        juce::String relativePath;
    };

    std::vector<Entry> entries;
    int found = 0;
    bool complete = true;

    bool truncated() const noexcept { return static_cast<int>(entries.size()) < found || ! complete; }
};

AudioFolderListing listAudioFiles(const juce::File& folder, int maxEntries, int scanLimit = 20000,
                                  const std::atomic<bool>* cancel = nullptr);

std::vector<const AudioFolderListing::Entry*> matchAudioFiles(const AudioFolderListing& listing, const juce::String& query, int maxResults);

juce::StringArray parsePlaces(const juce::String& stored);
juce::String storePlaces(const juce::StringArray& places);
}
