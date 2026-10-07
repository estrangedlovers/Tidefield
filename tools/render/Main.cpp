// tidefield_render: renders a score through the engine offline and writes a WAV plus
// a JSON analysis report.
//
//   tidefield_render <score.json> [-o out.wav] [--report out.json] [--seed N] [--strict]
//
// --strict exits non-zero if the render contains non-finite samples or exceeds the
// limiter ceiling, so scores can run as regression tests.

#include "Analysis.h"
#include "Score.h"

#include <engine/Engine.h>

#include <dsp/core/Random.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <iostream>
#include <memory>
#include <optional>

namespace {

struct Options
{
    juce::File score;
    juce::File output;
    juce::File report;
    std::optional<std::uint64_t> seed;
    bool strict = false;
};

void printUsage()
{
    std::cerr << "usage: tidefield_render <score.json> [-o out.wav] [--report out.json] [--seed N] [--strict]\n";
}

std::optional<Options> parseArgs(int argc, char** argv)
{
    Options o;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        auto next = [&]() -> juce::String { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };

        if (arg == "-o" || arg == "--out")
            o.output = cwd.getChildFile(next());
        else if (arg == "--report")
            o.report = cwd.getChildFile(next());
        else if (arg == "--seed")
            o.seed = static_cast<std::uint64_t>(next().getLargeIntValue());
        else if (arg == "--strict")
            o.strict = true;
        else if (arg == "-h" || arg == "--help")
            return std::nullopt;
        else if (o.score == juce::File())
            o.score = cwd.getChildFile(arg);
        else
            return std::nullopt;
    }
    if (o.score == juce::File())
        return std::nullopt;
    if (o.output == juce::File())
        o.output = cwd.getChildFile("out").getChildFile(o.score.getFileNameWithoutExtension() + ".wav");
    if (o.report == juce::File())
        o.report = o.output.withFileExtension("json");
    return o;
}

bool writeWav(const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen())
        return false;

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate(sampleRate)
                             .withNumChannels(static_cast<int>(channels.size()))
                             .withBitsPerSample(32)
                             .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
        return false;

    std::vector<const float*> ptrs;
    for (const auto& ch : channels)
        ptrs.push_back(ch.data());
    return writer->writeFromFloatArrays(ptrs.data(), static_cast<int>(ptrs.size()), static_cast<int>(channels[0].size()));
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseArgs(argc, argv);
    if (! options)
    {
        printUsage();
        return 2;
    }

    try
    {
        tf::engine::ParamRegistry registry;
        auto score = tf::tools::Score::load(options->score, registry);
        if (options->seed)
            score.seed = *options->seed;

        tf::engine::Engine::Config config;
        config.seed = score.seed;
        config.controlQueueSize = std::max<std::size_t>(4096, score.events.size() + 16);
        tf::engine::Engine engine(config);
        engine.prepare(score.sampleRate, score.blockSize);

        const auto total = static_cast<std::uint64_t>(score.durationSeconds * score.sampleRate);
        std::vector<std::vector<float>> out(2, std::vector<float>(static_cast<std::size_t>(total), 0.0f));

        tf::dsp::Random blockRng(score.seed ^ 0xb10cull);
        std::size_t nextEvent = 0;
        std::uint64_t pos = 0;

        while (pos < total)
        {
            // Deliver every event due at or before this position.
            while (nextEvent < score.events.size() && score.events[nextEvent].sample <= pos)
                engine.post(score.events[nextEvent++].event);

            int block = score.randomBlockSizes ? 1 + blockRng.nextInt(score.blockSize) : score.blockSize;
            // Split blocks at event times so events land where the score says.
            if (nextEvent < score.events.size())
                block = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(block), score.events[nextEvent].sample - pos));
            block = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(block), total - pos));

            float* ptrs[2] = { out[0].data() + pos, out[1].data() + pos };
            engine.process(nullptr, 0, ptrs, 2, block);

            tf::engine::TelemetryFrame frame;
            while (engine.popTelemetry(frame)) {}
            tf::engine::EngineNotice notice;
            while (engine.popNotice(notice))
                if (notice.type == tf::engine::EngineNotice::Type::GuardTripped)
                    std::cerr << "warning: safety guard tripped at " << notice.sampleTime / score.sampleRate << " s\n";

            pos += static_cast<std::uint64_t>(block);
        }

        if (! writeWav(options->output, out, score.sampleRate))
        {
            std::cerr << "error: could not write " << options->output.getFullPathName() << "\n";
            return 1;
        }

        const float ceilingDb = registry.spec(tf::engine::P::MasterCeiling).defaultValue;
        const auto analysis = tf::tools::Analysis::run(out, score.sampleRate, ceilingDb);
        auto json = analysis.toJson();
        json.getDynamicObject()->setProperty("score", options->score.getFileName());
        json.getDynamicObject()->setProperty("seed", static_cast<juce::int64>(score.seed));
        options->report.replaceWithText(juce::JSON::toString(json));

        std::cout << options->output.getFullPathName() << "\n"
                  << juce::JSON::toString(json, true) << "\n";

        if (options->strict && (analysis.nonFiniteSamples > 0 || analysis.samplesAboveCeiling > 0))
        {
            std::cerr << "strict: render failed safety checks\n";
            return 1;
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
