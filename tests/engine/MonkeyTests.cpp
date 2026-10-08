#include <dsp/core/MathUtil.h>
#include <dsp/core/Random.h>
#include <dsp/fx/ProcessorFactory.h>
#include <engine/Engine.h>
#include <engine/mix/FxManager.h>
#include <engine/mod/ModRouteManager.h>
#include <engine/mod/SeasonManager.h>
#include <engine/scene/SceneManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace tf;
using namespace tf::engine;

namespace {
constexpr double kFs = 48000.0;
}

TEST_CASE("Monkey: random everything never breaks the output", "[monkey]")
{
    Engine engine;
    engine.prepare(kFs, 512);
    SceneManager scenes(engine);
    FxManager fx(engine);
    SeasonManager seasons(engine);
    ModRouteManager routes(engine);
    fx.loadDefaultLayout();

    auto glass = std::make_shared<dsp::SampleBuffer>();
    glass->sampleRate = kFs;
    glass->left.resize(48000);
    for (std::size_t i = 0; i < glass->left.size(); ++i)
        glass->left[i] = 0.5f * std::sin(0.05f * static_cast<float>(i)) * std::exp(-2.0f * static_cast<float>(i) / 48000.0f);
    for (int k = 0; k < kNumClouds; ++k)
        engine.loadCloudSample(k, glass);
    engine.loadBloomSample(glass);

    dsp::Random rng(2026);
    const auto& reg = engine.getRegistry();
    const auto& types = dsp::ProcessorFactory::instance().entries();
    const Command commands[] = { Command::FadeIn, Command::FadeOut, Command::Panic, Command::ResumeFromPanic, Command::ResetFeedback,
                                 Command::ReleaseLiveLayer, Command::Catch, Command::LoopRecord, Command::LoopClear };

    std::vector<float> l(512), r(512), in(512);
    const float ceiling = dsp::dbToGain(reg.spec(P::MasterCeiling).defaultValue) + 1.0e-5f;
    double t = 0.0;
    float peak = 0.0f;
    engine.command(Command::FadeIn);
    while (t < 120.0)
    {
        for (int k = 0; k < 6; ++k)
        {
            const auto p = static_cast<ParamIndex>(rng.nextInt(static_cast<int>(kNumParams)));
            if (p == idx(P::MasterCeiling))
                continue;
            const auto& s = reg.spec(p);
            engine.setParam(static_cast<P>(p), s.minValue + rng.nextFloat() * (s.maxValue - s.minValue));
        }
        if (rng.chance(0.02f))
            engine.command(commands[rng.nextInt(9)]);
        if (rng.chance(0.05f))
            engine.noteOn(36 + rng.nextInt(60), rng.nextFloat());
        if (rng.chance(0.05f))
            engine.noteOff(36 + rng.nextInt(60));
        if (rng.chance(0.01f))
            fx.setType(rng.nextInt(kNumFxSlots), rng.chance(0.2f) ? "" : types[static_cast<std::size_t>(rng.nextInt(static_cast<int>(types.size())))].info->typeId, true);
        if (rng.chance(0.005f))
            scenes.captureScene({}, { rng.nextFloat(), rng.nextFloat() }, TelemetryFrame {});
        if (rng.chance(0.003f))
        {
            Season s;
            s.param = static_cast<ParamIndex>(rng.nextInt(static_cast<int>(kNumParams)));
            s.depth = rng.nextBipolar();
            s.periodSeconds = 20.0f + 100.0f * rng.nextFloat();
            seasons.set(static_cast<int>(seasons.getSeasons().size()) % kMaxSeasons, s);
        }
        if (rng.chance(0.005f))
            routes.add(static_cast<ModSource>(rng.nextInt(kNumModSources)), static_cast<ParamIndex>(rng.nextInt(static_cast<int>(kNumParams))),
                       rng.nextBipolar());
        if (rng.chance(0.003f) && ! routes.getRoutes().empty())
            routes.remove(rng.nextInt(static_cast<int>(routes.getRoutes().size())));
        if (rng.chance(0.01f))
            engine.command(Command::FadeIn);

        const int n = 1 + rng.nextInt(512);
        for (int i = 0; i < n; ++i)
            in[static_cast<std::size_t>(i)] = 0.3f * rng.nextBipolar();
        float* outs[2] = { l.data(), r.data() };
        const float* ins[2] = { in.data(), in.data() };
        engine.process(ins, 2, outs, 2, n);
        for (int i = 0; i < n; ++i)
        {
            const float a = l[static_cast<std::size_t>(i)], b = r[static_cast<std::size_t>(i)];
            REQUIRE(std::isfinite(a));
            REQUIRE(std::isfinite(b));
            peak = std::max({ peak, std::fabs(a), std::fabs(b) });
        }
        REQUIRE(peak <= ceiling);

        scenes.tick();
        fx.tick();
        seasons.tick();
        routes.tick();
        engine.collectGarbage();
        TelemetryFrame f;
        while (engine.popTelemetry(f)) {}
        EngineNotice notice;
        while (engine.popNotice(notice)) {}
        t += n / kFs;
    }

    engine.command(Command::ResumeFromPanic);
    engine.setParam(P::MasterFadeSecs, 0.5f);
    engine.setParam(P::MasterLevel, 0.0f);
    engine.command(Command::FadeIn);
    float recovered = 0.0f;
    for (int b = 0; b < static_cast<int>(3.0 * kFs / 512); ++b)
    {
        float* outs[2] = { l.data(), r.data() };
        const float* ins[2] = { in.data(), in.data() };
        engine.process(ins, 2, outs, 2, 512);
        for (int i = 0; i < 512; ++i)
            recovered = std::max({ recovered, std::fabs(l[static_cast<std::size_t>(i)]), std::fabs(r[static_cast<std::size_t>(i)]) });
        TelemetryFrame f;
        while (engine.popTelemetry(f)) {}
        EngineNotice notice;
        while (engine.popNotice(notice)) {}
    }
    CHECK(recovered > 0.01f);
    CHECK(recovered <= ceiling);
}
