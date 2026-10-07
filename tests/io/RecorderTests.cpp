#include <engine/Engine.h>
#include <io/AudioFileIO.h>
#include <io/Recorder.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace tf;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tidefield_rec", "");
    ~TempDir() { dir.deleteRecursively(); }
};

} // namespace

TEST_CASE("Recorder writes the master and stems as float WAVs matching the output", "[record]")
{
    TempDir tmp;
    engine::Engine eng;
    eng.prepare(kFs, kBlock);
    eng.setParam(engine::P::MasterFadeSecs, 0.1f);
    eng.command(engine::Command::FadeIn);

    std::vector<float> l(kBlock), r(kBlock), in(kBlock, 0.0f), outL;
    float* outs[2] = { l.data(), r.data() };
    const float* ins[2] = { in.data(), in.data() };
    auto block = [&](bool keep) {
        eng.process(ins, 2, outs, 2, kBlock);
        if (keep)
            outL.insert(outL.end(), l.begin(), l.end());
    };
    for (int b = 0; b < 40; ++b)
        block(false);

    io::Recorder rec(eng.getRecordTap());
    bool finished = false, finishedOk = false;
    rec.onFinished = [&](const juce::File&, bool ok) {
        finished = true;
        finishedOk = ok;
    };
    const auto folder = io::Recorder::makeFolder(tmp.dir, "test take");
    REQUIRE(folder.getFileName().endsWith("test take"));
    REQUIRE(rec.start(folder, kFs, true).wasOk());
    REQUIRE(rec.getStatus().state == io::Recorder::State::Recording);
    REQUIRE(rec.start(folder, kFs, true).failed()); // one at a time

    const int blocks = static_cast<int>(1.5 * kFs / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        block(true);
        if (b % 8 == 0)
            rec.drainNow(); // the background thread drains too; both are fine
    }
    rec.stop();
    REQUIRE(rec.getStatus().state == io::Recorder::State::Finishing);
    block(false); // the engine acknowledges the stop at its next block
    rec.drainNow();
    REQUIRE(rec.getStatus().state == io::Recorder::State::Idle);
    REQUIRE(finished);
    REQUIRE(finishedOk);
    CHECK(rec.getStatus().droppedFrames == 0);

    juce::String error;
    auto master = io::loadSample(folder.getChildFile("master.wav"), error);
    REQUIRE(master != nullptr);
    REQUIRE(master->size() == outL.size());
    for (std::size_t i = 0; i < outL.size(); ++i)
        REQUIRE(master->left[i] == outL[i]); // 32-bit float: bit-exact

    for (const auto& name : io::Recorder::stemNames())
    {
        auto stem = io::loadSample(folder.getChildFile("stems").getChildFile(name + ".wav"), error);
        REQUIRE(stem != nullptr);
        CHECK(stem->size() == outL.size());
    }

    // The next take can start straight away.
    const auto second = io::Recorder::makeFolder(tmp.dir, "second");
    REQUIRE(rec.start(second, kFs, false).wasOk());
    block(false);
    rec.stop();
    block(false);
    rec.drainNow();
    CHECK(second.getChildFile("master.wav").getSize() > 0);
    CHECK_FALSE(second.getChildFile("stems").exists());
}
