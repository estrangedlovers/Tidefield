#pragma once

#include <dsp/core/Fft.h>
#include <dsp/core/MathUtil.h>
#include <dsp/core/SampleBuffer.h>
#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <engine/scene/SceneManager.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace tf::audit {
using namespace tf::engine;

inline constexpr double kFs = 48000.0;
inline constexpr int kBlock = 256;
inline constexpr float kMeasurable = 0.5f;
inline constexpr float kHalfMeasurable = 0.15f;
inline constexpr float kFloorDb = -150.0f;

inline const ParamRegistry& registry()
{
    static const ParamRegistry r;
    return r;
}

inline P param(const char* id)
{
    const auto i = registry().find(id);
    REQUIRE(i.has_value());
    return static_cast<P>(*i);
}

inline const ParamSpec& spec(P p) { return registry().spec(p); }

inline bool reporting() { return std::getenv("TF_AUDIT_REPORT") != nullptr; }

inline void report(const std::string& what, const std::string& context, const std::string& result)
{
    if (reporting())
        std::printf("AUDIT|%s|%s|%s\n", what.c_str(), context.c_str(), result.c_str());
}

struct Audio
{
    std::vector<float> l, r;
    std::size_t size() const noexcept { return l.size(); }
};

struct Features
{
    float rmsDb = kFloorDb;
    float peak = 0.0f;
    float centroid = 0.0f;
    float corr = 1.0f;
    float sideDb = -60.0f;
    float balanceDb = 0.0f;
    std::array<float, 10> bandDb {};
    bool finite = true;
};

inline float toDb(double energy) { return std::max(kFloorDb, 10.0f * static_cast<float>(std::log10(std::max(1.0e-30, energy)))); }

inline Features analyse(const Audio& a, std::size_t from, std::size_t to)
{
    Features f;
    to = std::min(to, a.size());
    from = std::min(from, to);
    double el = 0.0, er = 0.0, elr = 0.0, em = 0.0, es = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const float l = a.l[i], r = a.r[i];
        if (! std::isfinite(l) || ! std::isfinite(r))
            f.finite = false;
        else
            f.peak = std::max({ f.peak, std::fabs(l), std::fabs(r) });
    }
    for (std::size_t i = from; i < to; ++i)
    {
        const double l = a.l[i], r = a.r[i];
        if (! std::isfinite(l) || ! std::isfinite(r))
            continue;
        el += l * l;
        er += r * r;
        elr += l * r;
        em += 0.25 * (l + r) * (l + r);
        es += 0.25 * (l - r) * (l - r);
    }
    const double n = static_cast<double>(std::max<std::size_t>(1, to - from));
    f.rmsDb = toDb((el + er) / (2.0 * n));
    f.corr = el > 0.0 && er > 0.0 ? static_cast<float>(elr / std::sqrt(el * er)) : 1.0f;
    f.sideDb = std::clamp(toDb(es) - toDb(em), -60.0f, 60.0f);
    f.balanceDb = std::clamp(toDb(er) - toDb(el), -60.0f, 60.0f);

    constexpr int kOrder = 12;
    constexpr int kSize = 1 << kOrder;
    static thread_local dsp::Fft fft;
    if (fft.getSize() != kSize)
        fft.prepare(kOrder);
    std::vector<dsp::Fft::Complex> buf(kSize);
    std::vector<double> power(kSize / 2 + 1, 0.0);
    int frames = 0;
    for (std::size_t start = from; start + kSize <= to; start += kSize / 2)
    {
        for (int i = 0; i < kSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos(dsp::kTwoPi * static_cast<float>(i) / kSize);
            const auto k = start + static_cast<std::size_t>(i);
            const float m = std::isfinite(a.l[k]) && std::isfinite(a.r[k]) ? 0.5f * (a.l[k] + a.r[k]) : 0.0f;
            buf[static_cast<std::size_t>(i)] = { m * w, 0.0f };
        }
        fft.forward(buf.data());
        for (int k = 0; k <= kSize / 2; ++k)
            power[static_cast<std::size_t>(k)] += static_cast<double>(std::norm(buf[static_cast<std::size_t>(k)]));
        ++frames;
    }
    double num = 0.0, den = 0.0;
    std::array<double, 10> bands {};
    const double binHz = kFs / kSize;
    for (int k = 1; k <= kSize / 2; ++k)
    {
        const double hz = k * binHz;
        const double p = power[static_cast<std::size_t>(k)];
        num += hz * p;
        den += p;
        const int band = std::clamp(static_cast<int>(std::floor(std::log2(hz / 31.25))), 0, 9);
        bands[static_cast<std::size_t>(band)] += p;
    }
    f.centroid = den > 0.0 ? static_cast<float>(num / den) : 0.0f;
    for (std::size_t b = 0; b < bands.size(); ++b)
        f.bandDb[b] = toDb(bands[b] / std::max(1, frames));
    return f;
}

