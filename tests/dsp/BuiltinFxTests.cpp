#include "support/AllocationGuard.h"

#include <dsp/core/Denormal.h>
#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/fx/ProcessorFactory.h>
#include <dsp/fx/delay/GrainDelay.h>
#include <dsp/fx/drive/LoFi.h>
#include <dsp/fx/drive/Saturator.h>
#include <dsp/fx/dynamics/GlueCompressor.h>
#include <dsp/fx/filter/MultimodeFilter.h>
#include <dsp/fx/modulation/Phaser.h>
#include <dsp/fx/modulation/Tremolo.h>
#include <dsp/fx/pitch/PitchShimmer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace tf::dsp;

namespace {
constexpr int kBlock = 256;
constexpr double kFs = 48000.0;

const char* const kNewTypes[] = { "tf.filter", "tf.pitchShimmer", "tf.phaser", "tf.tremolo",
                                  "tf.saturator", "tf.grainDelay", "tf.compressor", "tf.lofi" };

using Controls = std::array<float, 6>;
using Signal = std::function<float(int)>;

struct Stereo
{
    std::vector<float> l, r;
};

Signal sine(float hz, float gain, double fs = kFs)
{
    return [hz, gain, fs](int i) { return gain * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * i / fs)); };
}

Stereo run(Processor& p, const Signal& in, double seconds, double fs = kFs)
{
    const int total = static_cast<int>(seconds * fs);
    Stereo out;
    out.l.resize(static_cast<std::size_t>(total));
    out.r.resize(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i)
        out.l[static_cast<std::size_t>(i)] = out.r[static_cast<std::size_t>(i)] = in(i);
    for (int pos = 0; pos < total; pos += kBlock)
        p.process(out.l.data() + pos, out.r.data() + pos, std::min(kBlock, total - pos));
    return out;
}

template <typename T>
std::unique_ptr<T> make(const Controls& c, double fs = kFs)
{
    auto p = std::make_unique<T>();
    p->prepare({ fs, kBlock });
    p->setControls(c, {});
    p->reset();
    return p;
}

double rms(const std::vector<float>& x, double fromSeconds = 0.0, double toSeconds = 1.0e9, double fs = kFs)
{
    const auto from = std::min(x.size(), static_cast<std::size_t>(fromSeconds * fs));
    const auto to = std::min(x.size(), static_cast<std::size_t>(toSeconds * fs));
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return to > from ? std::sqrt(s / static_cast<double>(to - from)) : 0.0;
}

double db(double v) { return 20.0 * std::log10(std::max(v, 1.0e-12)); }

double toneLevel(const std::vector<float>& x, double hz, double fromSeconds, double fs = kFs)
{
    const auto from = static_cast<std::size_t>(fromSeconds * fs);
    double re = 0.0, im = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
    {
        const double ph = 2.0 * 3.14159265358979 * hz * static_cast<double>(i) / fs;
        re += x[i] * std::cos(ph);
        im += x[i] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size() - from);
}

double crossingHz(const std::vector<float>& x, double fromSeconds, double fs = kFs)
{
    int crossings = 0;
    const auto from = static_cast<std::size_t>(fromSeconds * fs);
    for (std::size_t i = from + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            ++crossings;
    return crossings / (static_cast<double>(x.size() - from) / fs);
}

Controls defaults(const ProcessorInfo& info)
{
    Controls c {};
    for (std::size_t k = 0; k < c.size(); ++k)
        c[k] = info.controls[k].defaultValue;
    return c;
}
}

