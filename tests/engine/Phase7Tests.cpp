#include "support/AllocationGuard.h"

#include <dsp/sources/resonator/ResonatorBank.h>
#include <engine/Engine.h>
#include <engine/guard/DegradationPolicy.h>
#include <engine/record/RecordTap.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::engine;
using Catch::Approx;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

float rms(const float* x, std::size_t n)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, n))));
}

struct RecordRig
{
    Engine engine;
    std::vector<float> outL, outR;
    std::vector<std::vector<float>> recorded;
    std::vector<float> scratch;

    RecordRig()
    {
        engine.prepare(kFs, kBlock);
        engine.setParam(P::MasterFadeSecs, 0.2f);
        engine.command(Command::FadeIn);
    }

    void run(double seconds)
    {
        std::vector<float> l(kBlock), r(kBlock), in(kBlock, 0.0f);
        scratch.resize(static_cast<std::size_t>(kBlock * RecordTap::kMaxChannels));
        const int blocks = static_cast<int>(seconds * kFs / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            float* outs[2] = { l.data(), r.data() };
            const float* ins[2] = { in.data(), in.data() };
            engine.process(ins, 2, outs, 2, kBlock);
            outL.insert(outL.end(), l.begin(), l.end());
            outR.insert(outR.end(), r.begin(), r.end());
            drain();
        }
    }

    void drain()
    {
        auto& tap = engine.getRecordTap();
        const auto stride = static_cast<std::size_t>(tap.getStride());
        if (recorded.size() != stride)
            recorded.assign(stride, {});
        int frames = 0;
        while ((frames = tap.read(scratch.data(), kBlock)) > 0)
            for (int i = 0; i < frames; ++i)
                for (std::size_t c = 0; c < stride; ++c)
                    recorded[c].push_back(scratch[static_cast<std::size_t>(i) * stride + c]);
    }
};
}

TEST_CASE("RecordTap moves through its states and keeps channels in step", "[record]")
{
    RecordTap tap;
    tap.prepare(1000.0, 0.1);
    REQUIRE(tap.getState() == RecordTap::State::Idle);
    REQUIRE(tap.beginBlock() == 0);

    REQUIRE(tap.begin(false));
    REQUIRE_FALSE(tap.begin(false));
    REQUIRE(tap.beginBlock() == 2);

    std::vector<float> a(10), b(10);
    for (int i = 0; i < 10; ++i)
    {
        a[static_cast<std::size_t>(i)] = static_cast<float>(i);
        b[static_cast<std::size_t>(i)] = -static_cast<float>(i);
    }
    const float* ch[2] = { a.data(), b.data() };
    tap.push(ch, 10);

    std::vector<float> out(64);
    REQUIRE(tap.read(out.data(), 4) == 4);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[6] == 3.0f);
    CHECK(out[7] == -3.0f);

    tap.end();
    REQUIRE(tap.getState() == RecordTap::State::Stopping);
    REQUIRE(tap.beginBlock() == 0);
    REQUIRE(tap.getState() == RecordTap::State::Stopped);
    tap.push(ch, 10);
    REQUIRE(tap.read(out.data(), 64) == 6);
    CHECK(out[0] == 4.0f);
    REQUIRE(tap.read(out.data(), 64) == 0);
    tap.finish();
    REQUIRE(tap.getState() == RecordTap::State::Idle);
}

TEST_CASE("RecordTap drops whole blocks when the writer falls behind", "[record]")
{
    RecordTap tap;
    tap.prepare(1000.0, 2.0 * 140.0 / (1000.0 * RecordTap::kMaxChannels));
    REQUIRE(tap.begin(false));
    std::vector<float> a(50, 1.0f);
    const float* ch[2] = { a.data(), a.data() };
    REQUIRE(tap.beginBlock() == 2);
    tap.push(ch, 50);
    tap.push(ch, 50);
    tap.push(ch, 50);
    CHECK(tap.getDroppedFrames() == 50);
    std::vector<float> out(400);
    CHECK(tap.read(out.data(), 200) == 100);
    tap.push(ch, 50);
    CHECK(tap.read(out.data(), 200) == 50);
}

TEST_CASE("Recording captures exactly what the engine outputs", "[record]")
{
    RecordRig rig;
    rig.run(0.5);
    const auto before = rig.outL.size();
    REQUIRE(rig.engine.getRecordTap().begin(false));
    rig.run(2.0);
    rig.engine.getRecordTap().end();
    rig.run(0.1);
    REQUIRE(rig.engine.getRecordTap().getState() == RecordTap::State::Stopped);
    rig.engine.getRecordTap().finish();

    REQUIRE(rig.recorded.size() == 2);
    const auto frames = rig.recorded[0].size();
    REQUIRE(frames == static_cast<std::size_t>(static_cast<int>(2.0 * kFs / kBlock) * kBlock));
    for (std::size_t i = 0; i < frames; ++i)
    {
        REQUIRE(rig.recorded[0][i] == rig.outL[before + i]);
        REQUIRE(rig.recorded[1][i] == rig.outR[before + i]);
    }
    CHECK(rms(rig.recorded[0].data(), frames) > 1.0e-3f);
}

