#include <io/AudioFolder.h>

#include <catch2/catch_test_macros.hpp>

using namespace tf;

namespace {
struct TempFolder
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tidefield_places", "", false);
    TempFolder() { root.createDirectory(); }
    ~TempFolder() { root.deleteRecursively(); }
    void touch(const juce::String& relative) const
    {
        auto f = root.getChildFile(relative);
        f.getParentDirectory().createDirectory();
        f.replaceWithText("x");
    }
};
}

TEST_CASE("Audio files are recognised by extension, case-insensitively", "[places]")
{
    CHECK(io::isAudioFile(juce::File("/a/b.wav")));
    CHECK(io::isAudioFile(juce::File("/a/b.AIFF")));
    CHECK(io::isAudioFile(juce::File("/a/b.flac")));
    CHECK(io::isAudioFile(juce::File("/a/b.ogg")));
    CHECK(io::isAudioFile(juce::File("/a/b.Mp3")));
    CHECK_FALSE(io::isAudioFile(juce::File("/a/b.txt")));
    CHECK_FALSE(io::isAudioFile(juce::File("/a/b.tide")));
    CHECK_FALSE(io::isAudioFile(juce::File("/a/._b.wav")));
}

TEST_CASE("A place lists audio files in subfolders, sorted naturally, skipping hidden ones", "[places]")
{
    TempFolder t;
    for (auto name : { "pad 10.wav", "pad 2.wav", "notes.txt", "Bells/bowl.aif", "Bells/deep/gong.flac", ".hidden/secret.wav", "._pad.wav" })
        t.touch(name);
    const auto listing = io::listAudioFiles(t.root, 100);
    REQUIRE(listing.entries.size() == 4);
    CHECK(listing.found == 4);
    CHECK(listing.complete);
    CHECK_FALSE(listing.truncated());
    CHECK(listing.entries[0].relativePath == "Bells/bowl.aif");
    CHECK(listing.entries[1].relativePath == "Bells/deep/gong.flac");
    CHECK(listing.entries[2].relativePath == "pad 2.wav");
    CHECK(listing.entries[3].relativePath == "pad 10.wav");
    CHECK(listing.entries[3].file == t.root.getChildFile("pad 10.wav"));
}

TEST_CASE("A big place is capped but still reports how many files it found", "[places]")
{
    TempFolder t;
    for (int i = 0; i < 30; ++i)
        t.touch("take " + juce::String(i) + ".wav");
    const auto capped = io::listAudioFiles(t.root, 10);
    CHECK(capped.entries.size() == 10);
    CHECK(capped.found == 30);
    CHECK(capped.complete);
    CHECK(capped.truncated());
    CHECK(capped.entries.front().relativePath == "take 0.wav");

    const auto limited = io::listAudioFiles(t.root, 100, 12);
    CHECK(limited.found == 12);
    CHECK_FALSE(limited.complete);
    CHECK(limited.truncated());
}

TEST_CASE("A cancelled scan stops early and a missing folder lists nothing", "[places]")
{
    TempFolder t;
    for (int i = 0; i < 5; ++i)
        t.touch(juce::String(i) + ".wav");
    std::atomic<bool> cancel { true };
    const auto cancelled = io::listAudioFiles(t.root, 100, 20000, &cancel);
    CHECK(cancelled.entries.empty());
    CHECK_FALSE(cancelled.complete);
    const auto missing = io::listAudioFiles(t.root.getChildFile("nope"), 100);
    CHECK(missing.entries.empty());
    CHECK(missing.found == 0);
}

TEST_CASE("Search matches file names and subfolders up to a limit", "[places]")
{
    TempFolder t;
    for (auto name : { "Rain/soft rain.wav", "Rain/heavy.wav", "wind.wav", "Rain on glass.flac" })
        t.touch(name);
    const auto listing = io::listAudioFiles(t.root, 100);
    CHECK(io::matchAudioFiles(listing, "RAIN", 10).size() == 3);
    CHECK(io::matchAudioFiles(listing, "rain", 2).size() == 2);
    CHECK(io::matchAudioFiles(listing, " wind ", 10).size() == 1);
    CHECK(io::matchAudioFiles(listing, "", 10).empty());
}

TEST_CASE("Places round-trip through settings text without duplicates or junk", "[places]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::StringArray places { root.getChildFile("A").getFullPathName(), root.getChildFile("B").getFullPathName() };
    CHECK(io::parsePlaces(io::storePlaces(places)) == places);
    const auto messy = places[0] + "\n\n  " + places[1] + "  \nrelative/path\n" + places[0];
    CHECK(io::parsePlaces(messy) == places);
    CHECK(io::parsePlaces({}).isEmpty());
}
