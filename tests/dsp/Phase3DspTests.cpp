#include "support/AllocationGuard.h"

#include <dsp/core/DelayLine.h>
#include <dsp/core/MathUtil.h>
#include <dsp/core/SampleBuffer.h>
#include <dsp/fx/ProcessorFactory.h>
#include <dsp/fx/delay/TapeDelay.h>
#include <dsp/fx/medium/Medium.h>
#include <dsp/fx/reverb/FdnReverb.h>
#include <dsp/harmony/HarmonicGravity.h>
#include <dsp/sources/granular/GranularCloud.h>
#include <dsp/sources/resonator/ResonatorBank.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf::dsp;
using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;

SampleBuffer makeTone(float hz, float seconds)
{
    SampleBuffer b;
    b.sampleRate = kFs;
    b.left.resize(static_cast<size_t>(seconds * kFs));
    for (size_t i = 0; i < b.left.size(); ++i)
        b.left[i] = 0.5f * std::sin(kTwoPi * hz * static_cast<float>(i) / static_cast<float>(kFs));
    return b;
}

float rms(const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(s / static_cast<double>(to - from)));
}

float peak(const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max(p, std::fabs(v));
    return p;
}

/** Runs a processor over `seconds` of input produced by `gen(sampleIndex)`. */
template <typename Gen>
std::vector<float> runProcessor(Processor& p, float seconds, Gen gen, std::vector<float>* rightOut = nullptr)
{
    const int total = static_cast<int>(seconds * kFs);
    std::vector<float> outL(static_cast<size_t>(total)), outR(static_cast<size_t>(total));
    std::vector<float> l(256), r(256);
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min(256, total - pos);
        for (int i = 0; i < n; ++i)
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = gen(pos + i);
        p.process(l.data(), r.data(), n);
        std::copy_n(l.data(), n, outL.data() + pos);
        std::copy_n(r.data(), n, outR.data() + pos);
    }
    if (rightOut != nullptr)
        *rightOut = outR;
    return outL;
}

} // namespace

TEST_CASE("DelayLine reads back an impulse at the requested delay")
{
    DelayLine d;
    d.prepare(1000);
    d.push(1.0f);
    for (int i = 0; i < 99; ++i)
        d.push(0.0f);
    REQUIRE(d.at(99) == 1.0f);
    REQUIRE(d.read(99.0f) == Approx(1.0f));
    REQUIRE(d.read(99.5f) == Approx(0.5625f).margin(0.1)); // between impulse and zero
}

TEST_CASE("Scale snaps to the nearest scale tone and walks degrees")
{
    Scale dMinor { kScaleTypes[1].mask, 2 };
    REQUIRE(dMinor.nearest(61.0f) == 60.0f);  // C# -> C (D minor has C, not C#)
    REQUIRE(dMinor.nearest(62.2f) == 62.0f);
    REQUIRE(dMinor.degreeToNote(50, 0) == 50.0f); // D3
    REQUIRE(dMinor.degreeToNote(50, 2) == 53.0f); // F3
    REQUIRE(dMinor.degreeToNote(50, 7) == 62.0f); // D4
    REQUIRE(dMinor.degreeToNote(50, -1) == 48.0f); // C3
}

TEST_CASE("Harmonic gravity migrates voices one by one over the morph time")
{
    HarmonicGravity g;
    g.snapTo({ kScaleTypes[0].mask, 0 });     // C major
    g.setMorphSeconds(10.0f);
    g.setTarget({ kScaleTypes[0].mask, 1 });  // C# major
    g.advance(5.0f);
    // Halfway: low-seed voices moved, high-seed voices have not.
    // C#: not in C major (snaps down to C), in C# major (stays).
    REQUIRE(g.quantize(61.0f, 0.1f, 1.0f) == 61.0f);
    REQUIRE(g.quantize(61.0f, 0.9f, 1.0f) == 60.0f);
    g.advance(5.0f);
    REQUIRE(g.quantize(61.0f, 0.9f, 1.0f) == 61.0f);
    REQUIRE_FALSE(g.isMorphing());
    REQUIRE(g.quantize(61.7f, 0.5f, 0.5f) == Approx(61.35f)); // halfway from 61.7 to C# (61)
}