inline float distance(const Features& a, const Features& b)
{
    float d = std::fabs(a.rmsDb - b.rmsDb);
    if (a.centroid > 0.0f && b.centroid > 0.0f)
        d = std::max(d, 12.0f * std::fabs(std::log2(a.centroid / b.centroid)));
    float bandSum = 0.0f;
    int bandCount = 0;
    const float loudest = std::max(a.rmsDb, b.rmsDb);
    for (std::size_t k = 0; k < a.bandDb.size(); ++k)
        if (std::max(a.bandDb[k], b.bandDb[k]) > loudest - 20.0f)
        {
            bandSum += std::fabs(a.bandDb[k] - b.bandDb[k]);
            ++bandCount;
        }
    if (bandCount > 0)
        d = std::max(d, bandSum / static_cast<float>(bandCount));
    d = std::max(d, 20.0f * std::fabs(a.corr - b.corr));
    d = std::max(d, std::fabs(a.balanceDb - b.balanceDb));
    d = std::max(d, 0.5f * std::fabs(a.sideDb - b.sideDb));
    return d;
}

inline std::shared_ptr<dsp::SampleBuffer> pluckTrain(double seconds = 3.0)
{
    auto b = std::make_shared<dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->name = "audit plucks";
    const auto n = static_cast<std::size_t>(seconds * kFs);
    b->left.resize(n);
    constexpr int kNotes = 24;
    const std::size_t per = n / kNotes;
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t note = std::min<std::size_t>(i / per, kNotes - 1);
        const float hz = 150.0f * std::pow(2.0f, 4.0f * static_cast<float>(note) / kNotes);
        const float t = static_cast<float>(i - note * per) / static_cast<float>(kFs);
        const float env = std::exp(-t / 0.05f) * std::min(1.0f, t * 2000.0f);
        float s = 0.0f;
        for (int h = 1; h <= 4; ++h)
            s += std::sin(dsp::kTwoPi * hz * static_cast<float>(h) * t) / static_cast<float>(h);
        b->left[i] = 0.5f * env * s;
    }
    dsp::buildMips(*b);
    return b;
}

inline std::shared_ptr<dsp::SampleBuffer> oneShot()
{
    auto b = std::make_shared<dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->name = "audit one-shot";
    const auto n = static_cast<std::size_t>(1.6 * kFs);
    b->left.resize(n);
    b->right.resize(n);
    dsp::Random rng(5);
    for (std::size_t i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kFs);
        const float env = std::exp(-t / 0.45f) * std::min(1.0f, t * 400.0f);
        float s = 0.0f;
        for (int h = 1; h <= 12; ++h)
            s += std::sin(dsp::kTwoPi * 261.63f * static_cast<float>(h) * t + 0.3f * static_cast<float>(h)) / static_cast<float>(h);
        const float click = t < 0.01f ? 0.2f * rng.nextBipolar() : 0.0f;
        b->left[i] = 0.25f * env * s + click;
        b->right[i] = 0.25f * env * s * 0.9f + click * 0.7f;
    }
    dsp::buildMips(*b);
    return b;
}

inline std::shared_ptr<dsp::SampleBuffer> sineSample(float hz, double seconds = 3.0)
{
    auto b = std::make_shared<dsp::SampleBuffer>();
    b->sampleRate = kFs;
    b->name = "audit sine";
    b->left.resize(static_cast<std::size_t>(seconds * kFs));
    for (std::size_t i = 0; i < b->left.size(); ++i)
        b->left[i] = 0.4f * static_cast<float>(std::sin(2.0 * dsp::kPi * hz * static_cast<double>(i) / kFs));
    dsp::buildMips(*b);
    return b;
}

