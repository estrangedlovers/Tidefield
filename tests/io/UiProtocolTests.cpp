#include <engine/Engine.h>
#include <io/UiProtocol.h>

#include <catch2/catch_test_macros.hpp>

using namespace tf;

TEST_CASE("The schema describes every parameter and processor")
{
    engine::Engine e;
    e.prepare(48000.0, 256);
    const auto schema = io::buildSchema(e);
    REQUIRE(schema["params"].size() == static_cast<int>(engine::kNumParams));
    REQUIRE(schema["params"][0]["id"].toString() == "master.level");
    REQUIRE(schema["strips"].size() == engine::kNumStrips);
    REQUIRE(schema["fxSlots"].size() == engine::kNumFxSlots);
    REQUIRE(schema["processors"].size() >= 3);
    REQUIRE(schema["processors"][0]["controls"].size() == 6);
    REQUIRE(schema["scales"].size() == 12);
    const auto text = juce::JSON::toString(schema, true);
    REQUIRE(juce::JSON::parse(text)["limits"]["scenes"].operator int() == engine::kMaxScenes);
}

TEST_CASE("Telemetry sends every parameter once, then only changes")
{
    engine::TelemetryFrame f;
    for (std::size_t i = 0; i < engine::kNumParams; ++i)
        f.paramTargets[i] = static_cast<float>(i);
    io::TelemetryEncoder enc;

    auto first = enc.encode(f);
    REQUIRE(static_cast<bool>(first["full"]));
    REQUIRE(first["p"].size() == static_cast<int>(engine::kNumParams));
    REQUIRE(first["live"].size() == static_cast<int>(engine::kNumParams));

    auto second = enc.encode(f);
    REQUIRE_FALSE(second.hasProperty("p"));
    REQUIRE_FALSE(second.hasProperty("live"));

    f.paramTargets[5] = 99.0f;
    f.live[7] = 1;
    auto third = enc.encode(f);
    REQUIRE(third["p"].size() == 1);
    REQUIRE(static_cast<int>(third["p"][0][0]) == 5);
    REQUIRE(third["live"].size() == 1);

    enc.reset();
    auto fourth = enc.encode(f);
    REQUIRE(fourth["p"].size() == static_cast<int>(engine::kNumParams));

    REQUIRE(juce::JSON::toString(enc.encode(f), true).length() < 16000);
}