TEST_CASE("Stems carry each strip and the bus returns", "[record]")
{
    RecordRig rig;
    rig.engine.setParam(P::DroneSendA, -12.0f);
    rig.run(1.0);
    REQUIRE(rig.engine.getRecordTap().begin(true));
    rig.run(3.0);
    rig.engine.getRecordTap().end();
    rig.run(0.05);
    rig.engine.getRecordTap().finish();

    REQUIRE(rig.recorded.size() == static_cast<std::size_t>(RecordTap::kMaxChannels));
    const auto n = rig.recorded[0].size();
    auto stemRms = [&](int pair) { return rms(rig.recorded[static_cast<std::size_t>(2 + 2 * pair)].data(), n); };
    CHECK(stemRms(static_cast<int>(StripId::Drone)) > 1.0e-3f);
    CHECK(stemRms(static_cast<int>(StripId::Input)) == 0.0f);
    CHECK(stemRms(kNumStrips) > 1.0e-5f);

    std::vector<float> sum(n, 0.0f);
    for (int pair = 0; pair < RecordTap::kStemPairs; ++pair)
        for (std::size_t i = 0; i < n; ++i)
            sum[i] += rig.recorded[static_cast<std::size_t>(2 + 2 * pair)][i];
    const float ratioDb = 20.0f * std::log10(rms(sum.data(), n) / rms(rig.recorded[0].data(), n));
    CHECK(std::fabs(ratioDb) < 3.0f);
}

TEST_CASE("Recording stems with guardrails on never allocates", "[record][guard][alloc]")
{
    Engine engine;
    engine.prepare(kFs, kBlock);
    engine.setGuardrailsEnabled(true);
    engine.command(Command::FadeIn);
    REQUIRE(engine.getRecordTap().begin(true));
    std::vector<float> l(kBlock), r(kBlock), in(kBlock, 0.0f);
    float* outs[2] = { l.data(), r.data() };
    const float* ins[2] = { in.data(), in.data() };
    std::vector<float> scratch(static_cast<std::size_t>(kBlock * RecordTap::kMaxChannels));

    std::size_t allocations = 0;
    {
        tf::test::ScopedAllocationCounter counter;
        for (int b = 0; b < 200; ++b)
        {
            engine.process(ins, 2, outs, 2, kBlock);
            while (engine.getRecordTap().read(scratch.data(), kBlock) > 0) {}
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}

TEST_CASE("DegradationPolicy steps down fast, back up slowly, with back-off", "[guard]")
{
    DegradationPolicy policy;
    policy.reset();
    const float dt = 256.0f / 48000.0f;
    auto feed = [&](float load, float seconds) {
        int changes = 0;
        for (float t = 0.0f; t < seconds; t += dt)
            changes += policy.update(load, dt) ? 1 : 0;
        return changes;
    };

    feed(0.6f, 5.0f);
    CHECK(policy.getLevel() == 0);

    feed(0.9f, 0.3f);
    CHECK(policy.getLevel() == 0);
    feed(0.9f, 0.2f);
    CHECK(policy.getLevel() == 1);

    feed(0.3f, 5.0f);
    CHECK(policy.getLevel() == 1);
    feed(0.3f, 2.0f);
    CHECK(policy.getLevel() == 0);

    feed(0.9f, 0.5f);
    CHECK(policy.getLevel() == 1);
    feed(0.3f, 7.0f);
    CHECK(policy.getLevel() == 1);
    feed(0.3f, 6.0f);
    CHECK(policy.getLevel() == 0);

    feed(0.1f, 1.0f);
    REQUIRE(policy.update(1.6f, dt));
    CHECK(policy.getLevel() == 1);

    feed(2.0f, 10.0f);
    CHECK(policy.getLevel() == kMaxGuardLevel);
    CHECK_FALSE(policy.update(std::nanf(""), dt));
}

TEST_CASE("Engine guardrails follow load and report it", "[guard]")
{
    Engine engine;
    engine.prepare(kFs, kBlock);
    engine.command(Command::FadeIn);
    std::vector<float> l(kBlock), r(kBlock), in(kBlock, 0.0f);
    float* outs[2] = { l.data(), r.data() };
    const float* ins[2] = { in.data(), in.data() };
    TelemetryFrame f;
    auto run = [&](double seconds) {
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlock); ++b)
        {
            engine.process(ins, 2, outs, 2, kBlock);
            while (engine.popTelemetry(f)) {}
        }
    };

    run(0.5);
    CHECK(f.guardLevel == 0);
    CHECK(f.dspLoad == 0.0f);

    engine.setGuardrailsEnabled(true);
    engine.forceLoadForTesting(1.2f);
    run(4.0);
    CHECK(f.guardLevel == kMaxGuardLevel);
    CHECK(f.dspLoad > 0.9f);

    engine.forceLoadForTesting(0.2f);
    run(8.0);
    CHECK(f.guardLevel == kMaxGuardLevel - 1);

    engine.setGuardrailsEnabled(false);
    run(0.1);
    CHECK(f.guardLevel == 0);
}

TEST_CASE("Lowering the resonator's mode count rings out instead of cutting", "[guard][resonator]")
{
    tf::dsp::ResonatorBank bank;
    bank.prepare({ kFs, kBlock }, 3);
    tf::dsp::ResonatorBank::Params p;
    p.modes = 24;
    p.decaySeconds = 6.0f;
    p.rain = 0.0f;
    bank.setParams(p);

    std::vector<float> ex(kBlock, 0.0f), l(kBlock), r(kBlock);
    ex[0] = 20.0f;
    bank.process(ex.data(), l.data(), r.data(), kBlock, 1.0f);
    ex[0] = 0.0f;
    for (int b = 0; b < 40; ++b)
        bank.process(ex.data(), l.data(), r.data(), kBlock, 1.0f);
    const float before = rms(l.data() + kBlock - 96, 96);
    REQUIRE(before > 1.0e-4f);

    bank.setModeLimit(1);
    bank.process(ex.data(), l.data(), r.data(), kBlock, 1.0f);
    const float justAfter = rms(l.data(), 96);
    CHECK(justAfter > 0.5f * before);

    for (int b = 0; b < 40; ++b)
        bank.process(ex.data(), l.data(), r.data(), kBlock, 1.0f);
    CHECK(rms(l.data(), kBlock) < 0.5f * before);
}
