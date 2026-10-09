#include "AudioFolder.h"

#include <algorithm>

namespace tf::io {
namespace {
bool insideHiddenFolder(const juce::String& relativePath)
{
    return relativePath.startsWithChar('.') || relativePath.contains("/.");
}
}

bool isAudioFile(const juce::File& file)
{
    return ! file.getFileName().startsWithChar('.') && file.hasFileExtension(kAudioFileExtensions);
}

AudioFolderListing listAudioFiles(const juce::File& folder, int maxEntries, int scanLimit, const std::atomic<bool>* cancel)
{
    AudioFolderListing listing;
    if (! folder.isDirectory())
        return listing;
    const int visitLimit = std::max(scanLimit, 1) * 10;
    int visited = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::noCycles))
    {
        if ((cancel != nullptr && cancel->load(std::memory_order_relaxed)) || ++visited > visitLimit)
        {
            listing.complete = false;
            break;
        }
        const auto& file = entry.getFile();
        if (! isAudioFile(file))
            continue;
        const auto relative = file.getRelativePathFrom(folder).replaceCharacter('\\', '/');
        if (insideHiddenFolder(relative))
            continue;
        listing.entries.push_back({ file, relative });
        if (static_cast<int>(listing.entries.size()) >= scanLimit)
        {
            listing.complete = false;
            break;
        }
    }
    listing.found = static_cast<int>(listing.entries.size());
    std::sort(listing.entries.begin(), listing.entries.end(),
              [](const auto& a, const auto& b) { return a.relativePath.compareNatural(b.relativePath) < 0; });
    if (static_cast<int>(listing.entries.size()) > maxEntries)
        listing.entries.resize(static_cast<std::size_t>(std::max(0, maxEntries)));
    return listing;
}

std::vector<const AudioFolderListing::Entry*> matchAudioFiles(const AudioFolderListing& listing, const juce::String& query, int maxResults)
{
    std::vector<const AudioFolderListing::Entry*> matches;
    const auto needle = query.trim();
    if (needle.isEmpty())
        return matches;
    for (const auto& e : listing.entries)
    {
        if (static_cast<int>(matches.size()) >= maxResults)
            break;
        if (e.relativePath.containsIgnoreCase(needle))
            matches.push_back(&e);
    }
    return matches;
}

juce::StringArray parsePlaces(const juce::String& stored)
{
    juce::StringArray places;
    for (auto line : juce::StringArray::fromLines(stored))
    {
        line = line.trim();
        if (line.isNotEmpty() && juce::File::isAbsolutePath(line))
            places.addIfNotAlreadyThere(juce::File(line).getFullPathName());
    }
    return places;
}

juce::String storePlaces(const juce::StringArray& places) { return places.joinIntoString("\n"); }
}
