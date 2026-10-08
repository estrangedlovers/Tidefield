#include "ControlAudit.h"

#include <dsp/fx/delay/TapeDelay.h>
#include <dsp/fx/delay/WornEcho.h>
#include <engine/capture/CatchManager.h>
#include <engine/mod/ModRouteManager.h>

#include <catch2/catch_approx.hpp>
#include <set>
#include <catch2/catch_template_test_macros.hpp>

using namespace tf::audit;
using Catch::Approx;

namespace {
const std::shared_ptr<tf::dsp::SampleBuffer>& plucks()
{
    static const auto b = pluckTrain();
    return b;
}

const std::shared_ptr<tf::dsp::SampleBuffer>& shot()
{
    static const auto b = oneShot();
    return b;
}

Check sweep(P p, std::string context, Setup setup, Metric m = Metric::Any, int direction = 0)
{
    Check c;
    c.name = spec(p).id;
    c.context = std::move(context);
    c.param = p;
    c.setup = std::move(setup);
    c.metric = m;
    c.direction = direction;
    return c;
}

Check timed(Check c, double seconds, double from, double to = 0.0)
{
    c.seconds = seconds;
    c.from = from;
    c.to = to;
    return c;
}

void playBloomNotes(Rig& r, double secs)
{
    r.run(0.02);
    r.note(60, 0.9f);
    r.note(67, 0.7f);
    r.run(0.4);
    r.off(60);
    r.off(67);
    r.run(std::max(0.0, secs - 0.42));
}

void playLoop(Rig& r, double secs)
{
    r.run(0.05);
    r.engine.command(Command::LoopRecord);
    r.run(1.0);
    r.engine.command(Command::LoopRecord);
    r.run(std::max(0.0, secs - 1.05));
}

void playFreeze(Rig& r, double secs)
{
    r.run(1.0);
    r.snap(P::FreezeOn, 1.0f);
    r.run(std::max(0.0, secs - 1.0));
}

void playInputHold(Rig& r, double secs)
{
    r.run(0.5);
    r.snap(P::InputFreeze, 1.0f);
    r.run(std::max(0.0, secs - 0.5));
}

Setup droneCtx() { return [](Rig& r) { r.solo({ StripId::Drone }); }; }

StripId cloudStrip(int k) { return static_cast<StripId>(static_cast<int>(StripId::Cloud1) + k); }

Setup cloudCtx(int k)
{
    return [k](Rig& r) {
        r.solo({ cloudStrip(k) });
        r.engine.loadCloudSample(k, plucks());
    };
}

Setup resCtx()
{
    return [](Rig& r) {
        r.solo({ StripId::Resonator });
        r.snap(P::ResRain, 1.0f);
    };
}

Setup inputCtx(float gain = 1.0f)
{
    return [gain](Rig& r) {
        r.solo({ StripId::Input });
        r.input = testInput(gain);
        r.snap(P::InputArmed, 1.0f);
    };
}

Setup bloomCtx()
{
    return [](Rig& r) {
        r.solo({ StripId::Bloom });
        r.engine.loadBloomSample(shot());
        r.snap(P::BloomRoot, 60.0f);
    };
}

Setup weatherCtx()
{
    return [](Rig& r) {
        r.solo({ StripId::Weather });
        r.snap(P::WeatherWind, 0.5f);
        r.snap(P::WeatherRain, 0.3f);
    };
}

Setup then(Setup a, Setup b)
{
    return [a = std::move(a), b = std::move(b)](Rig& r) {
        if (a)
            a(r);
        if (b)
            b(r);
    };
}

P cloudParam(int k, int offset) { return static_cast<P>(idx(kCloudFirstParam[static_cast<std::size_t>(k)]) + offset); }

P slotParam(int slot, int control) { return static_cast<P>(idx(kFxSlots[static_cast<std::size_t>(slot)].firstParam) + control); }

float periodJitter(const Rig& r, double from)
{
    std::vector<double> crossings;
    for (std::size_t i = std::max<std::size_t>(1, r.at(from)); i < r.out.size(); ++i)
        if (r.out.l[i - 1] < 0.0f && r.out.l[i] >= 0.0f)
            crossings.push_back(static_cast<double>(i - 1) + r.out.l[i - 1] / static_cast<double>(r.out.l[i - 1] - r.out.l[i]));
    if (crossings.size() < 4)
        return 0.0f;
    std::vector<double> periods;
    for (std::size_t i = 1; i < crossings.size(); ++i)
        periods.push_back(crossings[i] - crossings[i - 1]);
    double mean = 0.0;
    for (double p : periods)
        mean += p;
    mean /= static_cast<double>(periods.size());
    double var = 0.0;
    for (double p : periods)
        var += (p - mean) * (p - mean);
    return static_cast<float>(10000.0 * std::sqrt(var / static_cast<double>(periods.size())) / mean);
}

float peakFraction(const Rig& r, double from, double hz)
{
    constexpr int kOrder = 13;
    constexpr int kSize = 1 << kOrder;
    tf::dsp::Fft fft;
    fft.prepare(kOrder);
    std::vector<tf::dsp::Fft::Complex> buf(kSize);
    double near = 0.0, total = 0.0;
    const double binHz = kFs / kSize;
    for (std::size_t start = r.at(from); start + kSize <= r.out.size(); start += kSize / 2)
    {
        for (int i = 0; i < kSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos(tf::dsp::kTwoPi * static_cast<float>(i) / kSize);
            const auto k = start + static_cast<std::size_t>(i);
            buf[static_cast<std::size_t>(i)] = { 0.5f * (r.out.l[k] + r.out.r[k]) * w, 0.0f };
        }
        fft.forward(buf.data());
        for (int k = 1; k <= kSize / 2; ++k)
        {
            const double p = std::norm(buf[static_cast<std::size_t>(k)]);
            total += p;
            if (std::fabs(k * binHz - hz) <= hz * 0.012)
                near += p;
        }
    }
    return total > 0.0 ? static_cast<float>(100.0 * near / total) : 0.0f;
}

float countChanges(const Rig& r, std::function<float(const TelemetryFrame&)> fn)
{
    float changes = 0.0f;
    for (std::size_t i = 1; i < r.frames.size(); ++i)
        changes += std::fabs(fn(r.frames[i]) - fn(r.frames[i - 1])) > 1.0e-4f ? 1.0f : 0.0f;
    return changes;
}

float frameRmsSpread(const Rig& r, double from)
{
    const auto hop = static_cast<std::size_t>(0.05 * kFs);
    std::vector<float> db;
    for (std::size_t s = r.at(from); s + hop <= r.out.size(); s += hop)
    {
        double e = 0.0;
        for (std::size_t i = s; i < s + hop; ++i)
            e += static_cast<double>(r.out.l[i]) * r.out.l[i] + static_cast<double>(r.out.r[i]) * r.out.r[i];
        db.push_back(toDb(e / static_cast<double>(2 * hop)));
    }
    double mean = 0.0;
    for (float v : db)
        mean += v;
    mean /= static_cast<double>(std::max<std::size_t>(1, db.size()));
    double var = 0.0;
    for (float v : db)
        var += (v - mean) * (v - mean);
    return static_cast<float>(std::sqrt(var / static_cast<double>(std::max<std::size_t>(1, db.size()))));
}

std::vector<Check> stripChecks(StripId s, const std::string& name, Setup ctx, Play play, double seconds, double from, Setup widthCtx = {},
                               Play widthPlay = {})
{
    const auto& info = kStrips[static_cast<std::size_t>(s)];
    std::vector<Check> v;
    auto add = [&](P p, Setup setup, Metric m, int dir, float minDelta, bool halves = true) {
        auto c = timed(sweep(p, name, std::move(setup), m, dir), seconds, from);
        c.play = play;
        c.minDelta = minDelta;
        c.checkHalves = halves;
        v.push_back(std::move(c));
    };
    add(info.level, ctx, Metric::Rms, 1, 12.0f);
    add(info.pan, ctx, Metric::Balance, 1, 2.0f);
    add(info.width, widthCtx ? widthCtx : ctx, Metric::Side, 1, 2.0f);
    if (widthCtx && widthPlay)
        v.back().play = widthPlay;
    add(info.sendA, then(ctx, [](Rig& r) { r.effect(kBusASlot, "tf.reverb"); }), Metric::Rms, 1, 0.2f, false);
    add(info.sendB, then(ctx, [](Rig& r) { r.effect(kBusBSlot, "tf.delay"); }), Metric::Rms, 1, 0.2f, false);
    return v;
}

std::vector<Check> stripGroup(StripId s)
{
    switch (s)
    {
        case StripId::Drone: return stripChecks(s, "drone solo", droneCtx(), {}, 2.5, 1.0);
        case StripId::Cloud1:
        case StripId::Cloud2:
        case StripId::Cloud3:
        case StripId::Cloud4:
        {
            const int k = static_cast<int>(s) - static_cast<int>(StripId::Cloud1);
            return stripChecks(s, "cloud " + std::to_string(k + 1) + " with plucks", cloudCtx(k), {}, 2.0, 0.5);
        }
        case StripId::Resonator: return stripChecks(s, "resonator rain", resCtx(), {}, 2.0, 0.5);
        case StripId::Input:
            return stripChecks(s, "test tone input, monitored", inputCtx(), {}, 1.5, 0.5,
                               [](Rig& r) {
                                   r.solo({ StripId::Input });
                                   r.input = testInput();
                               },
                               playInputHold);
        case StripId::Bloom: return stripChecks(s, "bloom notes", bloomCtx(), playBloomNotes, 2.0, 0.2);
        case StripId::Loop:
            return stripChecks(s, "loop of the mix (weather), weather then muted", [](Rig& r) {
                r.solo({ StripId::Weather, StripId::Loop });
                r.snap(P::WeatherWind, 0.5f);
                r.snap(P::WeatherRain, 0.3f);
                r.snap(P::LoopSource, 1.0f);
            }, [](Rig& r, double secs) {
                playLoop(r, 1.1);
                r.snap(P::WeatherLevel, -60.0f);
                r.run(secs - 1.1);
            }, 3.0, 1.5);
        case StripId::Weather: return stripChecks(s, "wind and rain", weatherCtx(), {}, 2.0, 0.5);
        case StripId::Freeze:
            return stripChecks(s, "weather frozen after 1 s, weather then muted", [](Rig& r) {
                r.solo({ StripId::Weather, StripId::Freeze });
                r.snap(P::WeatherWind, 0.5f);
                r.snap(P::WeatherRain, 0.3f);
            }, [](Rig& r, double secs) {
                playFreeze(r, 1.2);
                r.snap(P::WeatherLevel, -60.0f);
                r.run(secs - 1.2);
            }, 3.0, 1.8);
        case StripId::Count: break;
    }
    return {};
}

std::vector<Check> masterChecks()
{
    std::vector<Check> v;
    v.push_back(timed(sweep(P::MasterLevel, "drone solo", droneCtx(), Metric::Rms, 1), 2.0, 1.0));
    v.back().minDelta = 40.0f;
    {
        auto c = timed(sweep(P::MasterCeiling, "drone driven +18 dB into the limiter", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::DroneLevel, 6.0f);
                           r.snap(P::MasterLevel, 6.0f);
                           r.snap(P::DroneDensity, 6.0f);
                           r.snap(P::DroneResonance, 0.9f);
                       }, Metric::Custom, 1), 2.0, 1.0);
        c.measure = [](Rig& r, const Features& f) {
            const float ceiling = r.last().paramTargets[idx(P::MasterCeiling)];
            CHECK(20.0f * std::log10(f.peak) <= ceiling + 0.01f);
            CHECK(20.0f * std::log10(f.peak) >= ceiling - 3.0f);
            return 20.0f * std::log10(f.peak);
        };
        c.minDelta = 10.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::MasterAuto, "drone solo, quiet", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::DroneLevel, -18.0f);
                       }), 6.0, 4.0);
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::MasterAutoTarget, "auto master on, drone solo", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::MasterAuto, 1.0f);
                       }, Metric::Rms, 1), 12.0, 10.0);
        c.minDelta = 5.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::MasterAutoAmount, "auto master on, dark drone", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::MasterAuto, 1.0f);
                           r.snap(P::DroneCutoff, 200.0f);
                       }, Metric::Custom, 1), 10.0, 7.0);
        c.context += ", total tonal correction";
        c.measure = [](Rig& r, const Features&) {
            const auto& a = r.last().autoMaster;
            return std::fabs(a[2]) + std::fabs(a[3]) + std::fabs(a[4]) + 10.0f * std::fabs(a[5] - 1.0f);
        };
        c.minDelta = 3.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::BusALevel, "drone sent at 0 dB to the reverb bus", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::DroneSendA, 0.0f);
                           r.effect(kBusASlot, "tf.reverb");
                       }, Metric::Rms, 1), 2.5, 1.0);
        c.minDelta = 2.0f;
        c.checkHalves = false;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::BusBLevel, "drone sent at 0 dB to the delay bus", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::DroneSendB, 0.0f);
                           r.effect(kBusBSlot, "tf.delay");
                       }, Metric::Rms, 1), 2.5, 1.0);
        c.minDelta = 2.0f;
        c.checkHalves = false;
        v.push_back(c);
    }
    return v;
}

