// tidefield_render: renders a score through the engine offline and writes a WAV plus
// a JSON analysis report.
//
//   tidefield_render <score.json> [-o out.wav] [--report out.json] [--seed N] [--strict]
//                    [--save-session out.tidefield] [--stems dir]
//
// --stems writes the take the way the app's recorder does (dir/master.wav plus
// dir/stems/<strip>.wav), through the same record tap.
//
// --strict exits non-zero if the render contains non-finite samples or exceeds the
// limiter ceiling, so scores can run as regression tests.

#include "Analysis.h"
#include "Score.h"

#include <engine/Engine.h>
#include <engine/capture/CatchManager.h>
#include <engine/mix/FxManager.h>
#include <io/AudioFileIO.h>
#include <io/Recorder.h>
#include <io/Session.h>
#include <io/UiProtocol.h>

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
    juce::File saveSession;
    juce::File stems;
};

void printUsage()
{
    std::cerr << "usage: tidefield_render <score.json> [-o out.wav] [--report out.json] [--seed N] [--strict] [--save-session out.tidefield]\n"
                 "                       [--stems dir]\n";
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
        else if (arg == "--save-session")
            o.saveSession = cwd.getChildFile(next());
        else if (arg == "--stems")
            o.stems = cwd.getChildFile(next());
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
        o.report = o.output.getSiblingFile(o.output.getFileNameWithoutExtension() + ".report.json");
    if (o.report == o.score || o.output == o.score)
    {
        std::cerr << "error: refusing to overwrite the score file\n";
        return std::nullopt;
    }
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
    // `tidefield_render --dump-schema file.json` writes the UI schema (used by the web
    // UI's browser mock so it always matches the engine).
    // `--check-schema file.json` fails if that copy is stale (run as a ctest).
    if (argc == 3 && (juce::String(argv[1]) == "--dump-schema" || juce::String(argv[1]) == "--check-schema"))
    {
        tf::engine::Engine engine;
        engine.prepare(48000.0, 512);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
        const auto text = juce::JSON::toString(tf::io::buildSchema(engine));
        if (juce::String(argv[1]) == "--check-schema")
        {
            if (file.loadFileAsString() == text)
                return 0;
            std::cerr << file.getFullPathName() << " is out of date with the engine's parameters.\n"
                      << "Regenerate: tidefield_render --dump-schema ui/src/bridge/schema.json\n";
            return 1;
        }
        file.replaceWithText(text);
        std::cout << file.getFullPathName() << "\n";
        return 0;
    }

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

        tf::engine::FxManager fx(engine);
        if (score.defaultFx)
            fx.loadDefaultLayout();
        for (const auto& f : score.fx)
            fx.setType(f.slot, f.type);

        for (const auto& sm : score.samples)
        {
            juce::String error;
            auto buffer = tf::io::loadSample(sm.file, error);
            if (buffer == nullptr)
                throw std::runtime_error(error.toStdString());
            if (! engine.loadCloudSample(sm.cloud, std::move(buffer)))
                throw std::runtime_error("Could not load sample into cloud " + std::to_string(sm.cloud));
        }

        std::unique_ptr<tf::dsp::SampleBuffer> input;
        if (score.input != juce::File())
        {
            juce::String error;
            input = tf::io::loadSample(score.input, error);
            if (input == nullptr)
                throw std::runtime_error(error.toStdString());
        }
        std::vector<float> inputBlock(static_cast<std::size_t>(score.blockSize));
        tf::engine::TelemetryFrame lastFrame;
        std::size_t inputPos = 0;

        tf::engine::SceneManager scenes(engine);
        tf::engine::CatchManager catcher(engine);
        catcher.onCaught = [](int cloud, const std::string& name) { std::cerr << "caught " << name << " into cloud " << cloud + 1 << "\n"; };
        catcher.onRejected = [](const std::string& reason) { std::cerr << "catch rejected: " << reason << "\n"; };

        if (score.session != juce::File())
        {
            juce::String error;
            auto data = tf::io::loadSession(score.session, error);
            if (! data)
                throw std::runtime_error(error.toStdString());
            for (const auto& w : tf::io::applySession(*data, engine, scenes, fx, true))
                std::cerr << "session warning: " << w << "\n";
        }
        if (score.bloomSample != juce::File())
        {
            juce::String error;
            auto buffer = tf::io::loadSample(score.bloomSample, error);
            if (buffer == nullptr)
                throw std::runtime_error(error.toStdString());
            engine.loadBloomSample(std::move(buffer));
        }

        for (auto p : score.pins)
            scenes.setPinned(p, true);
        for (auto& scene : score.scenes)
            if (scenes.addScene(scene) < 0)
                throw std::runtime_error("Score has more scenes than the terrain holds");

        const auto total = static_cast<std::uint64_t>(score.durationSeconds * score.sampleRate);
        std::vector<std::vector<float>> out(2, std::vector<float>(static_cast<std::size_t>(total), 0.0f));

        std::unique_ptr<tf::io::Recorder> recorder;
        if (options->stems != juce::File())
        {
            options->stems.deleteRecursively();
            recorder = std::make_unique<tf::io::Recorder>(engine.getRecordTap());
            const auto result = recorder->start(options->stems, score.sampleRate, true);
            if (result.failed())
                throw std::runtime_error(result.getErrorMessage().toStdString());
        }

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
            if (input != nullptr)
            {
                for (int i = 0; i < block; ++i, ++inputPos)
                    inputBlock[static_cast<std::size_t>(i)] = input->left[inputPos % input->size()];
                const float* ins[2] = { inputBlock.data(), inputBlock.data() };
                engine.process(ins, 2, ptrs, 2, block);
            }
            else
            {
                engine.process(nullptr, 0, ptrs, 2, block);
            }

            scenes.tick();
            fx.tick();
            engine.collectGarbage();
            tf::engine::TelemetryFrame frame;
            while (engine.popTelemetry(frame)) {}
            tf::engine::EngineNotice notice;
            while (engine.popNotice(notice))
            {
                if (notice.type == tf::engine::EngineNotice::Type::GuardTripped)
                    std::cerr << "warning: safety guard tripped at " << notice.sampleTime / score.sampleRate << " s\n";
                catcher.handle(notice);
            }
            if (frame.sampleTime > 0)
                lastFrame = frame;
            if (recorder != nullptr)
                recorder->drainNow(); // offline runs faster than the writer thread polls

            pos += static_cast<std::uint64_t>(block);
        }

        if (recorder != nullptr)
        {
            recorder->stop();
            engine.getRecordTap().beginBlock(); // no more blocks: acknowledge the stop here
            recorder->drainNow();
            if (const auto dropped = recorder->getStatus().droppedFrames; dropped > 0)
                std::cerr << "warning: " << dropped << " frames were dropped from the stems\n";
            std::cerr << "wrote stems to " << options->stems.getFullPathName() << "\n";
        }

        if (! writeWav(options->output, out, score.sampleRate))
        {
            std::cerr << "error: could not write " << options->output.getFullPathName() << "\n";
            return 1;
        }

        if (options->saveSession != juce::File())
        {
            juce::String error;
            auto data = tf::io::captureSession(engine, lastFrame, scenes, fx);
            data.name = options->saveSession.getFileNameWithoutExtension().toStdString();
            if (! tf::io::saveSession(data, options->saveSession, error))
                throw std::runtime_error(error.toStdString());
            std::cerr << "saved session " << options->saveSession.getFullPathName() << "\n";
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
