#include <dsp/spatial/Spatial.h>
#include <engine/Engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace tf;
using engine::Command;
using engine::ControlEvent;
using engine::P;
using engine::StripId;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

struct SpaceRig
{
    engine::Engine engine;
    int channels;
    std::vector<std::vector<float>> out;
    std::vector<std::vector<float>> scratch;

    explicit SpaceRig(int numChannels) : channels(numChannels)
    {
        engine.prepare(kFs, kBlock);
        scratch.assign(static_cast<std::size_t>(channels), std::vector<float>(kBlock));
        out.assign(static_cast<std::size_t>(channels), {});
        snap(P::MasterFadeSecs, 0.2f);
        for (const auto& s : engine::kStrips)
        {
            snap(s.level, -60.0f);
            snap(s.sendA, -60.0f);
            snap(s.sendB, -60.0f);
        }
        snap(engine::kStrips[0].level, 0.0f);
        snap(P::SpaceSpread, 0.0f);
        engine.command(Command::FadeIn);
    }

    void snap(P p, float v) { engine.post(ControlEvent::snapParam(engine::idx(p), v)); }

    void run(double seconds, bool keep = true)
    {
        const int blocks = static_cast<int>(seconds * kFs / kBlock);
        std::vector<float*> ptrs;
        for (auto& c : scratch)
            ptrs.push_back(c.data());
        for (int b = 0; b < blocks; ++b)
        {
            engine.process(nullptr, 0, ptrs.data(), channels, kBlock);
            if (keep)
                for (int c = 0; c < channels; ++c)
                    out[static_cast<std::size_t>(c)].insert(out[static_cast<std::size_t>(c)].end(), scratch[static_cast<std::size_t>(c)].begin(),
                                                            scratch[static_cast<std::size_t>(c)].end());
            engine::TelemetryFrame f;
            while (engine.popTelemetry(f))
                last = f;
            engine.collectGarbage();
        }
    }

    void clear()
    {
        for (auto& c : out)
            c.clear();
    }

    double rms(int channel) const
    {
        const auto& c = out[static_cast<std::size_t>(channel)];
        double sum = 0.0;
        for (float v : c)
            sum += static_cast<double>(v) * v;
        return c.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(c.size()));
    }

    int loudest() const
    {
        int best = 0;
        for (int c = 1; c < channels; ++c)
            if (rms(c) > rms(best))
                best = c;
        return best;
    }

    engine::TelemetryFrame last;
};

double db(double a, double b) { return 20.0 * std::log10(std::max(a, 1.0e-12) / std::max(b, 1.0e-12)); }
}

TEST_CASE("Ring panning keeps power constant and lands between the right speakers", "[spatial]")
{
    float g[dsp::kMaxSpeakers];
    for (int n : { 4, 6, 8 })
        for (float az = -180.0f; az < 180.0f; az += 7.5f)
        {
            dsp::ringGains(az, n, g);
            float power = 0.0f;
            int used = 0;
            for (int k = 0; k < dsp::kMaxSpeakers; ++k)
            {
                power += g[k] * g[k];
                used += g[k] > 1.0e-4f ? 1 : 0;
            }
            CHECK(power == Catch::Approx(1.0f).margin(1.0e-4));
            CHECK(used <= 2);
        }
    for (int n : { 4, 6, 8 })
        for (int k = 0; k < n; ++k)
        {
            dsp::ringGains(dsp::speakerAzimuth(k, n), n, g);
            CHECK(g[k] == Catch::Approx(1.0f).margin(1.0e-4));
        }
}

TEST_CASE("Quad output puts a source in the speaker it points at", "[spatial]")
{
    struct Case
    {
        float azimuth;
        int channel;
    };
    for (const auto c : { Case { -45.0f, 0 }, Case { 45.0f, 1 }, Case { -135.0f, 2 }, Case { 135.0f, 3 } })
    {
        SpaceRig r(4);
        r.snap(P::SpaceMode, 2.0f);
        r.snap(P::DroneAzimuth, c.azimuth);
        r.run(1.5, false);
        r.run(1.0);
        INFO("azimuth " << c.azimuth);
        CHECK(r.last.spaceChannels == 4);
        CHECK(r.loudest() == c.channel);
        for (int k = 0; k < 4; ++k)
            if (k != c.channel)
                CHECK(db(r.rms(c.channel), r.rms(k)) > 20.0);
    }
}