float levelWobble(const Rig& r, double from, double to)
{
    const std::size_t win = r.at(0.02);
    std::vector<double> levels;
    for (std::size_t i = r.at(from); i + win <= std::min(r.out.size(), r.at(to)); i += win)
    {
        double e = 0.0;
        for (std::size_t k = i; k < i + win; ++k)
            e += static_cast<double>(r.out.l[k]) * r.out.l[k] + static_cast<double>(r.out.r[k]) * r.out.r[k];
        levels.push_back(10.0 * std::log10(std::max(1.0e-12, e / static_cast<double>(win))));
    }
    double mean = 0.0, var = 0.0;
    for (double v : levels)
        mean += v;
    mean /= static_cast<double>(std::max<std::size_t>(1, levels.size()));
    for (double v : levels)
        var += (v - mean) * (v - mean);
    return static_cast<float>(std::sqrt(var / static_cast<double>(std::max<std::size_t>(1, levels.size()))));
}

float pitchWobble(const Rig& r, double from, double to)
{
    const std::size_t win = r.at(0.05);
    std::vector<double> periods;
    for (std::size_t i = r.at(from); i + win <= std::min(r.out.size(), r.at(to)); i += win)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (std::size_t k = i + 1; k < i + win; ++k)
            if (r.out.l[k - 1] < 0.0f && r.out.l[k] >= 0.0f)
            {
                const double frac = static_cast<double>(-r.out.l[k - 1]) / static_cast<double>(r.out.l[k] - r.out.l[k - 1]);
                const double t = static_cast<double>(k - 1) + frac;
                if (first < 0.0)
                    first = t;
                last = t;
                ++crossings;
            }
        if (crossings > 2)
            periods.push_back((last - first) / static_cast<double>(crossings - 1));
    }
    double mean = 0.0, var = 0.0;
    for (double v : periods)
        mean += v;
    mean /= static_cast<double>(std::max<std::size_t>(1, periods.size()));
    for (double v : periods)
        var += (v - mean) * (v - mean);
    return mean > 0.0 ? static_cast<float>(1200.0 * std::sqrt(var / static_cast<double>(std::max<std::size_t>(1, periods.size()))) / mean) : 0.0f;
}

Setup pureDrone()
{
    return [](Rig& r) {
        r.snap(P::DroneDensity, 1.0f);
        r.snap(P::DroneDetune, 0.0f);
        r.snap(P::DroneShape, 1.0f);
        r.snap(P::DroneDriftDepth, 0.0f);
        r.snap(P::DroneNoise, 0.0f);
        r.snap(P::DroneCutoff, 12000.0f);
        r.snap(P::DroneEvolve, 0.0f);
    };
}

std::vector<Check> droneChecks()
{
    const std::string ctx = "drone solo";
    std::vector<Check> v;
    auto add = [&](P p, Metric m = Metric::Any, int dir = 0, Setup extra = {}) {
        v.push_back(timed(sweep(p, ctx, extra ? then(droneCtx(), extra) : droneCtx(), m, dir), 2.5, 1.0));
        return &v.back();
    };
    add(P::DroneRoot, Metric::Centroid, 1);
    add(P::DroneDetune);
    add(P::DroneShape, Metric::Centroid, -1);
    add(P::DroneCutoff, Metric::Centroid, 1)->minDelta = 12.0f;
    add(P::DroneResonance);
    add(P::DroneNoise);
    add(P::DroneDriftDepth);
    add(P::DroneDriftRate);
    add(P::DroneDensity, Metric::Rms, 1)->minDelta = 3.0f;
    {
        auto* c = add(P::DroneEvolve, Metric::Custom, 1, [](Rig& r) {
            r.snap(P::TideRate, 8.0f);
            r.snap(P::DroneDensity, 6.0f);
        });
        c->context = "drone solo, tide 8x";
        c->seconds = 4.0;
        c->measure = [](Rig& r, const Features&) {
            float n = 0.0f;
            for (int k = 0; k < 6; ++k)
                n += countChanges(r, [k](const TelemetryFrame& f) { return f.droneVoiceInterval[static_cast<std::size_t>(k)]; });
            return n;
        };
        c->minDelta = 2.0f;
        c->checkHalves = false;
    }
    add(P::DroneSpread, Metric::Side, 1)->minDelta = 6.0f;
    {
        auto* c = add(P::DroneGravity, Metric::Custom, -1, [](Rig& r) {
            r.snap(P::DroneRoot, 39.4f);
            r.snap(P::DroneDensity, 6.0f);
            r.snap(P::HarmonyGravity, 1.0f);
            r.snap(P::DroneDriftDepth, 0.0f);
        });
        c->context = "drone root between D# and E, key D minor, global gravity 100%";
        c->seconds = 4.0;
        c->from = 3.0;
        c->measure = [](Rig& r, const Features&) {
            float off = 0.0f;
            for (float n : r.last().droneVoiceNote)
                off += std::fabs(n - std::round(n));
            return off;
        };
        c->minDelta = 1.0f;
    }
    add(P::DroneWave, Metric::Any, 0, [](Rig& r) { r.snap(P::DroneShape, 0.5f); });
    add(P::DroneSub, Metric::Centroid, -1)->minDelta = 3.0f;
    add(P::DroneFmRatio, Metric::Centroid, 1, [](Rig& r) {
        r.snap(P::DroneWave, 4.0f);
        r.snap(P::DroneShape, 1.0f);
        r.snap(P::DroneCutoff, 12000.0f);
    });
    add(P::DroneTilt, Metric::Centroid, -1, [](Rig& r) {
        r.snap(P::DroneDensity, 6.0f);
        r.snap(P::DroneCutoff, 12000.0f);
    });
    add(P::DroneFilterType, Metric::Centroid, 1);
    add(P::DroneKeyTrack, Metric::Centroid, 1, [](Rig& r) { r.snap(P::DroneDensity, 6.0f); });
    {
        auto* c = add(P::DroneDrive, Metric::Custom, 1, [](Rig& r) {
            pureDrone()(r);
            r.snap(P::DroneDensity, 3.0f);
        });
        c->context = "drone solo, pure sines, upper bands against the whole";
        c->measure = [](Rig&, const Features& f) {
            float upper = kFloorDb;
            for (std::size_t b = 5; b < f.bandDb.size(); ++b)
                upper = std::max(upper, f.bandDb[b]);
            return upper - f.rmsDb;
        };
        c->minDelta = 6.0f;
    }
    add(P::DroneBreathTone, Metric::Centroid, 1, [](Rig& r) {
        r.snap(P::DroneNoise, 1.0f);
        r.snap(P::DroneCutoff, 12000.0f);
    });
    {
        auto* c = add(P::DroneTremolo, Metric::Custom, 1, [](Rig& r) { r.snap(P::DroneTremoloRate, 4.0f); });
        c->measure = [](Rig& r, const Features&) { return levelWobble(r, 1.0, 2.5); };
        c->minDelta = 1.5f;
    }
    {
        auto* c = add(P::DroneTremoloRate, Metric::Custom, 1, [](Rig& r) { r.snap(P::DroneTremolo, 1.0f); });
        c->measure = [](Rig& r, const Features&) { return levelWobble(r, 1.0, 2.5); };
        c->minDelta = 1.0f;
        c->checkHalves = false;
    }
    {
        auto* c = add(P::DroneVibrato, Metric::Custom, 1, [](Rig& r) {
            pureDrone()(r);
            r.snap(P::DroneVibratoRate, 3.0f);
        });
        c->measure = [](Rig& r, const Features&) { return pitchWobble(r, 1.0, 2.5); };
        c->minDelta = 5.0f;
    }
    {
        auto* c = add(P::DroneVibratoRate, Metric::Custom, 1, [](Rig& r) {
            pureDrone()(r);
            r.snap(P::DroneVibrato, 1.0f);
        });
        c->measure = [](Rig& r, const Features&) { return pitchWobble(r, 1.0, 2.5); };
        c->minDelta = 5.0f;
        c->checkHalves = false;
    }
    return v;
}

std::vector<Check> cloudChecks(int k)
{
    const std::string ctx = "cloud " + std::to_string(k + 1) + " with plucks";
    std::vector<Check> v;
    auto add = [&](int offset, Metric m = Metric::Any, int dir = 0, Setup extra = {}) {
        v.push_back(timed(sweep(cloudParam(k, offset), ctx, extra ? then(cloudCtx(k), extra) : cloudCtx(k), m, dir), 2.0, 0.5));
        return &v.back();
    };
    auto grains = [k](Rig& r, const Features&) { return r.meanOver(0.5, 2.0, [k](const TelemetryFrame& f) { return static_cast<float>(f.cloudGrainCount[static_cast<std::size_t>(k)]); }); };
    {
        auto* c = add(0, Metric::Custom, 1);
        c->measure = grains;
        c->minDelta = 5.0f;
    }
    {
        auto* c = add(1, Metric::Custom, 1);
        c->measure = grains;
        c->minDelta = 3.0f;
    }
    add(2, Metric::Centroid, 1, [k](Rig& r) { r.snap(cloudParam(k, 3), 0.0f); })->minDelta = 12.0f;
    add(3);
    add(4, Metric::Any, 0, [](Rig& r) { r.snap(P::TideRate, 8.0f); })->context = ctx + ", tide 8x";
    add(5, Metric::Centroid, 1)->minDelta = 24.0f;
    {
        auto* c = add(6, Metric::Custom, -1, [k](Rig& r) { r.engine.loadCloudSample(k, sineSample(2000.0f)); });
        c->context = "cloud " + std::to_string(k + 1) + " with a 2 kHz sine, energy at the true pitch";
        c->measure = [](Rig& r, const Features&) { return peakFraction(r, 0.5, 2000.0); };
        c->minDelta = 10.0f;
    }
    add(7);
    add(8);
    add(9);
    add(10, Metric::Side, 1)->minDelta = 10.0f;
    {
        auto* c = add(11, Metric::Any, 0, [k](Rig& r) {
            r.snap(cloudParam(k, 5), 0.5f);
            r.snap(cloudParam(k, 6), 0.0f);
            r.snap(P::HarmonyGravity, 1.0f);
        });
        c->context = ctx + ", pitched a quarter-tone off the key";
    }
    return v;
}

