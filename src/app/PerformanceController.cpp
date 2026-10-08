#include "PerformanceController.h"

#include "AppCore.h"
#include "Undo.h"

#include <io/Recorder.h>

#include <map>

namespace tf::app {
namespace {
constexpr int kConfirmTicks = 45;
}

PerformanceController::PerformanceController(AppCore& c) : core(c) {}

PerformanceController::~PerformanceController()
{
    *alive = false;
    cancel.store(true);
}

double PerformanceController::getPosition() const noexcept
{
    if (state == State::Idle)
        return playOrigin;
    return static_cast<double>(core.latest().performanceSeconds);
}

void PerformanceController::record()
{
    if (state == State::Recording)
    {
        stop();
        return;
    }
    if (! core.host.isRunning())
    {
        core.status("No audio device is running, so there is nothing to record.", true);
        return;
    }
    if (state == State::Playing)
        stop();
    io::Performance fresh;
    fresh.start = io::captureSession(core.engine, core.latest(), core.scenes, core.fx, &core.midi, &core.seasons, &core.paths, &core.gestures, &core.mod);
    fresh.sampleRate = core.engine.getSampleRate();
    fresh.startedOpen = core.latest().fadeState == engine::FadeState::Open || core.latest().fadeState == engine::FadeState::FadingIn;
    engine::GestureEvent stale;
    while (core.engine.popPerformance(stale)) {}
    performance = std::move(fresh);
    selection = {};
    playOrigin = 0.0;
    ++revision;
    core.engine.setPerformanceRecording(true);
    state = State::Recording;
    confirmCountdown = kConfirmTicks;
    core.status("Recording the performance: everything you play and move is kept. Press Stop when you are done.");
}

void PerformanceController::stopRecording()
{
    core.engine.setPerformanceRecording(false);
    engine::GestureEvent g;
    while (core.engine.popPerformance(g))
        performance.events.push_back(g);
    const auto heard = static_cast<std::uint64_t>(static_cast<double>(core.latest().performanceSeconds) * performance.sampleRate);
    performance.length = std::max(heard, performance.events.empty() ? std::uint64_t { 0 } : performance.events.back().time);
    state = State::Idle;
    ++revision;
    core.status("Performance kept: " + juce::String(static_cast<int>(performance.events.size())) + " moves over "
                + juce::String(performance.seconds(), 1) + " s. Play it back, edit it or render it.");
}

void PerformanceController::play(double fromSeconds)
{
    if (state == State::Recording)
        stopRecording();
    if (performance.empty())
        return;
    if (! core.host.isRunning())
    {
        core.status("No audio device is running, so the performance cannot play. Render it instead.", true);
        return;
    }
    const double from = std::clamp(fromSeconds, 0.0, performance.seconds());
    io::applySession(performance.start, core.engine, core.scenes, core.fx, false, &core.midi, &core.seasons, &core.paths, &core.gestures, &core.mod);
    core.seedTargets(performance.start);

    const auto at = performance.toSamples(from);
    const auto events = performance.playable();
    std::map<engine::ParamIndex, float> caughtUp;
    for (const auto& g : events)
    {
        if (g.time >= at)
            break;
        if (g.event.type == engine::ControlEvent::Type::SetParam)
            caughtUp[g.event.param] = g.event.value;
    }
    for (const auto& [param, value] : caughtUp)
        core.engine.post(engine::ControlEvent::setParam(param, value, engine::ControlSource::UI));
    if (performance.startedOpen && core.latest().fadeState != engine::FadeState::Open)
        core.engine.command(engine::Command::FadeIn);

    auto take = std::make_unique<engine::GestureTake>();
    take->events = events;
    take->length = performance.length;
    take->sampleRate = performance.sampleRate;
    take->loop = false;
    take->startAt = at;
    take->version = ++takeVersion;
    if (! core.engine.publishPerformance(std::move(take)))
    {
        core.status("The engine is busy; try playing again in a moment.", true);
        return;
    }
    playOrigin = from;
    state = State::Playing;
    confirmCountdown = kConfirmTicks;
}

void PerformanceController::stop()
{
    if (state == State::Recording)
    {
        stopRecording();
        return;
    }
    if (state == State::Playing)
    {
        auto none = std::make_unique<engine::GestureTake>();
        none->version = ++takeVersion;
        core.engine.publishPerformance(std::move(none));
        playOrigin = getPosition();
        state = State::Idle;
    }
}

void PerformanceController::tick()
{
    const auto engineState = core.latest().performanceState;
    if (state == State::Recording)
    {
        engine::GestureEvent g;
        while (core.engine.popPerformance(g))
            performance.events.push_back(g);
        if (engineState == 1)
        {
            confirmCountdown = 0;
            performance.length = static_cast<std::uint64_t>(static_cast<double>(core.latest().performanceSeconds) * performance.sampleRate);
        }
        else if (confirmCountdown > 0 && --confirmCountdown == 0)
            stopRecording();
    }
    else if (state == State::Playing)
    {
        if (engineState == 2)
            confirmCountdown = 0;
        else if (confirmCountdown == 0 || --confirmCountdown == 0)
        {
            state = State::Idle;
            playOrigin = 0.0;
        }
    }
}

void PerformanceController::load(io::Performance p)
{
    stop();
    performance = std::move(p);
    selection = {};
    playOrigin = 0.0;
    ++revision;
}

void PerformanceController::clear()
{
    edit("Clear performance", [](io::Performance& p) { p = io::Performance {}; });
    selection = {};
    playOrigin = 0.0;
}

void PerformanceController::edit(const juce::String& name, const std::function<void(io::Performance&)>& change)
{
    if (state == State::Recording)
        return;
    auto before = performance;
    change(performance);
    ++revision;
    core.undo.beginNewTransaction(name);
    core.undo.perform(new SnapshotAction<io::Performance>(std::move(before), performance,
                                                          [this, token = std::weak_ptr<bool>(alive)](const io::Performance& p) {
                                                              if (token.expired())
                                                                  return;
                                                              performance = p;
                                                              selection = {};
                                                              ++revision;
                                                          }),
                      name);
}

void PerformanceController::save()
{
    if (performance.empty())
    {
        core.status("Record a performance first.", true);
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Save performance", core.getRecordingsFolder().getChildFile("Performance.tidefield"), "*.tidefield");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, token = std::weak_ptr<bool>(alive)](const juce::FileChooser& fc) {
                             if (token.expired() || fc.getResult() == juce::File())
                                 return;
                             const auto file = fc.getResult().withFileExtension(io::kSessionExtension);
                             auto data = std::make_shared<io::SessionData>(performance.start);
                             data->name = file.getFileNameWithoutExtension().toStdString();
                             data->performance = io::performanceToJson(performance, core.engine.getRegistry());
                             core.workers.addJob([this, token, file, data] {
                                 juce::String error;
                                 const bool ok = io::saveSession(*data, file, error);
                                 juce::MessageManager::callAsync([this, token, file, ok, error] {
                                     if (token.expired())
                                         return;
                                     core.status(ok ? "Performance saved: " + file.getFileName() : "Could not save the performance: " + error, ! ok);
                                 });
                             });
                         });
}