using InputFn = std::function<void(std::uint64_t t0, int n, float* l, float* r)>;

inline InputFn sineInput(double hz, float gain = 0.4f)
{
    return [hz, gain](std::uint64_t t0, int n, float* l, float* r) {
        for (int i = 0; i < n; ++i)
            l[i] = r[i] = gain * static_cast<float>(std::sin(2.0 * dsp::kPi * hz * static_cast<double>(t0 + static_cast<std::uint64_t>(i)) / kFs));
    };
}

inline InputFn testInput(float gain = 1.0f)
{
    return [gain](std::uint64_t t0, int n, float* l, float* r) {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(t0 + static_cast<std::uint64_t>(i)) / kFs;
            const float low = static_cast<float>(std::sin(2.0 * dsp::kPi * 45.0 * t));
            l[i] = gain * (0.3f * low + 0.2f * static_cast<float>(std::sin(2.0 * dsp::kPi * 330.0 * t)));
            r[i] = gain * (0.08f * low + 0.25f * static_cast<float>(std::sin(2.0 * dsp::kPi * 2500.0 * t)));
        }
    };
}

struct Rig
{
    Engine engine;
    FxManager fx { engine };
    std::unique_ptr<SceneManager> scenes;
    InputFn input;
    Audio out;
    std::vector<TelemetryFrame> frames;
    std::vector<EngineNotice> notices;
    std::vector<float> inL, inR;
    float swept = 0.0f;

    Rig() : Rig(Engine::Config {}) {}

    explicit Rig(const Engine::Config& config) : engine(config)
    {
        engine.prepare(kFs, kBlock);
        inL.assign(kBlock, 0.0f);
        inR.assign(kBlock, 0.0f);
        snap(P::MasterFadeSecs, 0.5f);
        solo({});
        engine.command(Command::FadeIn);
        run(0.55);
        for (const auto& s : kStrips)
            snap(s.level, spec(s.level).defaultValue);
        engine.command(Command::ResetFeedback);
        out = {};
        frames.clear();
        notices.clear();
        origin = engine.getSampleTime();
    }

    std::uint64_t origin = 0;

    double time(const TelemetryFrame& f) const { return static_cast<double>(f.sampleTime - origin) / kFs; }

    void snap(P p, float v) { engine.post(ControlEvent::snapParam(idx(p), v)); }
    void set(P p, float v) { engine.post(ControlEvent::setParam(idx(p), v)); }
    void note(int n, float vel) { engine.noteOn(n, vel); }
    void off(int n) { engine.noteOff(n); }

    void solo(std::initializer_list<StripId> on)
    {
        for (const auto& s : kStrips)
            snap(s.level, -60.0f);
        for (auto s : on)
            snap(kStrips[static_cast<std::size_t>(s)].level, 0.0f);
    }

    void effect(int slot, const char* type)
    {
        fx.setType(slot, type);
    }

    SceneManager& sceneManager()
    {
        if (scenes == nullptr)
            scenes = std::make_unique<SceneManager>(engine);
        return *scenes;
    }

    double seconds() const { return static_cast<double>(out.size()) / kFs; }
    std::size_t at(double t) const { return static_cast<std::size_t>(std::max(0.0, t) * kFs); }