std::vector<Check> resonatorChecks()
{
    const std::string ctx = "resonator rain";
    std::vector<Check> v;
    auto add = [&](P p, Metric m = Metric::Any, int dir = 0, Setup extra = {}) {
        v.push_back(timed(sweep(p, ctx, extra ? then(resCtx(), extra) : resCtx(), m, dir), 2.0, 0.5));
        return &v.back();
    };
    add(P::ResRoot, Metric::Centroid, 1)->minDelta = 6.0f;
    add(P::ResModes)->minChange = 0.25f;
    add(P::ResStructure);
    add(P::ResDecay, Metric::Rms, 1)->minDelta = 3.0f;
    add(P::ResBrightness, Metric::Centroid, 1);
    add(P::ResRain, Metric::Rms, 1)->minDelta = 10.0f;
    add(P::ResRainColour, Metric::Centroid, 1);
    add(P::ResSpread, Metric::Side, 1)->minDelta = 6.0f;
    {
        auto* c = add(P::ResGravity, Metric::Any, 0, [](Rig& r) {
            r.snap(P::ResStructure, 0.25f);
            r.snap(P::HarmonyGravity, 1.0f);
        });
        c->context = "resonator rain, structure 25% (between strings and chord)";
    }
    {
        auto* c = add(P::ResGravity, Metric::Any, 0, [](Rig& r) {
            r.snap(P::ResStructure, 0.75f);
            r.snap(P::HarmonyGravity, 1.0f);
        });
        c->context = "resonator rain, structure 75% (between chord and bells)";
    }
    auto excite = [&](P p, Setup source, std::string context, Play play = {}) {
        auto* c = add(p, Metric::Rms, 1, then(std::move(source), [](Rig& r) { r.snap(P::ResRain, 0.0f); }));
        c->context = std::move(context);
        c->play = std::move(play);
        c->minDelta = 12.0f;
        c->checkHalves = false;
    };
    excite(P::ResExciteInput, [](Rig& r) { r.input = testInput(); }, "no rain, test tone input");
    excite(P::ResExciteDrone, [](Rig&) {}, "no rain, drone muted but running");
    excite(P::ResExciteClouds, [](Rig& r) { r.engine.loadCloudSample(0, plucks()); }, "no rain, cloud 1 playing plucks but muted");
    excite(P::ResExciteBloom, [](Rig& r) { r.engine.loadBloomSample(shot()); }, "no rain, bloom notes muted", playBloomNotes);
    return v;
}

std::vector<Check> inputChecks()
{
    const std::string ctx = "test tone input, monitored";
    std::vector<Check> v;
    auto add = [&](P p, Metric m = Metric::Any, int dir = 0, Setup extra = {}) {
        v.push_back(timed(sweep(p, ctx, extra ? then(inputCtx(), extra) : inputCtx(), m, dir), 1.5, 0.5));
        return &v.back();
    };
    add(P::InputChannel);
    add(P::InputGain, Metric::Rms, 1)->minDelta = 20.0f;
    add(P::InputHighPass, Metric::Centroid, 1)->minDelta = 3.0f;
    {
        auto* c = add(P::InputGate, Metric::Rms, -1, [](Rig& r) { r.snap(P::InputGain, -24.0f); });
        c->context = "test tone input at -24 dB gain";
        c->minDelta = 20.0f;
        c->checkHalves = false;
    }
    add(P::InputArmed, Metric::Rms, 1)->minDelta = 40.0f;
    {
        auto* c = add(P::InputFreeze, Metric::Rms, 1, [](Rig& r) { r.snap(P::InputArmed, 0.0f); });
        c->context = "test tone input, not monitored, hold pressed after 0.5 s";
        c->apply = [](Rig&, float) {};
        c->play = [](Rig& r, double secs) {
            r.run(0.5);
            r.snap(P::InputFreeze, r.swept);
            r.run(secs - 0.5);
        };
        c->from = 1.0;
        c->minDelta = 20.0f;
    }
    {
        auto* c = add(P::InputFreezeLevel, Metric::Rms, 1, [](Rig& r) { r.snap(P::InputArmed, 0.0f); });
        c->context = "input held as a pad, not monitored";
        c->play = playInputHold;
        c->from = 1.0;
        c->minDelta = 20.0f;
    }
    {
        auto* c = add(P::InputFreezeDrift, Metric::Any, 0, [](Rig& r) { r.snap(P::InputArmed, 0.0f); });
        c->context = "input held as a pad, not monitored";
        c->play = playInputHold;
        c->seconds = 2.5;
        c->from = 1.0;
    }
    return v;
}

std::vector<Check> bloomChecks()
{
    const std::string ctx = "bloom, two notes held 0.4 s";
    std::vector<Check> v;
    auto add = [&](P p, Metric m = Metric::Any, int dir = 0, Setup extra = {}) {
        auto c = timed(sweep(p, ctx, extra ? then(bloomCtx(), extra) : bloomCtx(), m, dir), 2.5, 0.2);
        c.play = playBloomNotes;
        v.push_back(std::move(c));
        return &v.back();
    };
    add(P::BloomTransform);
    static const char* transforms[] = { "Swell", "Smear", "Freeze", "Ghost", "Constellation", "Tape" };
    for (int t = 0; t < 6; ++t)
    {
        auto* c = add(P::BloomAmount, Metric::Any, 0, [t](Rig& r) { r.snap(P::BloomTransform, static_cast<float>(t)); });
        c->context = ctx + ", transform " + transforms[t];
        c->name += std::string(" (") + transforms[t] + ")";
    }
    {
        auto* c = add(P::BloomLength, Metric::Rms, 1);
        c->seconds = 5.0;
        c->from = 1.0;
        c->minDelta = 3.0f;
    }
    {
        auto* c = add(P::BloomAttack, Metric::Rms, -1, [](Rig& r) { r.snap(P::BloomTransform, 1.0f); });
        c->context = "bloom Smear, first 0.4 s";
        c->from = 0.0;
        c->to = 0.4;
        c->minDelta = 6.0f;
    }
    {
        auto* c = add(P::BloomRelease, Metric::Rms, 1, [](Rig& r) {
            r.snap(P::BloomLength, 0.5f);
            r.snap(P::BloomTransform, 1.0f);
        });
        c->context = "bloom Smear, notes released at 0.5 s";
        c->seconds = 4.0;
        c->from = 1.5;
        c->minDelta = 6.0f;
    }
    add(P::BloomPitch, Metric::Centroid, 1)->minDelta = 24.0f;
    add(P::BloomTone, Metric::Centroid, 1)->minDelta = 3.0f;
    add(P::BloomSpread, Metric::Side, 1)->minDelta = 3.0f;
    {
        auto* c = add(P::BloomRandom, Metric::Custom, 1);
        c->context = ctx + ", detune of the played notes";
        c->measure = [](Rig& r, const Features&) {
            float cents = 0.0f;
            for (const auto& f : r.frames)
                for (const auto& v : f.bloomVoices)
                    if (v.active)
                        cents = std::max(cents, 100.0f * std::fabs(v.note - std::round(v.note)));
            return cents;
        };
        c->minDelta = 1.0f;
    }
    {
        auto* c = add(P::BloomPosition, Metric::Any, 0, [](Rig& r) { r.snap(P::BloomTransform, 2.0f); });
        c->context = "bloom Freeze";
    }
    {
        auto* c = add(P::BloomGravity, Metric::Any, 0, [](Rig& r) {
            r.snap(P::BloomPitch, 0.5f);
            r.snap(P::BloomRandom, 0.0f);
            r.snap(P::HarmonyGravity, 1.0f);
        });
        c->context = "bloom pitched a quarter-tone off the key";
    }
    return v;
}

std::vector<Check> bloomRootChecks()
{
    auto c = timed(sweep(P::BloomRoot, "bloom Tape, one note", bloomCtx(), Metric::Centroid, -1), 0.8, 0.05);
    c.setup = then(bloomCtx(), [](Rig& r) { r.snap(P::BloomTransform, 5.0f); });
    c.play = [](Rig& r, double secs) {
        r.run(0.01);
        r.note(60, 0.9f);
        r.run(secs - 0.01);
    };
    c.minChange = 0.4f;
    c.minDelta = 48.0f;
    return { c };
}

std::vector<Check> looperChecks()
{
    const std::string ctx = "loop of the test tone input";
    Setup base = [](Rig& r) {
        r.solo({ StripId::Loop });
        r.input = testInput();
    };
    std::vector<Check> v;
    {
        auto c = timed(sweep(P::LoopSource, "loop recorded over drone and input", [](Rig& r) {
                           r.solo({ StripId::Drone, StripId::Loop });
                           r.snap(P::DroneLevel, -10.0f);
                           r.input = testInput();
                       }), 2.5, 1.5);
        c.play = playLoop;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::LoopErosion, ctx + ", passes 3 to 5", base, Metric::Rms, -1), 6.0, 4.0);
        c.play = playLoop;
        c.minDelta = 1.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::LoopFlakes, ctx + ", erosion 100%, level variation", then(base, [](Rig& r) { r.snap(P::LoopErosion, 1.0f); }),
                             Metric::Custom, 1), 8.0, 1.5);
        c.play = playLoop;
        c.measure = [](Rig& r, const Features&) { return frameRmsSpread(r, 1.5); };
        c.minDelta = 0.3f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::LoopOverdub, ctx + ", overdubbing from 1.5 s", base, Metric::Rms, 1), 4.0, 2.6);
        c.play = [](Rig& r, double secs) {
            playLoop(r, 1.5);
            r.engine.command(Command::LoopRecord);
            r.run(secs - 1.5);
        };
        c.minDelta = 1.0f;
        v.push_back(c);
    }
    return v;
}

std::vector<Check> weatherChecks()
{
    std::vector<Check> v;
    auto layer = [&](P p) {
        auto c = timed(sweep(p, "weather solo, one layer", [](Rig& r) { r.solo({ StripId::Weather }); }, Metric::Rms, 1), 2.0, 0.5);
        c.minDelta = 20.0f;
        c.checkHalves = true;
        v.push_back(c);
    };
    layer(P::WeatherWind);
    layer(P::WeatherRain);
    {
        auto c = timed(sweep(P::WeatherSurf, "weather solo, surf only", [](Rig& r) { r.solo({ StripId::Weather }); }, Metric::Rms, 1), 3.0, 0.5);
        c.minDelta = 20.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::WeatherGust, "wind only, tide 8x", [](Rig& r) {
                           r.solo({ StripId::Weather });
                           r.snap(P::WeatherWind, 0.6f);
                           r.snap(P::TideRate, 8.0f);
                       }, Metric::Custom, 1), 4.0, 1.0);
        c.measure = [](Rig& r, const Features&) { return frameRmsSpread(r, 1.0); };
        c.minDelta = 1.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::WeatherGust, "rain only, tide 8x", [](Rig& r) {
                           r.solo({ StripId::Weather });
                           r.snap(P::WeatherRain, 0.6f);
                           r.snap(P::TideRate, 8.0f);
                       }, Metric::Custom, 1), 4.0, 1.0);
        c.name += " (rain)";
        c.measure = [](Rig& r, const Features&) { return frameRmsSpread(r, 1.0); };
        c.minDelta = 0.5f;
        v.push_back(c);
    }
    v.push_back(timed(sweep(P::WeatherTone, "wind and rain", weatherCtx(), Metric::Centroid, 1), 2.0, 0.5));
    {
        auto c = timed(sweep(P::WeatherDistance, "wind and rain", weatherCtx(), Metric::Centroid, -1), 2.0, 0.5);
        c.minDelta = 6.0f;
        v.push_back(c);
    }
    return v;
}

std::vector<Check> freezeChecks()
{
    Setup base = [](Rig& r) { r.solo({ StripId::Drone, StripId::Freeze }); };
    std::vector<Check> v;
    {
        auto c = timed(sweep(P::FreezeOn, "drone, freeze pressed after 1 s", base), 3.0, 1.6);
        c.apply = [](Rig&, float) {};
        c.play = [](Rig& r, double secs) {
            r.run(1.0);
            r.snap(P::FreezeOn, r.swept);
            r.run(secs - 1.0);
        };
        c.measure = [](Rig& r, const Features&) { return 10.0f * r.last().freezeGain; };
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::FreezeDuck, "drone, frozen after 1 s", base, Metric::Rms, -1), 3.0, 1.6);
        c.play = playFreeze;
        c.minDelta = 1.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::FreezeTexture, "drone, frozen after 1 s", base), 3.0, 1.6);
        c.play = playFreeze;
        v.push_back(c);
    }
    return v;
}