void PerformanceController::render(bool stems, double loopCrossfadeSeconds)
{
    if (rendering)
    {
        cancelRender();
        return;
    }
    if (performance.empty())
    {
        core.status("Record a performance first.", true);
        return;
    }
    chooseFolderAndRender(std::make_shared<io::Performance>(performance), stems, loopCrossfadeSeconds);
}

void PerformanceController::renderSoundAsLoop(double seconds, double crossfadeSeconds)
{
    if (rendering)
        return;
    auto still = std::make_shared<io::Performance>();
    still->start = io::captureSession(core.engine, core.latest(), core.scenes, core.fx, &core.midi, &core.seasons, &core.paths, &core.gestures, &core.mod);
    still->sampleRate = 48000.0;
    still->length = static_cast<std::uint64_t>(std::max(1.0, seconds) * still->sampleRate);
    still->startedOpen = true;
    chooseFolderAndRender(std::move(still), false, crossfadeSeconds);
}

void PerformanceController::chooseFolderAndRender(std::shared_ptr<const io::Performance> source, bool stems, double loopCrossfadeSeconds)
{
    chooser = std::make_unique<juce::FileChooser>("Render into which folder?", core.getRecordingsFolder());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, source, stems, loopCrossfadeSeconds, token = std::weak_ptr<bool>(alive)](const juce::FileChooser& fc) {
                             if (token.expired() || fc.getResult() == juce::File())
                                 return;
                             startRender(source, io::Recorder::makeFolder(fc.getResult(), loopCrossfadeSeconds > 0.0 ? "Loop" : "Performance"), stems,
                                         loopCrossfadeSeconds);
                         });
}

void PerformanceController::startRender(std::shared_ptr<const io::Performance> copy, const juce::File& folder, bool stems, double loopCrossfadeSeconds)
{
    if (rendering)
        return;
    rendering = true;
    cancel.store(false);
    progress.store(0.0f);
    core.status(loopCrossfadeSeconds > 0.0 ? "Rendering a seamless loop..." : "Rendering the performance...");
    core.workers.addJob([this, copy, folder, stems, loopCrossfadeSeconds, token = std::weak_ptr<bool>(alive)] {
        io::RenderOptions options;
        options.folder = folder;
        options.stems = stems;
        options.sampleRate = copy->sampleRate;
        options.loopCrossfadeSeconds = loopCrossfadeSeconds;
        options.cancel = &cancel;
        options.onProgress = [this](float p) { progress.store(p); };
        const auto result = io::renderPerformance(*copy, options);
        juce::MessageManager::callAsync([this, token, result] {
            if (token.expired())
                return;
            rendering = false;
            if (! result.ok)
            {
                core.status(result.error, true);
                return;
            }
            juce::String message = "Rendered " + result.master.getFileName() + " in " + result.master.getParentDirectory().getFileName();
            if (! result.warnings.empty())
                message << " (" << static_cast<int>(result.warnings.size()) << " warnings: " << juce::String(result.warnings.front()) << ")";
            core.status(message, ! result.warnings.empty());
            result.master.revealToUser();
        });
    });
}
}
