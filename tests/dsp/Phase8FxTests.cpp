#include "support/AllocationGuard.h"

#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/fx/ProcessorFactory.h>
#include <dsp/fx/delay/WornEcho.h>
#include <dsp/fx/spectral/SpectralBlur.h>
#include <dsp/fx/strings/SympatheticStrings.h>
#include <dsp/harmony/HarmonicGravity.h>
#include <dsp/harmony/Scale.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace tf::dsp;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

double energy(const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > x.size())
        to = x.size();
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return s;
}
}

TEST_CASE("Every registered processor survives extreme and random controls", "[fx][fuzz]")
{
    for (const auto& entry : ProcessorFactory::instance().entries())
    {
        INFO(entry.info->typeId);
        auto p = entry.create();
        p->prepare({ kFs, kBlock });
        Random rng(17);
        std::vector<float> l(kBlock), r(kBlock);
        float peak = 0.0f;
        std::size_t allocations = 0;
        {
            tf::test::ScopedAllocationCounter counter;
            for (int b = 0; b < 1500; ++b)
            {
                if (b % 50 == 0)
                {
                    std::array<float, 6> c {};
                    for (auto& v : c)
                        v = b % 300 == 0 ? (b % 600 == 0 ? 1.0f : 0.0f) : rng.nextFloat();
                    p->setControls(c, { b % 100 == 0 ? 8.0f : 1.0f, nullptr });
                }
                for (int i = 0; i < kBlock; ++i)
                {
                    const bool loud = (b / 100) % 2 == 0;
                    l[static_cast<std::size_t>(i)] = loud ? rng.nextBipolar() : 0.0f;
                    r[static_cast<std::size_t>(i)] = loud ? rng.nextBipolar() : 0.0f;
                }
                p->process(l.data(), r.data(), kBlock);
                for (int i = 0; i < kBlock; ++i)
                {
                    REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
                    REQUIRE(std::isfinite(r[static_cast<std::size_t>(i)]));
                    peak = std::max({ peak, std::fabs(l[static_cast<std::size_t>(i)]), std::fabs(r[static_cast<std::size_t>(i)]) });
                }
            }
            allocations = counter.count();
        }
        CHECK(allocations == 0);
        CHECK(peak < 16.0f);
    }
}

TEST_CASE("Spectral blur at rest resynthesises its input", "[fx][blur]")
{
    SpectralBlur blur;
    blur.prepare({ kFs, kBlock });
    blur.setControls({ 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f }, {});
    Random rng(5);
    const int total = 188 * kBlock;
    std::vector<float> in(static_cast<std::size_t>(total)), outL(in.size()), outR(in.size());
    for (auto& x : in)
        x = 0.3f * rng.nextBipolar();
    for (int pos = 0; pos < total; pos += kBlock)
    {
        std::copy_n(in.data() + pos, kBlock, outL.data() + pos);
        std::copy_n(in.data() + pos, kBlock, outR.data() + pos);
        blur.process(outL.data() + pos, outR.data() + pos, kBlock);
    }
    const int latency = SpectralBlur::kSize;
    double err = 0.0, sig = 0.0;
    for (int i = latency + SpectralBlur::kSize; i < total; ++i)
    {
        const double d = outL[static_cast<std::size_t>(i)] - in[static_cast<std::size_t>(i - latency)];
        err += d * d;
        sig += static_cast<double>(in[static_cast<std::size_t>(i - latency)]) * in[static_cast<std::size_t>(i - latency)];
    }
    CHECK(10.0 * std::log10(err / sig) < -60.0);
}

TEST_CASE("Sympathetic strings tune to the key and ring for notes in it", "[fx][strings]")
{
    HarmonicGravity key;
    Scale dMinor;
    dMinor.mask = kScaleTypes[1].mask;
    dMinor.root = 2;
    key.snapTo(dMinor);

    auto response = [&](float note) {
        SympatheticStrings s;
        s.prepare({ kFs, kBlock });
        s.setControls({ 0.6f, 0.5f, 0.5f, 0.4f, 0.5f, 1.0f }, { 1.0f, &key });
        std::vector<float> l(kBlock), r(kBlock), wet;
        const float hz = midiToHz(note);
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float t = static_cast<float>(b * kBlock + i) / static_cast<float>(kFs);
                l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = b < 200 ? 0.2f * std::sin(kTwoPi * hz * t) : 0.0f;
            }
            s.process(l.data(), r.data(), kBlock);
            if (b >= 200)
                wet.insert(wet.end(), l.begin(), l.end());
        }
        return energy(wet);
    };

    SympatheticStrings probe;
    probe.prepare({ kFs, kBlock });
    probe.setControls({ 0.6f, 0.5f, 0.5f, 0.4f, 0.5f, 1.0f }, { 1.0f, &key });
    for (int i = 0; i < probe.getStringCount(); ++i)
        CHECK(dMinor.contains(static_cast<int>(std::lround(probe.getStringNote(i))) % 12));

    const float inKey = probe.getStringNote(4);
    const float outKey = inKey - 0.5f;
    CHECK(response(inKey) > 8.0 * response(outKey));
}

TEST_CASE("Worn echo repeats at the set time and wears down", "[fx][echo]")
{
    WornEcho echo;
    echo.prepare({ kFs, kBlock });
    echo.setControls({ 0.5f, 0.5f / 1.05f, 0.0f, 0.3f, 0.0f, 0.0f }, {});
    std::vector<float> l(kBlock), r(kBlock), out;
    for (int b = 0; b < static_cast<int>(1.5 * kFs / kBlock); ++b)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        echo.process(l.data(), r.data(), kBlock);
    }
    const int burst = static_cast<int>(0.01 * kFs);
    for (int b = 0; b < static_cast<int>(1.0 * kFs / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const int t = b * kBlock + i;
            l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] =
                t < burst ? 0.8f * std::sin(kTwoPi * 600.0f * static_cast<float>(t) / static_cast<float>(kFs)) : 0.0f;
        }
        echo.process(l.data(), r.data(), kBlock);
        out.insert(out.end(), l.begin(), l.end());
    }
    auto window = [&](double from, double to) {
        return energy(out, static_cast<std::size_t>(from * kFs), static_cast<std::size_t>(to * kFs));
    };
    const double gap = window(0.08, 0.18);
    const double first = window(0.195, 0.22);
    const double second = window(0.395, 0.42);
    CHECK(first > 100.0 * gap);
    CHECK(second < first);
    CHECK(second > 0.05 * first);
}