std::vector<Check> gestureChecks()
{
    std::vector<Check> v;
    Setup droneVerb = [](Rig& r) {
        r.solo({ StripId::Drone });
        r.effect(kBusASlot, "tf.reverb");
        r.effect(kBusBSlot, "tf.delay");
    };
    {
        auto c = timed(sweep(P::SwellHold, "drone with reverb and delay, held", droneVerb, Metric::Centroid, 1), 5.0, 3.5);
        c.minDelta = 2.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::SwellDepth, "drone with reverb and delay, swell held", then(droneVerb, [](Rig& r) { r.snap(P::SwellHold, 1.0f); }),
                             Metric::Centroid, 1), 5.0, 3.5);
        c.minDelta = 2.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::HushHold, "drone with reverb, held", droneVerb, Metric::Rms, -1), 3.0, 2.0);
        c.minDelta = 6.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::HushDepth, "drone with reverb, hush held", then(droneVerb, [](Rig& r) { r.snap(P::HushHold, 1.0f); }), Metric::Rms, -1),
                       3.0, 2.0);
        c.minDelta = 10.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::SlowHold, "resonator rain, held", resCtx(), Metric::Custom, -1), 4.0, 3.0);
        c.measure = [](Rig& r, const Features&) { return 10.0f * r.last().tide; };
        c.minDelta = 7.0f;
        v.push_back(c);
    }
    v.push_back(timed(sweep(P::PerformColour, "drone solo", droneCtx(), Metric::Centroid, 1), 2.5, 1.0));
    v.back().minDelta = 6.0f;
    {
        auto c = timed(sweep(P::PerformSpace, "drone with reverb", droneVerb, Metric::Side, 1), 3.0, 1.5);
        c.minDelta = 3.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::SeasonsDepth, "season on drone brightness at its peak", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           auto set = std::make_unique<SeasonSet>();
                           set->count = 1;
                           set->seasons[0].param = idx(P::DroneCutoff);
                           set->seasons[0].depth = 0.6f;
                           set->seasons[0].periodSeconds = 600.0f;
                           set->seasons[0].phase = 0.25f;
                           set->version = 1;
                           r.engine.publishSeasons(std::move(set));
                       }, Metric::Centroid, 1), 2.5, 1.0);
        c.minDelta = 6.0f;
        v.push_back(c);
    }
    return v;
}

std::vector<Check> harmonyChecks()
{
    std::vector<Check> v;
    Setup base = [](Rig& r) {
        r.solo({ StripId::Drone });
        r.snap(P::DroneRoot, 39.0f);
        r.snap(P::DroneDensity, 6.0f);
        r.snap(P::HarmonyGravity, 1.0f);
        r.snap(P::HarmonyMorph, 0.5f);
    };
    auto noteSignature = [](Rig& r, const Features&) {
        float s = 0.0f;
        const auto& f = r.last();
        for (std::size_t k = 0; k < f.loopNote.size(); ++k)
            s += f.loopNote[k] * static_cast<float>(k + 1) * 0.37f;
        for (std::size_t k = 0; k < f.droneVoiceNote.size(); ++k)
            s += f.droneVoiceNote[k] * static_cast<float>(k + 3) * 0.11f;
        return s;
    };
    {
        auto c = timed(sweep(P::HarmonyRoot, "drone on D#, gravity 100%, morph 0.5 s", base), 1.5, 1.0);
        c.measure = noteSignature;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::HarmonyScale, "drone on D#, gravity 100%, morph 0.5 s", base), 1.5, 1.0);
        c.measure = noteSignature;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::HarmonyGravity, "drone a quarter-tone off the key", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           r.snap(P::DroneRoot, 39.4f);
                           r.snap(P::DroneDensity, 6.0f);
                           r.snap(P::DroneDriftDepth, 0.0f);
                       }, Metric::Custom, -1), 4.0, 3.0);
        c.measure = [](Rig& r, const Features&) {
            float off = 0.0f;
            for (float n : r.last().droneVoiceNote)
                off += std::fabs(n - std::round(n));
            return off;
        };
        c.minDelta = 1.0f;
        v.push_back(c);
    }
    return v;
}

std::vector<Check> mediumChecks()
{
    std::vector<Check> v;
    Setup cassette = [](Rig& r) {
        r.solo({ StripId::Drone });
        r.snap(P::MediumType, 1.0f);
    };
    v.push_back(timed(sweep(P::MediumType, "drone solo", droneCtx()), 2.0, 1.0));
    v.push_back(timed(sweep(P::MediumAge, "drone solo on cassette", cassette, Metric::Centroid, -1), 2.0, 1.0));
    v.push_back(timed(sweep(P::MediumNoise, "drone solo on cassette", cassette), 2.0, 1.0));
    auto wobble = [&](int type, const char* name) {
        auto c = timed(sweep(P::MediumWobble, std::string("2 kHz input tone on ") + name + ", pitch jitter", [type](Rig& r) {
                           r.solo({ StripId::Input });
                           r.input = sineInput(2000.0);
                           r.snap(P::InputArmed, 1.0f);
                           r.snap(P::InputHighPass, 20.0f);
                           r.snap(P::MediumType, static_cast<float>(type));
                       }, Metric::Custom, 1), 2.5, 0.5);
        c.measure = [](Rig& r, const Features&) { return periodJitter(r, 0.5); };
        c.minDelta = 2.0f;
        if (type != 1)
            c.name += std::string(" (") + name + ")";
        v.push_back(c);
    };
    wobble(1, "cassette");
    v.push_back(timed(sweep(P::MediumDrive, "drone solo on cassette", cassette), 2.0, 1.0));
    v.push_back(timed(sweep(P::MediumMix, "drone solo on cassette", cassette), 2.0, 1.0));
    wobble(2, "vinyl");
    wobble(3, "sampler");
    for (int t : { 2, 3 })
        for (P p : { P::MediumAge, P::MediumNoise, P::MediumDrive })
        {
            auto c = timed(sweep(p, std::string("drone solo on ") + (t == 2 ? "vinyl" : "sampler"), [t](Rig& r) {
                               r.solo({ StripId::Drone });
                               r.snap(P::MediumType, static_cast<float>(t));
                           }), 2.0, 1.0);
            c.name += t == 2 ? " (vinyl)" : " (sampler)";
            v.push_back(c);
        }
    return v;
}

void addScene(Rig& r, float x, float y, std::initializer_list<std::pair<P, float>> values)
{
    Scene s;
    s.name = "audit";
    s.position = { x, y };
    for (const auto& [p, value] : values)
        s.values[idx(p)] = value;
    r.sceneManager().addScene(std::move(s));
}

std::vector<Check> terrainChecks()
{
    std::vector<Check> v;
    Setup twoScenesX = [](Rig& r) {
        r.solo({ StripId::Drone });
        addScene(r, 0.0f, 0.5f, { { P::DroneCutoff, 200.0f } });
        addScene(r, 1.0f, 0.5f, { { P::DroneCutoff, 8000.0f } });
        r.snap(P::TerrainGlide, 0.05f);
    };
    Setup twoScenesY = [](Rig& r) {
        r.solo({ StripId::Drone });
        addScene(r, 0.5f, 0.0f, { { P::DroneCutoff, 200.0f } });
        addScene(r, 0.5f, 1.0f, { { P::DroneCutoff, 8000.0f } });
        r.snap(P::TerrainGlide, 0.05f);
    };
    auto cutoff = [](Rig& r, const Features&) { return 12.0f * std::log2(r.last().paramTargets[idx(P::DroneCutoff)]); };
    {
        auto c = timed(sweep(P::TerrainX, "dark scene left, bright scene right", twoScenesX, Metric::Centroid, 1), 2.0, 1.0);
        c.measure = cutoff;
        c.minDelta = 6.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::TerrainY, "dark scene bottom, bright scene top", twoScenesY, Metric::Centroid, 1), 2.0, 1.0);
        c.measure = cutoff;
        c.minDelta = 6.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::TerrainFocus, "cursor a quarter of the way from the dark scene", then(twoScenesX, [](Rig& r) { r.snap(P::TerrainX, 0.25f); }),
                             Metric::Custom, 1), 1.0, 0.5);
        c.measure = [](Rig& r, const Features&) { return 20.0f * r.last().sceneWeights[0]; };
        c.minDelta = 2.0f;
        v.push_back(c);
    }
    auto drift = [](Rig& r, const Features&) {
        return 100.0f * r.meanOver(0.5, 1e9, [](const TelemetryFrame& f) { return std::hypot(f.position.x - f.cursor.x, f.position.y - f.cursor.y); });
    };
    {
        auto c = timed(sweep(P::TerrainWander, "two scenes, drift style", twoScenesX, Metric::Custom, 1), 3.0, 1.0);
        c.measure = drift;
        c.minDelta = 2.0f;
        c.checkHalves = false;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::TerrainWanderRate, "two scenes, full wander", then(twoScenesX, [](Rig& r) { r.snap(P::TerrainWander, 1.0f); }),
                             Metric::Custom, 1), 3.0, 0.5);
        c.measure = [](Rig& r, const Features&) {
            float path = 0.0f;
            for (std::size_t i = 1; i < r.frames.size(); ++i)
                path += std::hypot(r.frames[i].position.x - r.frames[i - 1].position.x, r.frames[i].position.y - r.frames[i - 1].position.y);
            return 100.0f * path;
        };
        c.minDelta = 5.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::TerrainWanderStyle, "three scenes and a drawn path, full fast wander", [](Rig& r) {
                           r.solo({ StripId::Drone });
                           addScene(r, 0.1f, 0.1f, { { P::DroneCutoff, 200.0f } });
                           addScene(r, 0.9f, 0.2f, { { P::DroneCutoff, 8000.0f } });
                           addScene(r, 0.5f, 0.9f, { { P::DroneCutoff, 1500.0f } });
                           auto path = std::make_unique<TerrainPath>(TerrainPath::build({ { 0.1f, 0.9f }, { 0.9f, 0.9f }, { 0.9f, 0.1f }, { 0.1f, 0.1f } }, 1));
                           r.engine.publishPath(std::move(path));
                           r.snap(P::TerrainWander, 1.0f);
                           r.snap(P::TerrainWanderRate, 0.5f);
                       }), 3.0, 0.5);
        c.measure = [](Rig& r, const Features&) {
            float sig = 0.0f;
            for (std::size_t i = 0; i < r.frames.size(); i += 7)
                sig += (r.frames[i].position.x * 3.0f + r.frames[i].position.y) * static_cast<float>(i % 13 + 1);
            return sig;
        };
        c.minChange = 1.0f;
        v.push_back(c);
    }
    {
        auto c = timed(sweep(P::TideRate, "resonator rain", resCtx(), Metric::Rms, 1), 3.0, 1.0);
        c.measure = [](Rig& r, const Features&) {
            CHECK(r.last().tide == Approx(r.last().paramTargets[idx(P::TideRate)]).epsilon(0.02));
            return 0.0f;
        };
        c.metric = Metric::Rms;
        c.minDelta = 10.0f;
        v.push_back(c);
    }
    return v;
}