    void run(double secs)
    {
        const int total = static_cast<int>(secs * kFs);
        const auto start = out.size();
        out.l.resize(start + static_cast<std::size_t>(total));
        out.r.resize(start + static_cast<std::size_t>(total));
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = std::min(kBlock, total - pos);
            float* outs[2] = { out.l.data() + start + static_cast<std::size_t>(pos), out.r.data() + start + static_cast<std::size_t>(pos) };
            if (input)
            {
                input(engine.getSampleTime(), n, inL.data(), inR.data());
                const float* ins[2] = { inL.data(), inR.data() };
                engine.process(ins, 2, outs, 2, n);
            }
            else
                engine.process(nullptr, 0, outs, 2, n);
            TelemetryFrame f;
            while (engine.popTelemetry(f))
                frames.push_back(f);
            EngineNotice note;
            while (engine.popNotice(note))
                notices.push_back(note);
            fx.tick();
            if (scenes != nullptr)
                scenes->tick();
            engine.collectGarbage();
        }
    }

    const TelemetryFrame& last() const
    {
        static const TelemetryFrame empty;
        return frames.empty() ? empty : frames.back();
    }

    template <typename Fn>
    float meanOver(double from, double to, Fn fn) const
    {
        double sum = 0.0;
        int n = 0;
        for (const auto& f : frames)
        {
            const double t = time(f);
            if (t >= from && t <= to)
            {
                sum += static_cast<double>(fn(f));
                ++n;
            }
        }
        return n > 0 ? static_cast<float>(sum / n) : 0.0f;
    }
};

enum class Metric { Any, Rms, Centroid, Side, Balance, Corr, Custom };

using Setup = std::function<void(Rig&)>;
using Apply = std::function<void(Rig&, float)>;
using Play = std::function<void(Rig&, double)>;
using Measure = std::function<float(Rig&, const Features&)>;

struct Check
{
    std::string name;
    std::string context;
    P param = P::MasterLevel;
    std::vector<P> covers;
    Setup setup;
    Apply apply;
    Play play;
    Setup reference;
    double seconds = 2.5;
    double from = 1.0;
    double to = 0.0;
    Metric metric = Metric::Any;
    int direction = 0;
    float minDelta = kMeasurable;
    Measure measure;
    std::vector<float> values;
    bool checkHalves = true;
    bool neighboursDiffer = true;
    float minChange = kMeasurable;
    std::vector<std::pair<float, float>> sameAllowed;
};

struct Outcome
{
    float value = 0.0f;
    Features f;
    float custom = 0.0f;
};

inline float metricValue(const Check& c, const Outcome& o)
{
    switch (c.metric)
    {
        case Metric::Rms: return o.f.rmsDb;
        case Metric::Centroid: return o.f.centroid > 0.0f ? 12.0f * std::log2(o.f.centroid) : 0.0f;
        case Metric::Side: return o.f.sideDb;
        case Metric::Balance: return o.f.balanceDb;
        case Metric::Corr: return 20.0f * o.f.corr;
        case Metric::Custom: return o.custom;
        case Metric::Any: return 0.0f;
    }
    return 0.0f;
}

inline float outcomeDistance(const Check& c, const Outcome& a, const Outcome& b)
{
    float d = distance(a.f, b.f);
    if (c.measure)
        d = std::max(d, std::fabs(a.custom - b.custom));
    return d;
}

inline bool isDiscrete(P p) { return (spec(p).flags & ParamFlag::kDiscrete) != 0; }

inline std::vector<float> sweepValues(const Check& c)
{
    if (! c.values.empty())
        return c.values;
    const auto& s = spec(c.param);
    if (isDiscrete(c.param))
    {
        std::vector<float> v;
        for (int k = static_cast<int>(s.minValue); k <= static_cast<int>(s.maxValue); ++k)
            v.push_back(static_cast<float>(k));
        return v;
    }
    return { s.minValue, s.fromNormalised(0.5f), s.maxValue };
}

inline void perform(const Check& c, Rig& rig, float value, const Setup& extra)
{
    rig.swept = value;
    if (c.setup)
        c.setup(rig);
    if (c.apply)
        c.apply(rig, value);
    else
        rig.snap(c.param, value);
    if (extra)
        extra(rig);
    if (c.play)
        c.play(rig, c.seconds);
    else
        rig.run(c.seconds);
}