TEST_CASE("Eight speakers each get their own direction", "[spatial]")
{
    for (int k = 0; k < 8; ++k)
    {
        SpaceRig r(8);
        r.snap(P::SpaceMode, 4.0f);
        r.snap(P::DroneAzimuth, dsp::speakerAzimuth(k, 8));
        r.run(1.5, false);
        r.run(0.6);
        CHECK(r.loudest() == k);
    }
}

TEST_CASE("Spread opens a source across neighbouring speakers", "[spatial]")
{
    SpaceRig narrow(8), wide(8);
    for (auto* r : { &narrow, &wide })
    {
        r->snap(P::SpaceMode, 4.0f);
        r->snap(P::DroneAzimuth, dsp::speakerAzimuth(0, 8));
    }
    wide.snap(P::SpaceSpread, 1.0f);
    for (auto* r : { &narrow, &wide })
    {
        r->run(1.5, false);
        r->run(1.0);
    }
    auto lit = [](const SpaceRig& r) {
        int n = 0;
        for (int k = 0; k < 8; ++k)
            n += db(r.rms(k), r.rms(r.loudest())) > -20.0 ? 1 : 0;
        return n;
    };
    CHECK(lit(narrow) == 1);
    CHECK(lit(wide) >= 2);
}

TEST_CASE("Rotate carries the whole field around the room", "[spatial]")
{
    SpaceRig r(4);
    r.snap(P::SpaceMode, 2.0f);
    r.snap(P::DroneAzimuth, -45.0f);
    r.snap(P::SpaceRotate, 30.0f);
    r.run(1.0, false);
    const float startRotation = r.last.spaceRotation;
    r.run(3.0, false);
    r.clear();
    r.run(0.5);
    const float moved = dsp::wrapDegrees(r.last.spaceRotation - startRotation);
    CHECK(moved == Catch::Approx(105.0f).margin(8.0f));
    CHECK(r.loudest() == 1);
}

TEST_CASE("A ring mode with too few outputs falls back to stereo", "[spatial]")
{
    SpaceRig r(2);
    r.snap(P::SpaceMode, 4.0f);
    r.snap(P::DroneAzimuth, 135.0f);
    r.run(1.5, false);
    r.run(0.5);
    CHECK(r.last.spaceChannels == 0);
    CHECK(r.last.outputChannels == 2);
    CHECK(r.rms(0) > 1.0e-3);
    CHECK(r.rms(1) > 1.0e-3);
}

TEST_CASE("Stereo mode leaves extra outputs silent", "[spatial]")
{
    SpaceRig r(4);
    r.run(1.5, false);
    r.run(0.5);
    CHECK(r.rms(0) > 1.0e-3);
    CHECK(r.rms(2) == 0.0);
    CHECK(r.rms(3) == 0.0);
}

TEST_CASE("Headphone mode places a source to one side with level and time", "[spatial]")
{
    SpaceRig r(2);
    r.snap(P::SpaceMode, 1.0f);
    r.snap(P::DronePan, 0.0f);
    r.snap(P::DroneWidth, 0.0f);
    r.snap(P::DroneAzimuth, 90.0f);
    r.run(1.5, false);
    r.run(1.0);
    CHECK(r.last.spaceMode == 1);
    CHECK(db(r.rms(1), r.rms(0)) > 3.0);

    const auto& l = r.out[0];
    const auto& rr = r.out[1];
    int bestLag = 0;
    double best = -1.0e30;
    for (int lag = -60; lag <= 60; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 100; i + 100 < l.size(); ++i)
            sum += static_cast<double>(rr[i]) * l[static_cast<std::size_t>(static_cast<long>(i) + lag)];
        if (sum > best)
        {
            best = sum;
            bestLag = lag;
        }
    }
    INFO("left ear lags by " << bestLag << " samples");
    CHECK(bestLag >= 15);
    CHECK(bestLag <= 40);
}

TEST_CASE("Switching output mode ramps instead of clicking", "[spatial]")
{
    SpaceRig r(4);
    r.snap(P::DroneAzimuth, 45.0f);
    r.run(1.5, false);
    r.run(0.2);
    r.snap(P::SpaceMode, 2.0f);
    r.run(0.4);
    r.snap(P::SpaceMode, 1.0f);
    r.run(0.4);
    float jump = 0.0f, peak = 0.0f;
    for (const auto& c : r.out)
        for (std::size_t i = 1; i < c.size(); ++i)
        {
            jump = std::max(jump, std::fabs(c[i] - c[i - 1]));
            peak = std::max(peak, std::fabs(c[i]));
        }
    INFO("largest step " << jump << " against a peak of " << peak);
    CHECK(jump < 0.25f * peak);
    CHECK(peak < 0.9f);
}