std::vector<Check> cycleChecks()
{
    std::vector<Check> v;
    Setup base = [](Rig& r) {
        r.solo({ StripId::Bloom, StripId::Resonator });
        r.snap(P::ResRain, 0.0f);
        r.engine.loadBloomSample(shot());
        r.snap(P::BloomRoot, 60.0f);
        r.snap(P::BloomTransform, 5.0f);
        r.snap(P::BloomLength, 0.5f);
        r.snap(P::BloomRelease, 0.3f);
        r.snap(P::LoopsOn, 1.0f);
        r.snap(P::LoopsRate, 4.0f);
        r.snap(P::TideRate, 8.0f);
    };
    const std::string ctx = "cycles into bloom, pace 4x, tide 8x";
    auto notesPlayed = [](Rig& r, const Features&) {
        float n = 0.0f;
        for (std::size_t i = 1; i < r.frames.size(); ++i)
            for (std::size_t k = 0; k < 8; ++k)
                n += r.frames[i].loopFlash[k] > r.frames[i - 1].loopFlash[k] + 0.2f ? 1.0f : 0.0f;
        return n;
    };
    auto add = [&](P p, Metric m = Metric::Any, int dir = 0, Setup extra = {}) {
        v.push_back(timed(sweep(p, ctx, extra ? then(base, extra) : base, m, dir), 3.0, 0.3));
        return &v.back();
    };
    {
        auto* c = add(P::LoopsOn, Metric::Custom, 1);
        c->measure = notesPlayed;
        c->minDelta = 3.0f;
    }
    {
        auto* c = add(P::LoopsCount, Metric::Custom, 1);
        c->measure = notesPlayed;
        c->minDelta = 6.0f;
        c->minChange = 0.5f;
    }
    {
        auto* c = add(P::LoopsRate, Metric::Custom, 1);
        c->measure = notesPlayed;
        c->minDelta = 6.0f;
    }
    {
        auto* c = add(P::LoopsDensity, Metric::Custom, 1);
        c->measure = notesPlayed;
        c->minDelta = 6.0f;
    }
    {
        auto* c = add(P::LoopsRegister, Metric::Custom, 1);
        c->measure = [](Rig& r, const Features&) {
            float s = 0.0f;
            for (float n : r.last().loopNote)
                s += n;
            return s / 8.0f;
        };
        c->minDelta = 24.0f;
    }
    {
        auto* c = add(P::LoopsSpread, Metric::Custom, 1);
        c->measure = [](Rig& r, const Features&) {
            const auto& n = r.last().loopNote;
            return *std::max_element(n.begin(), n.end()) - *std::min_element(n.begin(), n.end());
        };
        c->minDelta = 12.0f;
    }
    add(P::LoopsVelocity, Metric::Rms, 1)->minDelta = 6.0f;
    {
        auto* c = add(P::LoopsTarget);
        c->measure = [](Rig& r, const Features&) {
            float bloom = 0.0f, res = 0.0f;
            for (const auto& f : r.frames)
            {
                bloom = std::max(bloom, f.stripPeakL[static_cast<std::size_t>(StripId::Bloom)]);
                res = std::max(res, f.stripPeakL[static_cast<std::size_t>(StripId::Resonator)]);
            }
            const float b = bloom > 1.0e-4f ? 10.0f : 0.0f;
            const float s = res > 1.0e-4f ? 20.0f : 0.0f;
            return b + s;
        };
    }
    {
        auto* c = add(P::SyncOn);
        c->context = "cycles into bloom at 90 BPM";
        c->measure = [](Rig& r, const Features&) { return r.last().syncOn ? 10.0f : 0.0f; };
    }
    {
        auto* c = add(P::SyncBpm, Metric::Custom, 1, [](Rig& r) { r.snap(P::SyncOn, 1.0f); });
        c->context = "tempo sync on";
        c->measure = [](Rig& r, const Features&) {
            float beats = 0.0f;
            for (std::size_t i = 1; i < r.frames.size(); ++i)
                beats += r.frames[i].beatPhase < r.frames[i - 1].beatPhase ? 1.0f : 0.0f;
            return beats;
        };
        c->minDelta = 6.0f;
    }
    return v;
}

std::vector<Check> patternChecks()
{
    auto c = timed(sweep(P::LoopsPattern, "cycles telemetry, everything muted", [](Rig& r) {
                       r.solo({});
                       r.snap(P::LoopsOn, 1.0f);
                   }), 0.05, 0.0);
    c.measure = [](Rig& r, const Features&) {
        float s = 0.0f;
        const auto& f = r.last();
        for (std::size_t k = 0; k < 8; ++k)
            s += f.loopPhase[k] * static_cast<float>(k + 1) * 10.0f + f.loopNote[k] * 0.01f;
        return s;
    };
    return { c };
}

enum class FxSource { Clicks, Tone, Tail, Wet, WetTail };

struct FxControlPlan
{
    int control;
    Metric metric;
    int direction;
    float minDelta;
    FxSource source = FxSource::Clicks;
};

constexpr int kClickSlot = static_cast<int>(StripId::Resonator) * 2;
constexpr int kToneSlot = static_cast<int>(StripId::Input) * 2;

Setup fxSource(int slot, const char* type)
{
    return [slot, type](Rig& r) {
        r.solo({ StripId::Resonator });
        r.snap(P::ResRain, 1.0f);
        r.snap(P::ResDecay, 0.3f);
        r.snap(P::ResModes, 16.0f);
        r.snap(P::ResRoot, 66.0f);
        r.snap(P::ResBrightness, 1.0f);
        r.snap(P::ResRainColour, 1.0f);
        r.snap(P::ResSpread, 0.0f);
        r.effect(slot, type);
        if (std::string(type) == "tf.delay")
            r.snap(slotParam(slot, 4), 0.0f);
    };
}

Setup toneSource(int slot, const char* type)
{
    return [slot, type](Rig& r) {
        r.solo({ StripId::Input });
        r.input = sineInput(2000.0);
        r.snap(P::InputArmed, 1.0f);
        r.snap(P::InputHighPass, 20.0f);
        r.effect(slot, type);
        const std::string t(type);
        if (t == "tf.delay" || t == "tf.wornEcho")
            r.snap(slotParam(slot, 1), 0.0f);
        if (t == "tf.wornEcho")
            r.snap(slotParam(slot, 3), 0.0f);
    };
}

void playClicksThenTail(Rig& r, double secs)
{
    r.run(1.0);
    r.snap(P::ResRain, 0.0f);
    r.run(secs - 1.0);
}

std::vector<Check> effectChecks(const char* type, std::vector<FxControlPlan> plans)
{
    const auto* info = tf::dsp::ProcessorFactory::instance().find(type);
    REQUIRE(info != nullptr);
    std::vector<Check> v;
    for (const auto& plan : plans)
    {
        const bool tone = plan.source == FxSource::Tone;
        const int slot = tone ? kToneSlot : kClickSlot;
        const P p = slotParam(slot, plan.control);
        const auto& control = info->controls[static_cast<std::size_t>(std::min(plan.control, 5))];
        auto c = timed(sweep(p, std::string(info->name) + (tone ? " on a 2 kHz tone without feedback, pitch jitter" : " on mono resonator clicks"),
                             tone ? toneSource(slot, type) : fxSource(slot, type), plan.metric, plan.direction), 2.5, tone ? 0.8 : 0.5);
        c.name = std::string(type) + " " + (plan.control == 6 ? "Mix" : control.name) + " (" + spec(p).id + ")";
        c.minDelta = plan.minDelta;
        if (tone)
            c.measure = [](Rig& r, const Features&) { return periodJitter(r, 0.8); };
        if (plan.source == FxSource::Tail || plan.source == FxSource::WetTail)
        {
            c.play = playClicksThenTail;
            c.seconds = 3.0;
            c.from = 1.3;
            c.context += ", clicks stop at 1 s";
        }
        if (plan.source == FxSource::Wet || plan.source == FxSource::WetTail)
        {
            const P mix = slotParam(slot, 6);
            c.reference = [mix](Rig& r) { r.snap(mix, 0.0f); };
            c.context += ", wet part only";
        }
        if (plan.control < 6 && control.display.curve == tf::dsp::DisplayMap::Curve::Choice)
        {
            c.values.clear();
            for (int k = 0; k < control.display.numChoices; ++k)
                c.values.push_back((static_cast<float>(k) + 0.5f) / static_cast<float>(control.display.numChoices));
        }
        if (plan.control < 6 && std::string(control.name) == "Hold")
        {
            c.apply = [p](Rig& r, float) { r.snap(p, 0.0f); };
            c.play = [p](Rig& r, double secs) {
                r.run(1.0);
                r.snap(P::ResRain, 0.0f);
                r.snap(p, r.swept);
                r.run(secs - 1.0);
            };
            c.from = 2.0;
            c.seconds = 3.5;
            c.context += ", hold pressed after 1 s and the clicks stop";
        }
        v.push_back(c);
    }
    return v;
}

std::vector<Check> reverbChecks()
{
    return effectChecks("tf.reverb", { { 0, Metric::Any, 0, 0 }, { 1, Metric::Rms, 1, 3.0f }, { 2, Metric::Centroid, 1, 3.0f }, { 3, Metric::Any, 0, 0 },
                                       { 4, Metric::Any, 0, 0 }, { 5, Metric::Rms, 1, 10.0f }, { 6, Metric::Any, 0, 0 } });
}

std::vector<Check> delayChecks()
{
    return effectChecks("tf.delay", { { 0, Metric::Any, 0, 0 }, { 1, Metric::Rms, 1, 2.0f }, { 2, Metric::Centroid, 1, 3.0f }, { 3, Metric::Side, 1, 3.0f },
                                      { 4, Metric::Custom, 1, 2.0f, FxSource::Tone }, { 5, Metric::Centroid, -1, 0.5f }, { 6, Metric::Any, 0, 0 } });
}

std::vector<Check> wornEchoChecks()
{
    return effectChecks("tf.wornEcho", { { 0, Metric::Any, 0, 0 }, { 1, Metric::Rms, 1, 3.0f }, { 2, Metric::Any, 0, 0 }, { 3, Metric::Any, 0, 0 },
                                         { 4, Metric::Custom, 1, 2.0f, FxSource::Tone }, { 5, Metric::Side, 1, 3.0f }, { 6, Metric::Any, 0, 0 } });
}

std::vector<Check> ensembleChecks()
{
    return effectChecks("tf.ensemble", { { 0, Metric::Any, 0, 0 }, { 1, Metric::Any, 0, 0 }, { 2, Metric::Any, 0, 0 }, { 3, Metric::Any, 0, 0 },
                                         { 4, Metric::Side, 1, 3.0f }, { 5, Metric::Centroid, 1, 1.5f }, { 6, Metric::Any, 0, 0 } });
}

std::vector<Check> blurChecks()
{
    return effectChecks("tf.blur", { { 0, Metric::Any, 0, 0 }, { 1, Metric::Any, 0, 0 }, { 2, Metric::Any, 0, 0 }, { 3, Metric::Centroid, 1, 1.0f },
                                     { 4, Metric::Centroid, 1, 3.0f }, { 5, Metric::Any, 0, 0 }, { 6, Metric::Any, 0, 0 } });
}

std::vector<Check> stringsChecks()
{
    return effectChecks("tf.strings", { { 0, Metric::Rms, 1, 6.0f, FxSource::Wet }, { 1, Metric::Rms, 1, 3.0f, FxSource::WetTail },
                                        { 2, Metric::Centroid, 1, 1.0f, FxSource::Wet }, { 3, Metric::Any, 0, 0, FxSource::Wet },
                                        { 4, Metric::Any, 0, 0, FxSource::Wet }, { 5, Metric::Rms, 1, 6.0f, FxSource::Wet }, { 6, Metric::Rms, 1, 6.0f, FxSource::Wet } });
}

std::vector<Check> mediumFxChecks()
{
    auto v = effectChecks("tf.medium", { { 0, Metric::Any, 0, 0 }, { 6, Metric::Any, 0, 0 } });
    for (int control = 1; control <= 4; ++control)
    {
        const bool wobble = control == 3;
        auto more = effectChecks("tf.medium", { { control, control == 1 ? Metric::Centroid : (wobble ? Metric::Custom : Metric::Any), control == 1 || wobble ? (wobble ? 1 : -1) : 0,
                                                  wobble ? 2.0f : 0.25f, wobble ? FxSource::Tone : FxSource::Clicks } });
        for (auto& c : more)
        {
            const P typeParam = slotParam(wobble ? kToneSlot : kClickSlot, 0);
            c.setup = then(c.setup, [typeParam](Rig& r) { r.snap(typeParam, 0.375f); });
            c.context += ", type Cassette";
            v.push_back(c);
        }
    }
    return v;
}

Setup chainSource(int slot)
{
    if (slot >= kMasterSlot)
        return droneCtx();
    if (slot >= kBusBSlot)
        return [](Rig& r) {
            r.solo({ StripId::Drone });
            r.snap(P::DroneSendB, 0.0f);
        };
    if (slot >= kBusASlot)
        return [](Rig& r) {
            r.solo({ StripId::Drone });
            r.snap(P::DroneSendA, 0.0f);
        };
    switch (static_cast<StripId>(slot / 2))
    {
        case StripId::Drone: return droneCtx();
        case StripId::Cloud1: return cloudCtx(0);
        case StripId::Cloud2: return cloudCtx(1);
        case StripId::Cloud3: return cloudCtx(2);
        case StripId::Cloud4: return cloudCtx(3);
        case StripId::Resonator: return resCtx();
        case StripId::Input: return inputCtx();
        case StripId::Bloom: return bloomCtx();
        case StripId::Loop:
            return [](Rig& r) {
                r.solo({ StripId::Loop });
                r.input = testInput();
            };
        case StripId::Weather: return weatherCtx();
        case StripId::Freeze: return [](Rig& r) { r.solo({ StripId::Freeze, StripId::Drone }); r.snap(P::DroneLevel, -40.0f); };
        case StripId::Count: break;
    }
    return {};
}

