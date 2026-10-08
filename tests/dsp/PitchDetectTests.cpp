#include <dsp/analysis/PitchDetect.h>
#include <dsp/core/Random.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tf;
using Catch::Approx;

namespace {
dsp::SampleBuffer tone(float hz, int harmonics, float seconds = 2.0f)
{
    dsp::SampleBuffer b;
    b.sampleRate = 48000.0;
    b.left.resize(static_cast<std::size_t>(seconds * 48000.0f));
    for (std::size_t i = 0; i < b.left.size(); ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        float v = 0.0f;
        for (int h = 1; h <= harmonics; ++h)
            v += std::sin(2.0f * 3.14159265f * hz * static_cast<float>(h) * t) / static_cast<float>(h);
        b.left[i] = 0.4f * v * std::exp(-0.6f * t);
    }
    return b;
}
}

TEST_CASE("Pitch detection finds the root of plain and rich tones", "[pitch]")
{
    for (float note : { 33.0f, 45.0f, 57.0f, 69.0f, 81.0f })
    {
        const float hz = 440.0f * std::exp2((note - 69.0f) / 12.0f);
        for (int harmonics : { 1, 8 })
        {
            const auto p = dsp::detectPitch(tone(hz, harmonics));
            REQUIRE(p.has_value());
            CHECK(p->midiNote == Approx(note).margin(0.15f));
        }
    }
}

TEST_CASE("Noise and silence have no pitch", "[pitch]")
{
    dsp::SampleBuffer noise;
    noise.sampleRate = 48000.0;
    noise.left.resize(96000);
    dsp::Random rng(5);
    for (auto& x : noise.left)
        x = 0.3f * rng.nextBipolar();
    CHECK_FALSE(dsp::detectPitch(noise).has_value());
    dsp::SampleBuffer silence;
    silence.sampleRate = 48000.0;
    silence.left.assign(96000, 0.0f);
    CHECK_FALSE(dsp::detectPitch(silence).has_value());
}