TEST_CASE("Granular cloud is silent without a buffer and alive with one")
{
    GranularCloud cloud;
    cloud.prepare({ kFs, 512 }, 1);
    std::vector<float> l(256), r(256);
    cloud.process(l.data(), r.data(), 256, 1.0f);
    REQUIRE(peak(l) == 0.0f);

    const auto buf = makeTone(220.0f, 2.0f);
    cloud.setBuffer(&buf);
    GranularCloud::Params p;
    p.density = 40.0f;
    cloud.setParams(p);
    std::vector<float> all;
    for (int i = 0; i < 400; ++i)
    {
        cloud.process(l.data(), r.data(), 256, 1.0f);
        all.insert(all.end(), l.begin(), l.end());
    }
    REQUIRE(rms(all, 0, all.size()) > 0.02f);
    REQUIRE(peak(all) < 2.0f);
    REQUIRE(cloud.getActiveGrains() > 0);
}

TEST_CASE("Granular cloud respects its grain cap and never allocates")
{
    GranularCloud cloud;
    cloud.prepare({ kFs, 512 }, 2);
    const auto buf = makeTone(330.0f, 1.0f);
    cloud.setBuffer(&buf);
    GranularCloud::Params p;
    p.density = 2000.0f; // far more than the pool
    p.grainMs = 1000.0f;
    p.reverse = 0.5f;
    p.harmonize = 1.0f;
    cloud.setParams(p);
    cloud.setGrainLimit(20);
    HarmonicGravity h;
    h.snapTo({ kScaleTypes[0].mask, 0 });
    cloud.setHarmony(&h);

    std::vector<float> l(64), r(64);
    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int i = 0; i < 2000; ++i)
        {
            cloud.process(l.data(), r.data(), 64, 1.0f);
            REQUIRE(cloud.getActiveGrains() <= 20);
            for (float x : l)
                REQUIRE(std::isfinite(x));
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}

TEST_CASE("Resonator modes ring at their pitch, stay bounded and decay to zero")
{
    ResonatorBank bank;
    bank.prepare({ kFs, 512 }, 3);
    ResonatorBank::Params p;
    p.rootNote = 69.0f; // A4
    p.modes = 1;
    p.structure = 0.0f; // harmonic: mode 0 = root
    p.rain = 0.0f;
    p.decaySeconds = 2.0f;
    bank.setParams(p);

    std::vector<float> impulse(48000, 0.0f), l(48000), r(48000);
    impulse[0] = 1.0f;
    bank.process(impulse.data(), l.data(), r.data(), 48000, 1.0f);
    int crossings = 0;
    for (size_t i = 1; i < l.size(); ++i)
        crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
    REQUIRE(crossings / 2 == Approx(440).margin(3));

    // Max decay and continuous loud noise: must stay bounded (unity peak gain per mode).
    p.modes = 24;
    p.decaySeconds = 60.0f;
    p.structure = 1.0f;
    p.rain = 1.0f;
    bank.setParams(p);
    Random rng(1);
    std::vector<float> noise(48000);
    for (auto& x : noise)
        x = rng.nextBipolar();
    for (int s = 0; s < 10; ++s)
    {
        bank.process(noise.data(), l.data(), r.data(), 48000, 1.0f);
        for (float x : l)
            REQUIRE(std::isfinite(x));
        REQUIRE(peak(l) < 30.0f);
    }

    // Silence afterwards: decays to exact zero thanks to denormal flushing.
    p.rain = 0.0f;
    p.decaySeconds = 0.5f;
    bank.setParams(p);
    std::vector<float> silence(48000, 0.0f);
    for (int s = 0; s < 20; ++s)
        bank.process(silence.data(), l.data(), r.data(), 48000, 1.0f);
    REQUIRE(peak(l) == 0.0f);
}