Play chainPlay(int slot)
{
    if (slot >= kBusASlot)
        return {};
    switch (static_cast<StripId>(slot / 2))
    {
        case StripId::Bloom: return playBloomNotes;
        case StripId::Loop: return playLoop;
        case StripId::Freeze: return playFreeze;
        default: return {};
    }
}

std::vector<Check> routingChecks(int slot)
{
    const std::string where = std::string(kFxSlots[static_cast<std::size_t>(slot)].name);
    const auto source = chainSource(slot);
    const auto play = chainPlay(slot);
    const bool late = slot < kBusASlot && (slot / 2 == static_cast<int>(StripId::Loop) || slot / 2 == static_cast<int>(StripId::Freeze));
    std::vector<Check> v;
    auto add = [&](int control, const char* type, std::vector<float> values, Setup extra) {
        const P p = slotParam(slot, control);
        auto c = timed(sweep(p, where + " running " + type, then(then(source, [slot, type](Rig& r) { r.effect(slot, type); }), std::move(extra))),
                       late ? 2.6 : 1.6, late ? 1.6 : 0.6);
        c.play = play;
        c.values = std::move(values);
        c.minChange = 0.25f;
        v.push_back(c);
    };
    const P typeParam = slotParam(slot, 0);
    add(0, "tf.medium", { 0.1f, 0.9f }, {});
    for (int control = 1; control <= 2; ++control)
        add(control, "tf.medium", { 0.0f, 1.0f }, [typeParam](Rig& r) { r.snap(typeParam, 0.375f); });
    add(3, "tf.delay", { 0.0f, 1.0f }, {});
    add(4, "tf.medium", { 0.0f, 1.0f }, [typeParam](Rig& r) { r.snap(typeParam, 0.375f); });
    add(5, "tf.delay", { 0.0f, 1.0f }, {});
    add(6, "tf.medium", { 0.0f, 1.0f }, [typeParam, slot](Rig& r) {
        r.snap(typeParam, 0.9f);
        r.snap(slotParam(slot, 1), 1.0f);
    });
    return v;
}

std::vector<std::vector<Check>> allGroups()
{
    std::vector<std::vector<Check>> g { masterChecks(), droneChecks(), resonatorChecks(), inputChecks(), bloomChecks(), bloomRootChecks(), looperChecks(),
                                        weatherChecks(), freezeChecks(), gestureChecks(), harmonyChecks(), mediumChecks(), terrainChecks(), cycleChecks(),
                                        patternChecks(), reverbChecks(), delayChecks(), wornEchoChecks(), ensembleChecks(), blurChecks(), stringsChecks(),
                                        mediumFxChecks() };
    for (int k = 0; k < kNumClouds; ++k)
        g.push_back(cloudChecks(k));
    for (int s = 0; s < kNumStrips; ++s)
        g.push_back(stripGroup(static_cast<StripId>(s)));
    for (int slot = 0; slot < kNumFxSlots; ++slot)
        g.push_back(routingChecks(slot));
    return g;
}

double firstNotice(const Rig& r, EngineNotice::Type type, std::uint64_t after = 0)
{
    for (const auto& n : r.notices)
        if (n.type == type && n.sampleTime >= after)
            return static_cast<double>(n.sampleTime - after) / kFs;
    return -1.0;
}
}

TEST_CASE("Control audit: master", "[audit]") { runChecks(masterChecks()); }
TEST_CASE("Control audit: drone", "[audit]") { runChecks(droneChecks()); }
TEMPLATE_TEST_CASE_SIG("Control audit: cloud", "[audit]", ((int K), K), 0, 1, 2, 3) { runChecks(cloudChecks(K)); }
TEST_CASE("Control audit: resonator", "[audit]") { runChecks(resonatorChecks()); }
TEST_CASE("Control audit: input", "[audit]") { runChecks(inputChecks()); }
TEST_CASE("Control audit: bloom", "[audit]") { runChecks(bloomChecks()); }
TEST_CASE("Control audit: bloom sample root", "[audit]") { runChecks(bloomRootChecks()); }
TEST_CASE("Control audit: looper", "[audit]") { runChecks(looperChecks()); }
TEST_CASE("Control audit: weather", "[audit]") { runChecks(weatherChecks()); }
TEST_CASE("Control audit: freeze all", "[audit]") { runChecks(freezeChecks()); }
TEST_CASE("Control audit: gestures", "[audit]") { runChecks(gestureChecks()); }
TEST_CASE("Control audit: harmony", "[audit]") { runChecks(harmonyChecks()); }
TEST_CASE("Control audit: medium", "[audit]") { runChecks(mediumChecks()); }
TEST_CASE("Control audit: terrain and tide", "[audit]") { runChecks(terrainChecks()); }
TEST_CASE("Control audit: cycles and tempo", "[audit]") { runChecks(cycleChecks()); }
TEST_CASE("Control audit: cycle patterns", "[audit]") { runChecks(patternChecks()); }
TEST_CASE("Control audit: effect tf.reverb", "[audit]") { runChecks(reverbChecks()); }
TEST_CASE("Control audit: effect tf.delay", "[audit]") { runChecks(delayChecks()); }
TEST_CASE("Control audit: effect tf.wornEcho", "[audit]") { runChecks(wornEchoChecks()); }
TEST_CASE("Control audit: effect tf.ensemble", "[audit]") { runChecks(ensembleChecks()); }
TEST_CASE("Control audit: effect tf.blur", "[audit]") { runChecks(blurChecks()); }
TEST_CASE("Control audit: effect tf.strings", "[audit]") { runChecks(stringsChecks()); }
TEST_CASE("Control audit: effect tf.medium", "[audit]") { runChecks(mediumFxChecks()); }

TEMPLATE_TEST_CASE_SIG("Control audit: strip", "[audit]", ((int S), S), 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10)
{
    STATIC_REQUIRE(kNumStrips == 11);
    runChecks(stripGroup(static_cast<StripId>(S)));
}

TEMPLATE_TEST_CASE_SIG("Control audit: effect slot routing", "[audit]", ((int Slot), Slot), 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                       18, 19, 20, 21, 22, 23, 24, 25, 26, 27)
{
    STATIC_REQUIRE(kNumFxSlots == 28);
    runChecks(routingChecks(Slot));
}

TEST_CASE("Control audit: fade length times the fades", "[audit]")
{
    for (float seconds : { 0.5f, 8.0f, 120.0f })
    {
        Rig r;
        r.solo({});
        r.snap(P::MasterFadeSecs, 0.5f);
        r.engine.command(Command::FadeOut);
        r.run(0.6);
        r.snap(P::MasterFadeSecs, seconds);
        r.run(0.01);
        r.notices.clear();
        const auto start = r.engine.getSampleTime();
        r.engine.command(Command::FadeIn);
        r.run(seconds + 0.5);
        const double in = firstNotice(r, EngineNotice::Type::FadeInComplete, start);
        const auto outStart = r.engine.getSampleTime();
        r.engine.command(Command::FadeOut);
        r.run(seconds + 0.5);
        const double out = firstNotice(r, EngineNotice::Type::FadeOutComplete, outStart);
        INFO("fade length " << seconds << ": in " << in << " s, out " << out << " s");
        CHECK(in == Approx(seconds).margin(0.02));
        CHECK(out == Approx(seconds).margin(0.02));
        report("master.fadeSeconds", "fade in and out, silent engine", "ok | set " + fmt(seconds) + " s: in " + fmt(static_cast<float>(in)) + " s, out "
                                                                            + fmt(static_cast<float>(out)) + " s");
    }
}

TEST_CASE("Control audit: key morph times a key change", "[audit]")
{
    for (float seconds : { 0.5f, 8.0f, 60.0f })
    {
        Rig r;
        r.solo({});
        r.snap(P::HarmonyMorph, seconds);
        r.run(0.2);
        r.frames.clear();
        const auto start = r.engine.getSampleTime();
        r.set(P::HarmonyRoot, 7.0f);
        r.run(seconds * 1.1 + 0.3);
        double done = -1.0;
        float halfway = -1.0f;
        for (const auto& f : r.frames)
        {
            const double t = static_cast<double>(f.sampleTime - start) / kFs;
            if (halfway < 0.0f && f.harmonyMorph >= 0.5f)
                halfway = static_cast<float>(t);
            if (done < 0.0 && f.harmonyMorph >= 1.0f && f.harmonyRoot == 7)
                done = t;
        }
        INFO("key morph " << seconds << ": halfway " << halfway << " s, done " << done << " s");
        CHECK(done == Approx(seconds).epsilon(0.05).margin(0.05));
        CHECK(halfway == Approx(seconds * 0.5f).epsilon(0.05).margin(0.05));
        report("harmony.morph", "key change D to G, silent engine", "ok | set " + fmt(seconds) + " s: migrated in " + fmt(static_cast<float>(done)) + " s");
    }
}

TEST_CASE("Control audit: glide times the cursor", "[audit]")
{
    std::vector<double> times;
    for (float glide : { 0.05f, 1.5f, 30.0f })
    {
        Rig r;
        r.solo({});
        r.snap(P::TerrainGlide, glide);
        r.snap(P::TerrainX, 0.0f);
        r.run(0.2);
        const float c0 = r.last().cursor.x;
        r.frames.clear();
        const auto start = r.engine.getSampleTime();
        r.set(P::TerrainX, 1.0f);
        r.run(glide * 3.0 + 0.3);
        double t63 = -1.0;
        for (const auto& f : r.frames)
            if (t63 < 0.0 && f.cursor.x >= c0 + 0.632f * (1.0f - c0))
                t63 = static_cast<double>(f.sampleTime - start) / kFs;
        INFO("glide " << glide << ": 63% reached after " << t63 << " s");
        CHECK(t63 > 0.0);
        CHECK(t63 == Approx(glide).epsilon(0.15).margin(0.05));
        times.push_back(t63);
        report("terrain.glide", "cursor jump 0 to 1, silent engine", "ok | set " + fmt(glide) + " s: 63% after " + fmt(static_cast<float>(t63)) + " s");
    }
    CHECK(times[2] > times[1]);
    CHECK(times[1] > times[0]);
}

TEST_CASE("Control audit: swell rise and ebb time the swell", "[audit]")
{
    auto riseTime = [](float attack) {
        Rig r;
        r.solo({});
        r.snap(P::SwellAttack, attack);
        r.snap(P::SwellHold, 1.0f);
        r.run(attack * 1.6 + 0.3);
        for (const auto& f : r.frames)
            if (f.swell >= 0.95f)
                return r.time(f);
        return -1.0;
    };
    auto ebbTime = [](float release) {
        Rig r;
        r.solo({});
        r.snap(P::SwellAttack, 0.2f);
        r.snap(P::SwellRelease, release);
        r.snap(P::SwellHold, 1.0f);
        r.run(0.6);
        r.frames.clear();
        const auto start = r.engine.getSampleTime();
        r.snap(P::SwellHold, 0.0f);
        r.run(release * 1.6 + 0.3);
        for (const auto& f : r.frames)
            if (f.swell <= 0.05f)
                return static_cast<double>(f.sampleTime - start) / kFs;
        return -1.0;
    };
    for (float a : { 0.2f, 3.0f, 20.0f })
    {
        const double t = riseTime(a);
        INFO("swell rise " << a << ": 95% after " << t << " s");
        CHECK(t == Approx(a).epsilon(0.1).margin(0.05));
        report("swell.attack", "swell held, silent engine", "ok | set " + fmt(a) + " s: 95% after " + fmt(static_cast<float>(t)) + " s");
    }
    for (float rel : { 0.5f, 10.0f, 60.0f })
    {
        const double t = ebbTime(rel);
        INFO("swell ebb " << rel << ": down to 5% after " << t << " s");
        CHECK(t == Approx(rel).epsilon(0.1).margin(0.05));
        report("swell.release", "swell let go, silent engine", "ok | set " + fmt(rel) + " s: 5% after " + fmt(static_cast<float>(t)) + " s");
    }
}