inline Outcome render(const Check& c, float value)
{
    Rig rig;
    perform(c, rig, value, {});
    if (c.reference)
    {
        Rig ref;
        perform(c, ref, value, c.reference);
        for (std::size_t i = 0; i < rig.out.size() && i < ref.out.size(); ++i)
        {
            rig.out.l[i] -= ref.out.l[i];
            rig.out.r[i] -= ref.out.r[i];
        }
    }
    Outcome o;
    o.value = value;
    o.f = analyse(rig.out, rig.at(c.from), c.to > 0.0 ? rig.at(c.to) : rig.out.size());
    if (c.measure)
        o.custom = c.measure(rig, o.f);
    const float ceiling = dsp::dbToGain(rig.last().paramTargets[idx(P::MasterCeiling)]);
    INFO(c.name << " = " << value << " (" << c.context << "): peak " << o.f.peak << ", ceiling " << ceiling);
    CHECK(o.f.finite);
    CHECK(o.f.peak <= std::max(ceiling, dsp::dbToGain(-1.0f)) * (c.reference ? 2.0001f : 1.0001f));
    return o;
}

inline std::string fmt(float v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.3g", static_cast<double>(v));
    return b;
}

inline void runCheck(const Check& c)
{
    const auto values = sweepValues(c);
    std::vector<Outcome> outs;
    for (float v : values)
        outs.push_back(render(c, v));

    std::string result = "ok";
    bool pass = true;
    auto fail = [&](const std::string& why) {
        pass = false;
        result = "FAIL " + why;
    };

    for (const auto& o : outs)
        if (! o.f.finite)
            fail("non-finite at " + fmt(o.value));

    if (c.neighboursDiffer && outs.size() >= 2)
    {
        if (isDiscrete(c.param) || ! c.values.empty())
        {
            for (std::size_t i = 0; i + 1 < outs.size(); ++i)
            {
                const auto pair = std::make_pair(outs[i].value, outs[i + 1].value);
                if (std::find(c.sameAllowed.begin(), c.sameAllowed.end(), pair) != c.sameAllowed.end())
                    continue;
                const float d = outcomeDistance(c, outs[i], outs[i + 1]);
                INFO(c.name << ": " << outs[i].value << " vs " << outs[i + 1].value << " distance " << d);
                CHECK(d >= c.minChange);
                if (d < c.minChange)
                    fail(fmt(outs[i].value) + " and " + fmt(outs[i + 1].value) + " sound the same (" + fmt(d) + ")");
            }
        }
        else
        {
            const float total = outcomeDistance(c, outs.front(), outs.back());
            INFO(c.name << ": min vs max distance " << total);
            CHECK(total >= c.minChange);
            if (total < c.minChange)
                fail("min and max sound the same (" + fmt(total) + ")");
            if (c.checkHalves && outs.size() == 3)
            {
                const float low = outcomeDistance(c, outs[0], outs[1]);
                const float high = outcomeDistance(c, outs[1], outs[2]);
                INFO(c.name << ": lower half " << low << ", upper half " << high);
                CHECK(low >= kHalfMeasurable);
                CHECK(high >= kHalfMeasurable);
                if (low < kHalfMeasurable)
                    fail("lower half of the range is dead (" + fmt(low) + ")");
                if (high < kHalfMeasurable)
                    fail("upper half of the range is dead (" + fmt(high) + ")");
            }
        }
    }

    if (c.direction != 0 && c.metric != Metric::Any && outs.size() >= 2)
    {
        const float delta = (metricValue(c, outs.back()) - metricValue(c, outs.front())) * static_cast<float>(c.direction);
        INFO(c.name << ": directional change " << delta << " (want >= " << c.minDelta << ")");
        CHECK(delta >= c.minDelta);
        if (delta < c.minDelta)
            fail("moves the wrong way or too little (" + fmt(delta) + ")");
    }

    std::string detail;
    for (const auto& o : outs)
        detail += " " + fmt(o.value) + ":" + fmt(o.f.rmsDb) + "dB/" + fmt(o.f.centroid) + "Hz" + (c.measure ? "/m" + fmt(o.custom) : "");
    report(c.name, c.context, result + " |" + detail);
}

inline void runChecks(const std::vector<Check>& checks)
{
    for (const auto& c : checks)
    {
        DYNAMIC_SECTION(c.name << " [" << c.context << "]")
        {
            runCheck(c);
        }
    }
}

inline std::vector<P> covered(const std::vector<Check>& checks)
{
    std::vector<P> v;
    for (const auto& c : checks)
    {
        v.push_back(c.param);
        v.insert(v.end(), c.covers.begin(), c.covers.end());
    }
    return v;
}
}