TEST_CASE("Reverb tail decays to zero, and hold sustains without growing")
{
    FdnReverb rev;
    rev.prepare({ kFs, 512 });
    rev.setControls({ 0.6f, 0.4f, 0.6f, 0.0f, 0.3f, 0.0f }, {});
    const float decay = FdnReverb::decayFrom01(0.4f);
    REQUIRE(decay == Approx(2.48f).epsilon(0.02));

    auto out = runProcessor(rev, 30.0f, [](int i) { return i < 4800 ? (i % 7 == 0 ? 0.5f : -0.2f) : 0.0f; });
    REQUIRE(rms(out, 48000, 96000) > 1.0e-4f);
    REQUIRE(peak(std::vector<float>(out.end() - 48000, out.end())) == 0.0f);

    rev.reset();
    rev.setControls({ 0.6f, 0.4f, 0.6f, 0.0f, 0.3f, 1.0f }, {});
    // Feed a burst with hold off, then freeze.
    rev.setControls({ 0.6f, 0.4f, 0.6f, 0.0f, 0.3f, 0.0f }, {});
    runProcessor(rev, 0.3f, [](int i) { return (i % 13 == 0) ? 0.6f : -0.05f; });
    rev.setControls({ 0.6f, 0.4f, 0.6f, 0.0f, 0.3f, 1.0f }, {});
    out = runProcessor(rev, 40.0f, [](int) { return 0.3f; }); // input is muted by hold
    const float early = rms(out, 48000 * 2, 48000 * 4);
    const float late = rms(out, 48000 * 36, 48000 * 38);
    REQUIRE(early > 1.0e-3f);
    REQUIRE(late > early * 0.5f);  // sustains (within 6 dB)
    REQUIRE(late < early * 1.12f); // and does not grow
}

TEST_CASE("Tape delay echoes on time and self-oscillation stays bounded")
{
    TapeDelay d;
    d.prepare({ kFs, 512 });
    // 0.5 s: v = log(500/20)/log(100)
    const float v = std::log(25.0f) / std::log(100.0f);
    d.setControls({ v, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f }, {});
    runProcessor(d, 3.0f, [](int) { return 0.0f; }); // let the time glide settle
    auto out = runProcessor(d, 1.0f, [](int i) { return i == 0 ? 1.0f : 0.0f; });
    size_t peakAt = 0;
    for (size_t i = 0; i < out.size(); ++i)
        if (std::fabs(out[i]) > std::fabs(out[peakAt]))
            peakAt = i;
    REQUIRE(static_cast<double>(peakAt) == Approx(24000.0).margin(30.0));

    d.setControls({ 0.3f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, {}); // 110% feedback, max wobble/age
    Random rng(2);
    out = runProcessor(d, 20.0f, [&](int i) { return i < 48000 ? rng.nextBipolar() * 2.0f : 0.0f; });
    REQUIRE(peak(out) < 1.5f); // saturator bounds the loop at 1.2; Hermite reads may overshoot slightly
    for (float x : out)
        REQUIRE(std::isfinite(x));
}

TEST_CASE("Medium: digital is a clean delay; every type is bounded and finite")
{
    Medium m;
    m.prepare({ kFs, 512 }, 4);
    Medium::Params p;
    p.type = Medium::Type::Digital;
    m.setParams(p);
    const int lat = m.getLatencySamples();
    REQUIRE(lat == 144);

    std::vector<float> l(1024), r(1024), ref(1024);
    for (size_t i = 0; i < 1024; ++i)
        ref[i] = l[i] = r[i] = std::sin(0.01f * static_cast<float>(i));
    m.process(l.data(), r.data(), 1024);
    for (size_t i = static_cast<size_t>(lat); i < 1024; ++i)
        REQUIRE(l[i] == Approx(ref[i - static_cast<size_t>(lat)]).margin(1e-6));

    for (auto type : { Medium::Type::Cassette, Medium::Type::Vinyl, Medium::Type::Sampler })
    {
        m.reset();
        p.type = type;
        p.age = 1.0f;
        p.noise = 1.0f;
        p.wobble = 1.0f;
        p.drive = 1.0f;
        m.setParams(p);
        Random rng(5);
        for (int b = 0; b < 400; ++b)
        {
            for (size_t i = 0; i < 1024; ++i)
                l[i] = r[i] = rng.nextBipolar() * 1.5f;
            m.process(l.data(), r.data(), 1024);
            for (size_t i = 0; i < 1024; ++i)
            {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::fabs(l[i]) < 2.0f);
            }
        }
    }
}