TEST_CASE("Control audit: catch length and source and target", "[audit]")
{
    Rig r;
    r.solo({ StripId::Drone });
    r.input = testInput();
    r.run(31.0);

    for (float seconds : { 5.0f, 20.0f, 30.0f })
    {
        r.notices.clear();
        r.snap(P::CatchSeconds, seconds);
        r.snap(P::CatchSource, 0.0f);
        r.run(0.01);
        r.engine.command(Command::Catch);
        r.run(0.01);
        REQUIRE(r.notices.size() >= 1);
        const auto n = r.notices.back();
        REQUIRE(n.type == EngineNotice::Type::CatchReady);
        tf::dsp::SampleBuffer caught;
        REQUIRE(r.engine.copyCatch(n, caught));
        CHECK(caught.seconds() == Approx(seconds).margin(0.01));
        CHECK(caught.isStereo());
        report("catch.seconds", "drone 31 s, then Catch", "ok | set " + fmt(seconds) + " s: caught " + fmt(static_cast<float>(caught.seconds())) + " s");
    }

    auto catchFrom = [&](float source) {
        r.notices.clear();
        r.snap(P::CatchSeconds, 5.0f);
        r.snap(P::CatchSource, source);
        r.run(0.01);
        r.engine.command(Command::Catch);
        r.run(0.01);
        REQUIRE(! r.notices.empty());
        tf::dsp::SampleBuffer b;
        REQUIRE(r.engine.copyCatch(r.notices.back(), b));
        CHECK(static_cast<float>(r.notices.back().source) == source);
        return b;
    };
    const auto output = catchFrom(0.0f);
    const auto input = catchFrom(1.0f);
    CHECK(output.isStereo());
    CHECK_FALSE(input.isStereo());
    Audio a { output.left, output.right }, b { input.left, input.left };
    const auto fo = analyse(a, 0, a.size()), fi = analyse(b, 0, b.size());
    INFO("output centroid " << fo.centroid << ", input centroid " << fi.centroid);
    CHECK(distance(fo, fi) >= kMeasurable);
    report("catch.source", "drone out, test tone in", "ok | output " + fmt(fo.centroid) + " Hz, input " + fmt(fi.centroid) + " Hz");

    for (int target = 0; target <= kNumClouds; ++target)
    {
        Rig t;
        t.solo({ StripId::Drone });
        t.run(1.0);
        CatchManager catcher(t.engine);
        int landed = -1;
        catcher.onCaught = [&](int cloud, const std::string&) { landed = cloud; };
        t.snap(P::CatchTarget, static_cast<float>(target));
        t.snap(P::CatchSeconds, 5.0f);
        t.run(0.01);
        t.notices.clear();
        t.engine.command(Command::Catch);
        t.run(0.01);
        REQUIRE(! t.notices.empty());
        CHECK(t.notices.back().target == target);
        REQUIRE(catcher.handle(t.notices.back()));
        CHECK(landed == (target == 0 ? 0 : target - 1));
        CHECK(t.engine.getCloudSample(landed) != nullptr);
        report("catch.target", "drone 1 s, then Catch", "ok | " + std::to_string(target) + " -> cloud " + std::to_string(landed + 1));
    }
}

TEST_CASE("Control audit: bloom tone follows a held note", "[audit]")
{
    Rig r;
    bloomCtx()(r);
    r.snap(P::BloomTransform, 2.0f);
    r.snap(P::BloomLength, 30.0f);
    r.snap(P::BloomTone, 0.0f);
    r.run(0.02);
    r.note(60, 0.9f);
    r.run(1.5);
    const auto before = analyse(r.out, r.at(1.0), r.at(1.5));
    r.snap(P::BloomTone, 1.0f);
    r.run(1.0);
    const auto after = analyse(r.out, r.at(2.0), r.at(2.5));
    INFO("held note centroid before " << before.centroid << " Hz, after " << after.centroid << " Hz");
    CHECK(after.centroid > before.centroid * 1.25f);
    report("bloom.tone", "held Freeze note, tone moved while it sounds",
           std::string(after.centroid > before.centroid * 1.25f ? "ok" : "FAIL") + " | " + fmt(before.centroid) + " Hz -> " + fmt(after.centroid) + " Hz");
}

TEST_CASE("Control audit: levels at their floor are silent", "[audit]")
{
    for (P p : { P::MasterLevel, P::BusALevel, P::BusBLevel })
    {
        auto build = [p](Rig& r, float send) {
            r.solo({ StripId::Drone });
            r.effect(kBusASlot, "tf.reverb");
            r.effect(kBusBSlot, "tf.delay");
            r.snap(P::BusALevel, -60.0f);
            r.snap(P::BusBLevel, -60.0f);
            r.snap(p == P::BusALevel ? P::DroneSendA : P::DroneSendB, send);
            r.snap(p == P::BusALevel ? P::DroneSendB : P::DroneSendA, -60.0f);
            r.snap(p, -60.0f);
            r.run(2.1);
        };
        Rig r;
        build(r, 0.0f);
        if (p != P::MasterLevel)
        {
            Rig dry;
            build(dry, -60.0f);
            float worst = 0.0f;
            for (std::size_t i = 0; i < dry.out.size(); ++i)
                worst = std::max(worst, std::fabs(dry.out.l[i] - r.out.l[i]));
            INFO(spec(p).id << " at -60 dB still adds " << worst);
            CHECK(worst == 0.0f);
            report(spec(p).id, "level at its floor (shown as Off)", worst == 0.0f ? "ok | silent" : "FAIL | leaks " + fmt(worst));
        }
        else
        {
            const auto f = analyse(r.out, r.at(1.0), r.out.size());
            INFO("master at -60 dB: " << f.rmsDb << " dB");
            CHECK(f.peak == 0.0f);
            report(spec(p).id, "level at its floor (shown as Off)", f.peak == 0.0f ? "ok | silent" : "FAIL | " + fmt(f.rmsDb) + " dB");
        }
    }
}

TEST_CASE("Control audit: every parameter is covered", "[audit]")
{
    std::set<ParamIndex> seen;
    for (const auto& g : allGroups())
        for (P p : covered(g))
            seen.insert(idx(p));
    for (P p : { P::MasterFadeSecs, P::HarmonyMorph, P::TerrainGlide, P::SwellAttack, P::SwellRelease, P::CatchSeconds, P::CatchSource, P::CatchTarget,
                 P::ModFollowAttack, P::ModFollowRelease, P::ModFollowGain, P::SyncSource, P::SpaceMode, P::SpaceSpread, P::SpaceRotate, P::DroneChord, P::DroneGlide, P::DroneRevoice })
        seen.insert(idx(p));
    for (const auto& s : kStrips)
        seen.insert(idx(s.azimuth));
    for (ParamIndex i = idx(P::ModLfo1Rate); i < idx(P::ModFollowAttack); ++i)
        seen.insert(i);
    for (int k = 0; k < kMaxModRoutes; ++k)
        seen.insert(static_cast<ParamIndex>(idx(P::ModRoute1Depth) + k));
    std::string missing;
    for (ParamIndex i = 0; i < kNumParams; ++i)
        if (seen.count(i) == 0)
            missing += registry().spec(i).id + " ";
    INFO("not audited: " << missing);
    CHECK(missing.empty());
}

TEST_CASE("Control audit: drone shape keeps its level through the middle", "[audit]")
{
    std::vector<float> levels;
    for (float shape : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Rig r;
        r.solo({ StripId::Drone });
        r.snap(P::DroneShape, shape);
        r.snap(P::DroneDriftDepth, 0.0f);
        r.snap(P::DroneEvolve, 0.0f);
        r.run(3.0);
        levels.push_back(analyse(r.out, r.at(2.0), r.out.size()).rmsDb);
    }
    const float lo = std::min(levels.front(), levels.back());
    std::string detail;
    for (std::size_t k = 1; k + 1 < levels.size(); ++k)
    {
        INFO("shape " << 0.25 * static_cast<double>(k) << ": " << levels[k] << " dB, ends " << levels.front() << " / " << levels.back() << " dB");
        CHECK(levels[k] >= lo - 1.0f);
        detail += " " + fmt(levels[k]) + "dB";
    }
    report("drone.shape", "level across the crossfade", "ok | ends " + fmt(levels.front()) + "/" + fmt(levels.back()) + " dB, inside" + detail);
}

TEST_CASE("Control audit: delays reach their full spread time", "[audit]")
{
    auto echoAt = [](tf::dsp::Processor& fx, const std::array<float, 6>& controls) {
        fx.prepare({ kFs, kBlock });
        fx.setControls(controls, {});
        const int total = static_cast<int>(3.3 * kFs);
        std::vector<float> l(static_cast<std::size_t>(total), 0.0f), r(l.size(), 0.0f);
        l[0] = r[0] = 1.0f;
        for (int pos = 0; pos < total; pos += kBlock)
            fx.process(l.data() + pos, r.data() + pos, std::min(kBlock, total - pos));
        auto peakAt = [](const std::vector<float>& x) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < x.size(); ++i)
                if (std::fabs(x[i]) > std::fabs(x[best]))
                    best = i;
            return static_cast<double>(best) / kFs;
        };
        return std::make_pair(peakAt(l), peakAt(r));
    };
    tf::dsp::TapeDelay tape;
    const auto [tl, tr] = echoAt(tape, { 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f });
    INFO("tape delay echoes at " << tl << " s and " << tr << " s");
    CHECK(tl == Approx(2.0).margin(0.01));
    CHECK(tr == Approx(3.0).margin(0.01));
    tf::dsp::WornEcho worn;
    const auto [wl, wr] = echoAt(worn, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f });
    INFO("worn echo echoes at " << wl << " s and " << wr << " s");
    CHECK(wl == Approx(2.0).margin(0.01));
    CHECK(wr == Approx(3.0).margin(0.01));
    report("tf.delay / tf.wornEcho Time + Spread", "impulse, time 2 s, spread 100%",
           "ok | tape " + fmt(static_cast<float>(tl)) + "/" + fmt(static_cast<float>(tr)) + " s, worn " + fmt(static_cast<float>(wl)) + "/"
               + fmt(static_cast<float>(wr)) + " s");
}

namespace {
std::vector<float> sourceTrace(const Rig& r, ModSource s, double from)
{
    std::vector<float> v;
    for (const auto& f : r.frames)
        if (r.time(f) >= from)
            v.push_back(f.modValue[static_cast<std::size_t>(s)]);
    return v;
}

int signChanges(const std::vector<float>& v)
{
    int n = 0;
    for (std::size_t i = 1; i < v.size(); ++i)
        n += (v[i] >= 0.0f) != (v[i - 1] >= 0.0f) ? 1 : 0;
    return n;
}

float meanAbs(const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v)
        s += std::abs(x);
    return v.empty() ? 0.0f : static_cast<float>(s / static_cast<double>(v.size()));
}

float largestStep(const std::vector<float>& v)
{
    float m = 0.0f;
    for (std::size_t i = 1; i < v.size(); ++i)
        m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

float timeToCross(const Rig& r, ModSource s, double from, float level, bool rising)
{
    for (const auto& f : r.frames)
        if (r.time(f) >= from && (rising ? f.modValue[static_cast<std::size_t>(s)] >= level : f.modValue[static_cast<std::size_t>(s)] <= level))
            return static_cast<float>(r.time(f) - from);
    return 1.0e9f;
}

InputFn sineInput(float amplitude, double stopAt = 1.0e9)
{
    return [amplitude, stopAt](std::uint64_t t0, int n, float* l, float* r) {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(t0 + static_cast<std::uint64_t>(i)) / kFs;
            const float v = t < stopAt ? amplitude * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 330.0 * t)) : 0.0f;
            l[i] = r[i] = v;
        }
    };
}
}