TEST_CASE("New effects stay finite and bounded at every extreme and sample rate", "[fx][builtin]")
{
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
        for (const char* type : kNewTypes)
        {
            INFO(type << " at " << fs);
            auto p = ProcessorFactory::instance().create(type);
            REQUIRE(p != nullptr);
            p->prepare({ fs, kBlock });
            Random rng(23);
            std::vector<float> l(kBlock), r(kBlock);
            float peak = 0.0f;
            std::size_t allocations = 0;
            {
                tf::test::ScopedAllocationCounter counter;
                const int blocks = static_cast<int>(6.0 * fs / kBlock);
                for (int b = 0; b < blocks; ++b)
                {
                    const int phase = b / std::max(1, blocks / 12);
                    Controls c {};
                    for (std::size_t k = 0; k < c.size(); ++k)
                        c[k] = phase % 3 == 0 ? 1.0f : (phase % 3 == 1 ? 0.0f : ((phase + static_cast<int>(k)) % 2 == 0 ? 1.0f : 0.0f));
                    if (phase >= 9)
                        for (auto& v : c)
                            v = rng.nextFloat();
                    p->setControls(c, { phase % 4 == 3 ? 16.0f : 1.0f, nullptr, phase % 5 == 4 ? 0.5f : 0.0f });
                    const bool loud = phase % 2 == 0;
                    for (int i = 0; i < kBlock; ++i)
                    {
                        const float dc = phase == 6 ? 1.0f : 0.0f;
                        l[static_cast<std::size_t>(i)] = loud ? 1.5f * rng.nextBipolar() + dc : (phase == 5 ? 1.0e-30f : 0.0f);
                        r[static_cast<std::size_t>(i)] = loud ? 1.5f * rng.nextBipolar() - dc : 0.0f;
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

TEST_CASE("New effects reset to a clean, reproducible state", "[fx][builtin]")
{
    for (const char* type : kNewTypes)
    {
        INFO(type);
        auto p = ProcessorFactory::instance().create(type);
        p->prepare({ kFs, kBlock });
        const Controls c = defaults(p->info());
        p->setControls(c, {});
        p->reset();
        const auto first = run(*p, sine(330.0f, 0.4f), 1.0);

        p->setControls({ 1.0f, 1.0f, 0.3f, 1.0f, 0.9f, 0.7f }, {});
        Random rng(3);
        run(*p, [&rng](int) { return rng.nextBipolar(); }, 0.7);
        p->setControls(c, {});
        p->reset();
        const auto second = run(*p, sine(330.0f, 0.4f), 1.0);
        CHECK(first.l == second.l);
        CHECK(first.r == second.r);
    }
}

TEST_CASE("New effects fall silent with silent input", "[fx][builtin]")
{
    for (const char* type : kNewTypes)
    {
        INFO(type);
        auto p = ProcessorFactory::instance().create(type);
        p->prepare({ kFs, kBlock });
        Controls c = defaults(p->info());
        if (std::string(type) == "tf.lofi")
            c[2] = 0.0f;
        p->setControls(c, {});
        run(*p, sine(220.0f, 0.5f), 1.0);
        const auto tail = run(*p, [](int) { return 0.0f; }, 25.0);
        CHECK(rms(tail.l, 24.0) < 1.0e-5);
        CHECK(rms(tail.r, 24.0) < 1.0e-5);
    }
}

TEST_CASE("Filter passes below its cutoff in low mode and above it in high mode", "[fx][builtin][filter]")
{
    const float cutoff01 = std::log(500.0f / 20.0f) / std::log(1000.0f);
    auto level = [&](float mode, float hz) {
        auto f = make<MultimodeFilter>({ mode, cutoff01, 0.0f, 0.0f, 0.0f, 0.0f });
        return db(rms(run(*f, sine(hz, 0.1f), 0.5).l, 0.2));
    };
    const double ref = db(0.1 / std::sqrt(2.0));
    CHECK(level(0.1f, 100.0f) > ref - 1.5);
    CHECK(level(0.1f, 5000.0f) < ref - 30.0);
    CHECK(level(0.6f, 5000.0f) > ref - 1.5);
    CHECK(level(0.6f, 50.0f) < ref - 20.0);
    CHECK(level(0.9f, 500.0f) < ref - 20.0);
    CHECK(level(0.9f, 4000.0f) > ref - 1.5);
}

TEST_CASE("Filter stays stable with full resonance, drive and sweep", "[fx][builtin][filter]")
{
    for (const double fs : { 44100.0, 96000.0 })
        for (float mode : { 0.1f, 0.35f, 0.6f, 0.9f })
        {
            auto f = make<MultimodeFilter>({ mode, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, fs);
            const auto out = run(*f, sine(60.0f, 1.0f, fs), 2.0, fs);
            float peak = 0.0f;
            for (float v : out.l)
            {
                REQUIRE(std::isfinite(v));
                peak = std::max(peak, std::fabs(v));
            }
            CHECK(peak < 8.0f);
        }
}

TEST_CASE("Pitch shimmer shifts by its interval", "[fx][builtin][shimmer]")
{
    auto shifted = [](float interval01) {
        auto s = make<PitchShimmer>({ interval01, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f });
        return crossingHz(run(*s, sine(300.0f, 0.3f), 1.5).l, 0.5);
    };
    CHECK(std::fabs(shifted(4.5f / 7.0f) / 600.0 - 1.0) < 0.05);
    CHECK(std::fabs(shifted(3.5f / 7.0f) / (300.0 * std::exp2(7.0 / 12.0)) - 1.0) < 0.05);
    CHECK(std::fabs(shifted(0.5f / 7.0f) / 150.0 - 1.0) < 0.05);
}

TEST_CASE("Pitch shimmer feedback builds a bounded tail", "[fx][builtin][shimmer]")
{
    auto tailLevel = [](float shimmer) {
        auto s = make<PitchShimmer>({ 4.5f / 7.0f, shimmer, 1.0f, 0.5f, 0.0f, 0.0f });
        run(*s, sine(200.0f, 0.5f), 0.5);
        return rms(run(*s, [](int) { return 0.0f; }, 1.0).l, 0.5);
    };
    CHECK(tailLevel(1.0f) > 100.0 * tailLevel(0.0f) + 1.0e-4);
    auto s = make<PitchShimmer>({ 4.5f / 7.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f });
    const auto out = run(*s, sine(200.0f, 1.0f), 20.0);
    float peak = 0.0f;
    for (float v : out.l)
        peak = std::max(peak, std::fabs(v));
    CHECK(peak < 2.0f);
}

TEST_CASE("Phaser cuts a notch at the centre with two still stages", "[fx][builtin][phaser]")
{
    const float centre01 = std::log(800.0f / 100.0f) / std::log(40.0f);
    auto level = [&](float hz) {
        auto p = make<Phaser>({ 0.0f, 0.0f, 0.5f, 0.1f, centre01, 0.0f });
        return db(rms(run(*p, sine(hz, 0.3f), 0.5).l, 0.2));
    };
    CHECK(level(800.0f) < level(10000.0f) - 25.0);
    CHECK(level(30.0f) > db(0.3 / std::sqrt(2.0)) - 1.0);
}

TEST_CASE("Phaser stays stable at full feedback either sign", "[fx][builtin][phaser]")
{
    for (float fb : { 0.0f, 1.0f })
    {
        auto p = make<Phaser>({ 1.0f, 1.0f, fb, 0.9f, 1.0f, 1.0f });
        const auto out = run(*p, sine(1200.0f, 1.0f), 3.0);
        for (float v : out.l)
            REQUIRE(std::fabs(v) < 4.0f);
    }
}

TEST_CASE("Tremolo modulates level at its rate and pans with Stereo", "[fx][builtin][tremolo]")
{
    const float rate01 = std::log(4.0f / 0.05f) / std::log(400.0f);
    auto envelope = [](const std::vector<float>& x) {
        std::vector<double> e;
        const std::size_t w = 480;
        for (std::size_t i = static_cast<std::size_t>(0.5 * kFs); i + w <= x.size(); i += w)
        {
            double s = 0.0;
            for (std::size_t k = i; k < i + w; ++k)
                s += static_cast<double>(x[k]) * x[k];
            e.push_back(std::sqrt(s / w));
        }
        return e;
    };
    {
        auto t = make<Tremolo>({ rate01, 1.0f, 0.1f, 0.0f, 0.0f, 0.0f });
        const auto e = envelope(run(*t, sine(1000.0f, 0.3f), 2.0).l);
        CHECK(*std::max_element(e.begin(), e.end()) > 8.0 * *std::min_element(e.begin(), e.end()));
    }
    {
        auto t = make<Tremolo>({ rate01, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f });
        const auto e = envelope(run(*t, sine(1000.0f, 0.3f), 2.0).l);
        CHECK(*std::max_element(e.begin(), e.end()) < 1.05 * *std::min_element(e.begin(), e.end()));
    }
    {
        auto t = make<Tremolo>({ rate01, 1.0f, 0.1f, 1.0f, 0.0f, 0.0f });
        const auto out = run(*t, sine(1000.0f, 0.3f), 2.0);
        const auto el = envelope(out.l), er = envelope(out.r);
        double ml = 0.0, mr = 0.0, cov = 0.0;
        for (std::size_t i = 0; i < el.size(); ++i)
        {
            ml += el[i];
            mr += er[i];
        }
        ml /= static_cast<double>(el.size());
        mr /= static_cast<double>(er.size());
        for (std::size_t i = 0; i < el.size(); ++i)
            cov += (el[i] - ml) * (er[i] - mr);
        CHECK(cov < 0.0);
    }
}

TEST_CASE("Tremolo square edges are softened, never stepped", "[fx][builtin][tremolo]")
{
    auto t = make<Tremolo>({ 0.6f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f });
    const auto out = run(*t, [](int) { return 0.5f; }, 2.0);
    float jump = 0.0f;
    for (std::size_t i = 1; i < out.l.size(); ++i)
        jump = std::max(jump, std::fabs(out.l[i] - out.l[i - 1]));
    CHECK(jump < 0.02f);
}

TEST_CASE("Saturator adds harmonics with drive, even ones with bias, and keeps level", "[fx][builtin][saturator]")
{
    auto harmonics = [](float drive, float bias) {
        auto s = make<Saturator>({ drive, bias, 1.0f, 0.0f, 1.0f, 0.5f });
        const auto out = run(*s, sine(200.0f, 0.25f), 1.0);
        return std::array<double, 3> { toneLevel(out.l, 200.0, 0.3), toneLevel(out.l, 400.0, 0.3), toneLevel(out.l, 600.0, 0.3) };
    };
    const auto clean = harmonics(0.0f, 0.0f);
    const auto hot = harmonics(1.0f, 0.0f);
    const auto biased = harmonics(0.6f, 1.0f);
    CHECK(hot[2] / hot[0] > 30.0 * clean[2] / clean[0]);
    CHECK(biased[1] / biased[0] > 0.05);
    CHECK(hot[1] / hot[0] < 0.01);
    CHECK(std::fabs(db(hot[0]) - db(clean[0])) < 6.0);
    CHECK(std::fabs(db(clean[0]) - db(0.25)) < 1.0);
}

TEST_CASE("Grain delay repeats around its time and is deterministic", "[fx][builtin][grain]")
{
    const Controls c { 0.5f, 0.3f, 0.6f, 0.0f, 0.0f, 0.0f };
    const int burst = static_cast<int>(0.05 * kFs);
    auto burstIn = [burst](int i) { return i < burst ? 0.5f * static_cast<float>(std::sin(0.05 * i)) : 0.0f; };
    auto a = make<GrainDelay>(c);
    auto b = make<GrainDelay>(c);
    const auto outA = run(*a, burstIn, 1.5);
    const auto outB = run(*b, burstIn, 1.5);
    CHECK(outA.l == outB.l);
    const double timeSeconds = 0.02 * std::pow(100.0, 0.5);
    CHECK(rms(outA.l, 0.0, timeSeconds - 0.05) < 1.0e-6);
    CHECK(rms(outA.l, timeSeconds - 0.03, timeSeconds + 0.1) > 0.02);
    CHECK(rms(outA.l, 0.7) < 1.0e-6);
    CHECK(a->getActiveGrains() <= GrainDelay::kMaxGrains);
}

TEST_CASE("Grain delay feedback repeats but stays bounded", "[fx][builtin][grain]")
{
    auto g = make<GrainDelay>({ 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
    const auto out = run(*g, sine(300.0f, 1.0f), 10.0);
    for (float v : out.l)
        REQUIRE(std::fabs(v) < 4.0f);
    auto h = make<GrainDelay>({ 0.3f, 0.3f, 0.6f, 0.0f, 0.8f, 0.0f });
    run(*h, [](int i) { return i < 2400 ? 0.5f : 0.0f; }, 0.1);
    CHECK(rms(run(*h, [](int) { return 0.0f; }, 1.0).l) > 1.0e-3);
}

TEST_CASE("Compressor reduces loud signals, leaves quiet ones and moves smoothly", "[fx][builtin][compressor]")
{
    const float threshold01 = (-30.0f + 48.0f) / 48.0f;
    auto c = make<GlueCompressor>({ threshold01, 0.9f, 0.3f, 0.3f, 0.0f, 0.0f });
    const auto loud = run(*c, sine(300.0f, 0.5f), 1.0);
    CHECK(db(rms(loud.l, 0.5)) < db(0.5 / std::sqrt(2.0)) - 15.0);
    CHECK(c->getGainReductionDb() > 15.0f);

    auto q = make<GlueCompressor>({ threshold01, 0.9f, 0.3f, 0.3f, 0.0f, 0.0f });
    const auto quiet = run(*q, sine(300.0f, 0.005f), 1.0);
    CHECK(std::fabs(db(rms(quiet.l, 0.5)) - db(0.005 / std::sqrt(2.0))) < 0.5);

    auto m = make<GlueCompressor>({ threshold01, 0.9f, 0.3f, 0.3f, 1.0f, 0.0f });
    CHECK(db(rms(run(*m, sine(300.0f, 0.005f), 1.0).l, 0.5)) > db(0.005 / std::sqrt(2.0)) + 17.0);

    auto s = make<GlueCompressor>({ threshold01, 0.4f, 0.6f, 0.5f, 0.0f, 0.3f });
    float previous = 0.0f, step = 0.0f;
    std::vector<float> l(32), r(32);
    for (int b = 0; b < static_cast<int>(2.0 * kFs / 32); ++b)
    {
        for (int i = 0; i < 32; ++i)
        {
            const int t = b * 32 + i;
            const float level = (t / 24000) % 2 == 0 ? 0.6f : 0.02f;
            l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = level * static_cast<float>(std::sin(0.03 * t));
        }
        s->process(l.data(), r.data(), 32);
        step = std::max(step, std::fabs(s->getGainReductionDb() - previous));
        previous = s->getGainReductionDb();
    }
    CHECK(step < 3.0f);
}

TEST_CASE("Compressor sidechain high-pass ignores the lows", "[fx][builtin][compressor]")
{
    const float threshold01 = (-30.0f + 48.0f) / 48.0f;
    auto open = make<GlueCompressor>({ threshold01, 0.9f, 0.3f, 0.3f, 0.0f, 0.0f });
    auto filtered = make<GlueCompressor>({ threshold01, 0.9f, 0.3f, 0.3f, 0.0f, 1.0f });
    run(*open, sine(40.0f, 0.5f), 1.0);
    run(*filtered, sine(40.0f, 0.5f), 1.0);
    CHECK(open->getGainReductionDb() > filtered->getGainReductionDb() + 6.0f);
}

TEST_CASE("Lo-fi crushes bits, lowers the rate and adds noise", "[fx][builtin][lofi]")
{
    auto render = [](const Controls& c, float hz) { return run(*make<LoFi>(c), sine(hz, 0.4f), 1.0).l; };
    const Controls clean { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    const auto reference = render(clean, 440.0f);
    auto error = [&](const std::vector<float>& x) {
        double e = 0.0;
        for (std::size_t i = static_cast<std::size_t>(0.2 * kFs); i < x.size(); ++i)
            e += (static_cast<double>(x[i]) - reference[i]) * (static_cast<double>(x[i]) - reference[i]);
        return e;
    };
    auto crushed = clean;
    crushed[0] = 1.0f;
    auto mid = clean;
    mid[0] = 0.5f;
    CHECK(error(render(crushed, 440.0f)) > 10.0 * error(render(mid, 440.0f)));
    CHECK(error(render(mid, 440.0f)) > 1.0e-4);

    auto slow = clean;
    slow[1] = 1.0f;
    CHECK(toneLevel(render(slow, 5000.0f), 5000.0, 0.2) < 0.25 * toneLevel(render(clean, 5000.0f), 5000.0, 0.2));

    auto noisy = clean;
    noisy[2] = 1.0f;
    auto silentRun = [](const Controls& c) { return rms(run(*make<LoFi>(c), [](int) { return 0.0f; }, 0.5).l); };
    CHECK(silentRun(noisy) > 1.0e-3);
    CHECK(silentRun(clean) < 1.0e-7);
}

TEST_CASE("Lo-fi rate and bits glide without zipper steps", "[fx][builtin][lofi]")
{
    auto l = make<LoFi>({ 0.0f, 0.0f, 0.0f, 0.0f, 0.4f, 0.0f });
    run(*l, sine(200.0f, 0.3f), 0.3);
    l->setControls({ 1.0f, 1.0f, 0.0f, 0.0f, 0.4f, 0.0f }, {});
    const auto out = run(*l, sine(200.0f, 0.3f), 0.05);
    float jump = 0.0f;
    for (std::size_t i = 1; i < 240; ++i)
        jump = std::max(jump, std::fabs(out.l[i] - out.l[i - 1]));
    CHECK(jump < 0.1f);
}