TEST_CASE("Medium: tape hisses and vinyl crackles in silence; noise 0 is quiet")
{
    for (auto type : { Medium::Type::Cassette, Medium::Type::Vinyl })
    {
        Medium m;
        m.prepare({ kFs, 512 }, 6);
        Medium::Params p;
        p.type = type;
        p.noise = 1.0f;
        p.age = 0.6f;
        m.setParams(p);
        std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
        m.process(l.data(), r.data(), 48000);
        REQUIRE(rms(l, 0, l.size()) > dbToGain(-75.0f));

        p.noise = 0.0f;
        m.setParams(p);
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        m.process(l.data(), r.data(), 48000); // crossfade not involved: same type
        REQUIRE(rms(l, 24000, 48000) < dbToGain(-100.0f));
    }
}

TEST_CASE("Medium type changes crossfade without clicks")
{
    Medium m;
    m.prepare({ kFs, 512 }, 7);
    Medium::Params p;
    p.noise = 0.0f;
    p.drive = 0.0f;
    p.wobble = 0.0f;
    p.age = 0.0f;
    m.setParams(p);
    std::vector<float> l(256), r(256);
    float last = 0.0f;
    float worstJump = 0.0f;
    int sample = 0;
    for (int b = 0; b < 400; ++b)
    {
        if (b == 100)
        {
            p.type = Medium::Type::Cassette;
            m.setParams(p);
        }
        if (b == 250)
        {
            p.type = Medium::Type::Sampler;
            m.setParams(p);
        }
        for (size_t i = 0; i < 256; ++i, ++sample)
            l[i] = r[i] = 0.4f * std::sin(kTwoPi * 110.0f * static_cast<float>(sample) / static_cast<float>(kFs));
        m.process(l.data(), r.data(), 256);
        for (float x : l)
        {
            worstJump = std::max(worstJump, std::fabs(x - last));
            last = x;
        }
    }
    // A 110 Hz sine at 0.4 moves at most ~0.0058 per sample; allow coloration, not steps.
    REQUIRE(worstJump < 0.05f);
}

TEST_CASE("Every factory processor is realtime-safe and finite")
{
    for (const auto& entry : ProcessorFactory::instance().entries())
    {
        auto p = ProcessorFactory::instance().create(entry.info->typeId);
        REQUIRE(p != nullptr);
        p->prepare({ kFs, 512 });
        std::array<float, 6> c {};
        for (int i = 0; i < 6; ++i)
            c[static_cast<size_t>(i)] = entry.info->controls[static_cast<size_t>(i)].defaultValue;
        p->setControls(c, {});
        std::vector<float> l(512), r(512);
        Random rng(8);
        std::size_t allocations = 0;
        {
            const tf::test::ScopedAllocationCounter counter;
            for (int b = 0; b < 200; ++b)
            {
                for (size_t i = 0; i < 512; ++i)
                    l[i] = r[i] = rng.nextBipolar() * 0.5f;
                p->setControls(c, { 1.0f + static_cast<float>(b % 3) });
                p->process(l.data(), r.data(), 512);
            }
            allocations = counter.count();
        }
        INFO(entry.info->typeId);
        REQUIRE(allocations == 0);
        for (float x : l)
            REQUIRE(std::isfinite(x));
    }
    REQUIRE(ProcessorFactory::instance().create("") == nullptr);
    REQUIRE(ProcessorFactory::instance().create("tf.unknown") == nullptr);
}