TEST_CASE("Control audit: LFO rate sets the speed and every shape draws its own wave", "[audit][mod]")
{
    for (int k = 0; k < kNumLfos; ++k)
    {
        const auto rate = static_cast<P>(idx(P::ModLfo1Rate) + 2 * k);
        const auto shape = static_cast<P>(idx(P::ModLfo1Rate) + 2 * k + 1);
        const auto source = static_cast<ModSource>(k);
        int slow = 0, fast = 0;
        for (float hz : { 0.5f, 4.0f })
        {
            Rig r;
            r.snap(rate, hz);
            r.run(4.0);
            (hz < 1.0f ? slow : fast) = signChanges(sourceTrace(r, source, 0.5));
        }
        INFO("LFO " << k + 1 << " sign changes at 0.5 Hz " << slow << ", at 4 Hz " << fast);
        CHECK(slow >= 2);
        CHECK(fast > slow * 5);
        report(spec(rate).id, "zero crossings 0.5 vs 4 Hz", "ok | " + std::to_string(slow) + " vs " + std::to_string(fast));

        for (int s = 0; s < 5; ++s)
        {
            Rig r;
            r.snap(rate, 1.5f);
            r.snap(shape, static_cast<float>(s));
            r.run(3.0);
            const auto v = sourceTrace(r, source, 0.5);
            REQUIRE(v.size() > 50);
            int rising = 0, flat = 0, full = 0;
            for (std::size_t i = 1; i < v.size(); ++i)
            {
                rising += v[i] > v[i - 1] ? 1 : 0;
                flat += v[i] == v[i - 1] ? 1 : 0;
                full += std::abs(v[i]) > 0.99f ? 1 : 0;
            }
            const float n = static_cast<float>(v.size() - 1);
            INFO("LFO " << k + 1 << " shape " << s << " mean|v| " << meanAbs(v) << " rising " << rising / n << " flat " << flat / n << " full " << full / n);
            switch (s)
            {
                case 0: CHECK(meanAbs(v) == Approx(0.637f).margin(0.06f)); break;
                case 1: CHECK(meanAbs(v) == Approx(0.5f).margin(0.06f)); break;
                case 2: CHECK(rising / n > 0.9f); break;
                case 3: CHECK(full / n > 0.98f); break;
                default: CHECK(flat / n > 0.85f); break;
            }
        }
        report(spec(shape).id, "all five shapes", "ok");
    }
}

TEST_CASE("Control audit: random sources change faster with Rate and jump less with Smooth", "[audit][mod]")
{
    for (int k = 0; k < kNumRandoms; ++k)
    {
        const auto rate = static_cast<P>(idx(P::ModRandom1Rate) + 2 * k);
        const auto smooth = static_cast<P>(idx(P::ModRandom1Rate) + 2 * k + 1);
        const auto source = static_cast<ModSource>(static_cast<int>(ModSource::Random1) + k);
        float motion[2] {};
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.snap(rate, i == 0 ? 0.2f : 6.0f);
            r.snap(smooth, 0.0f);
            r.run(5.0);
            const auto v = sourceTrace(r, source, 0.5);
            double travel = 0.0;
            for (std::size_t j = 1; j < v.size(); ++j)
                travel += std::abs(v[j] - v[j - 1]);
            motion[i] = static_cast<float>(travel);
        }
        INFO("travel at 0.2 Hz " << motion[0] << ", at 6 Hz " << motion[1]);
        CHECK(motion[1] > motion[0] * 4.0f);
        report(spec(rate).id, "travel 0.2 vs 6 Hz", "ok | " + fmt(motion[0]) + " vs " + fmt(motion[1]));

        float steps[2] {};
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.snap(rate, 1.0f);
            r.snap(smooth, i == 0 ? 0.0f : 1.0f);
            r.run(6.0);
            steps[i] = largestStep(sourceTrace(r, source, 0.5));
        }
        INFO("largest step at Smooth 0 " << steps[0] << ", at Smooth 1 " << steps[1]);
        CHECK(steps[0] > 0.2f);
        CHECK(steps[1] < steps[0] * 0.25f);
        report(spec(smooth).id, "largest step 0 vs 1", "ok | " + fmt(steps[0]) + " vs " + fmt(steps[1]));
    }
}

TEST_CASE("Control audit: follower attack, release and sensitivity", "[audit][mod]")
{
    float rise[2] {}, fall[2] {}, held[2] {};
    for (int i = 0; i < 2; ++i)
    {
        Rig r;
        r.snap(P::ModFollowAttack, i == 0 ? 0.002f : 0.8f);
        r.input = sineInput(0.3f);
        r.run(3.0);
        rise[i] = timeToCross(r, ModSource::InputLevel, 0.0, 0.5f, true);
    }
    INFO("rise to half at attack 2 ms " << rise[0] << " s, at 800 ms " << rise[1] << " s");
    CHECK(rise[0] < 0.1f);
    CHECK(rise[1] > rise[0] + 0.3f);
    report("mod.follow.attack", "time to rise", "ok | " + fmt(rise[0]) + " vs " + fmt(rise[1]) + " s");

    for (int i = 0; i < 2; ++i)
    {
        Rig r;
        r.snap(P::ModFollowRelease, i == 0 ? 0.02f : 4.0f);
        r.input = sineInput(0.3f, 1.0);
        r.run(8.0);
        fall[i] = timeToCross(r, ModSource::InputLevel, 1.0, 0.1f, false);
    }
    INFO("fall at release 20 ms " << fall[0] << " s, at 4 s " << fall[1] << " s");
    CHECK(fall[0] < 0.3f);
    CHECK(fall[1] > fall[0] + 1.0f);
    report("mod.follow.release", "time to fall", "ok | " + fmt(fall[0]) + " vs " + fmt(fall[1]) + " s");

    for (int i = 0; i < 2; ++i)
    {
        Rig r;
        r.snap(P::ModFollowGain, i == 0 ? -24.0f : 24.0f);
        r.input = sineInput(0.01f);
        r.run(2.0);
        held[i] = r.frames.back().modValue[static_cast<std::size_t>(ModSource::InputLevel)];
    }
    INFO("level of a quiet input at -24 dB " << held[0] << ", at +24 dB " << held[1]);
    CHECK(held[1] > held[0] + 0.5f);
    report("mod.follow.gain", "quiet input", "ok | " + fmt(held[0]) + " vs " + fmt(held[1]));
}

TEST_CASE("Control audit: each route's Depth scales and inverts its swing", "[audit][mod]")
{
    for (int k = 0; k < kMaxModRoutes; ++k)
    {
        const auto depth = static_cast<P>(idx(P::ModRoute1Depth) + k);
        float swing[3] {};
        const float depths[3] { 0.0f, 0.5f, -0.25f };
        for (int i = 0; i < 3; ++i)
        {
            Rig r;
            ModRouteManager routes(r.engine);
            routes.replaceAll({ { ModSource::Lfo1, idx(P::DroneCutoff), k } });
            r.snap(P::ModLfo1Rate, 2.0f);
            r.snap(P::ModLfo1Shape, 3.0f);
            r.snap(depth, depths[i]);
            r.run(1.5);
            float hi = -1.0f;
            float signedAtTop = 0.0f;
            for (const auto& f : r.frames)
                if (r.time(f) > 0.5)
                {
                    const float m = f.paramMod[idx(P::DroneCutoff)];
                    if (std::abs(m) > hi)
                    {
                        hi = std::abs(m);
                        signedAtTop = f.modValue[0] > 0.0f ? m : -m;
                    }
                }
            swing[i] = signedAtTop;
        }
        INFO("route " << k + 1 << " swing at depth 0 / 0.5 / -0.25: " << swing[0] << " / " << swing[1] << " / " << swing[2]);
        CHECK(std::abs(swing[0]) < 1.0e-6f);
        CHECK(swing[1] == Approx(0.5f).margin(0.02f));
        CHECK(swing[2] == Approx(-0.25f).margin(0.02f));
        report(spec(depth).id, "swing at 0 / 0.5 / -0.25", "ok");
    }
}

TEST_CASE("Control audit: Follow decides whether MIDI clock sets the tempo", "[audit][sync]")
{
    float bpm[2] {};
    for (int i = 0; i < 2; ++i)
    {
        Rig r;
        r.snap(P::SyncOn, 1.0f);
        r.snap(P::SyncSource, static_cast<float>(i));
        r.run(0.05);
        const double tick = 60.0 / (150.0 * 24.0);
        double next = 10.0;
        RawMidi start;
        start.status = 0xfa;
        start.time = next;
        r.engine.postMidi(0, start);
        for (int b = 0; b < 300; ++b)
        {
            const double until = 10.0 + static_cast<double>(b + 1) * kBlock / kFs;
            while (next < until)
            {
                RawMidi clock;
                clock.status = 0xf8;
                clock.time = next;
                r.engine.postMidi(0, clock);
                next += tick;
            }
            r.run(static_cast<double>(kBlock) / kFs);
        }
        bpm[i] = r.frames.back().bpm;
    }
    INFO("tempo with Follow Internal " << bpm[0] << ", with Follow MIDI clock " << bpm[1]);
    CHECK(bpm[0] == Approx(spec(P::SyncBpm).defaultValue).margin(0.01f));
    CHECK(bpm[1] == Approx(150.0f).margin(1.0f));
    report("sync.source", "150 BPM clock", "ok | " + fmt(bpm[0]) + " vs " + fmt(bpm[1]) + " BPM");
}

TEST_CASE("Control audit: drone chord revoices into its notes", "[audit]")
{
    for (int chord = 0; chord < tf::dsp::DroneGenerator::kNumChords; ++chord)
    {
        Rig r;
        r.solo({ StripId::Drone });
        r.snap(P::DroneDensity, 6.0f);
        r.snap(P::DroneGravity, 0.0f);
        r.snap(P::DroneEvolve, 0.0f);
        r.snap(P::DroneRevoice, 0.5f);
        r.run(0.5);
        r.snap(P::DroneChord, static_cast<float>(chord));
        r.run(2.5);
        std::set<int> heard;
        for (float i : r.last().droneVoiceInterval)
            heard.insert(static_cast<int>(std::lround(i * 100.0f)));
        INFO("chord " << tf::dsp::DroneGenerator::chordName(chord));
        CHECK(heard.size() >= 3);
        CHECK(std::isfinite(analyse(r.out, r.at(0.5), r.out.size()).rmsDb));
    }
    Rig a, b;
    for (auto* r : { &a, &b })
    {
        r->solo({ StripId::Drone });
        r->snap(P::DroneEvolve, 0.0f);
        r->snap(P::DroneRevoice, 0.5f);
        r->snap(P::DroneGravity, 0.0f);
    }
    a.snap(P::DroneChord, 3.0f);
    b.snap(P::DroneChord, 4.0f);
    a.run(3.0);
    b.run(3.0);
    CHECK(a.last().droneVoiceInterval[2] == 15.0f);
    CHECK(b.last().droneVoiceInterval[2] == 16.0f);
    report("drone.chord", "minor and major put the third voice on 15 and 16 semitones", "ok");
}

TEST_CASE("Control audit: drone glide sets how fast the root moves", "[audit]")
{
    std::vector<float> lag;
    for (float glide : { 0.05f, 2.0f, 20.0f })
    {
        Rig r;
        r.solo({ StripId::Drone });
        r.snap(P::DroneGlide, glide);
        r.snap(P::DroneGravity, 0.0f);
        r.run(0.5);
        r.snap(P::DroneRoot, 50.0f);
        r.run(1.0);
        lag.push_back(50.0f - r.last().droneVoiceNote[0]);
    }
    INFO(lag[0] << " " << lag[1] << " " << lag[2]);
    CHECK(lag[0] < 0.5f);
    CHECK(lag[1] > lag[0] + 1.0f);
    CHECK(lag[2] > lag[1] + 1.0f);
    report("drone.glide", "semitones still to travel 1 s after a 12 st jump", "ok | " + fmt(lag[0]) + " / " + fmt(lag[1]) + " / " + fmt(lag[2]));
}

TEST_CASE("Control audit: drone revoice time sets the crossfade between notes", "[audit]")
{
    std::vector<float> settled;
    for (float seconds : { 0.5f, 6.0f })
    {
        Rig r;
        r.solo({ StripId::Drone });
        r.snap(P::DroneEvolve, 0.0f);
        r.snap(P::DroneRevoice, seconds);
        r.run(0.5);
        r.snap(P::DroneChord, 4.0f);
        r.run(2.0);
        settled.push_back(r.last().droneVoiceInterval[2]);
    }
    CHECK(settled[0] == 16.0f);
    CHECK(settled[1] == 7.0f);
    report("drone.revoice", "a chord change lands within 2 s at 0.5 s, not yet at 6 s", "ok");
}
