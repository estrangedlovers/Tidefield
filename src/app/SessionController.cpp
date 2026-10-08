#include "SessionController.h"

#include <thread>

namespace tf::app {

namespace {
constexpr float kSwapFadeSeconds = 1.5f;
const char* const kWildcard = "*.tidefield";
} // namespace

SessionController::SessionController(engine::Engine& e, engine::SceneManager& s, engine::FxManager& f, engine::MidiManager* m,
                                     engine::SeasonManager* sm, engine::PathManager* pm,
                                     engine::GestureManager* gm)
    : engine(e), scenes(s), fx(f), midi(m), seasons(sm), path(pm), gestures(gm)
{
}

SessionController::~SessionController() { *alive = false; }

void SessionController::runInBackground(std::function<void()> job)
{
    if (workers != nullptr)
        workers->addJob(std::move(job));
    else
        std::thread(std::move(job)).detach();
}

void SessionController::newSession()
{
    current = juce::File();
    apply(std::make_shared<io::SessionData>(makeNewSession ? makeNewSession() : io::defaultSession(engine)));
}

void SessionController::open()
{
    chooser = std::make_unique<juce::FileChooser>("Open session", current.getParentDirectory(), kWildcard);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, token = alive](const juce::FileChooser& fc) {
                             if (*token && fc.getResult() != juce::File())
                                 openFile(fc.getResult());
                         });
}

void SessionController::openFile(const juce::File& file)
{
    if (busy)
        return;
    busy = true;
    if (onStatus)
        onStatus("Opening " + file.getFileName() + "...");
    runInBackground([this, file, token = alive] {
        juce::String error;
        auto data = io::loadSession(file, error);
        std::shared_ptr<io::SessionData> shared = data ? std::make_shared<io::SessionData>(std::move(*data)) : nullptr;
        juce::MessageManager::callAsync([this, token, file, shared, error] {
            if (! *token)
                return;
            busy = false;
            if (shared == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not open session", error);
                return;
            }
            current = file;
            apply(shared);
        });
    });
}

void SessionController::apply(std::shared_ptr<io::SessionData> data)
{
    pending = std::move(data);
    if (latest.fadeState == engine::FadeState::Silent || latest.panicActive)
    {
        applyNow();
        return;
    }
    // Fade out, swap at silence (FadeOutComplete), fade back in.
    waitingForFadeOut = true;
    engine.post(engine::ControlEvent::snapParam(engine::idx(engine::P::MasterFadeSecs), kSwapFadeSeconds));
    engine.command(engine::Command::FadeOut);
}

void SessionController::applyNow()
{
    if (pending == nullptr)
        return;
    const bool resume = waitingForFadeOut;
    waitingForFadeOut = false;
    auto warnings = io::applySession(*pending, engine, scenes, fx, true, midi, seasons, path, gestures);
    if (onApplied)
        onApplied(*pending);
    const auto fadeIt = pending->params.find("master.fadeSeconds");
    const float sessionFade = fadeIt != pending->params.end() ? fadeIt->second
                                                              : engine.getRegistry().spec(engine::P::MasterFadeSecs).defaultValue;
    pending.reset();

    if (resume)
    {
        // Fade back in over the swap time, then restore the session's own fade length.
        engine.post(engine::ControlEvent::snapParam(engine::idx(engine::P::MasterFadeSecs), kSwapFadeSeconds));
        engine.command(engine::Command::FadeIn);
        engine.post(engine::ControlEvent::setParam(engine::idx(engine::P::MasterFadeSecs), sessionFade));
    }
    if (onStatus)
        onStatus(warnings.empty() ? "Opened " + getName()
                                  : "Opened " + getName() + " with " + juce::String(static_cast<int>(warnings.size())) + " warnings: "
                                        + juce::String(warnings.front()));
    if (onSessionChanged)
        onSessionChanged();
}

void SessionController::handleNotice(const engine::EngineNotice& notice)
{
    if (waitingForFadeOut && notice.type == engine::EngineNotice::Type::FadeOutComplete)
        applyNow();
}

void SessionController::save()
{
    if (current == juce::File())
        saveAs();
    else
        saveTo(current);
}

void SessionController::saveAs()
{
    chooser = std::make_unique<juce::FileChooser>("Save session", current == juce::File() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory) : current,
                                                  kWildcard);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, token = alive](const juce::FileChooser& fc) {
                             if (! *token || fc.getResult() == juce::File())
                                 return;
                             saveTo(fc.getResult().withFileExtension(io::kSessionExtension));
                         });
}

void SessionController::saveTo(const juce::File& file)
{
    if (busy)
        return;
    busy = true;
    // Capture on the message thread (cheap: values plus shared sample references),
    // encode and write on a worker.
    auto data = std::make_shared<io::SessionData>(io::captureSession(engine, latest, scenes, fx, midi, seasons, path, gestures));
    data->name = file.getFileNameWithoutExtension().toStdString();
    if (onStatus)
        onStatus("Saving " + file.getFileName() + "...");
    runInBackground([this, file, data, token = alive] {
        juce::String error;
        const bool ok = io::saveSession(*data, file, error);
        juce::MessageManager::callAsync([this, token, file, ok, error] {
            if (! *token)
                return;
            busy = false;
            if (! ok)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not save session", error);
                return;
            }
            current = file;
            if (onStatus)
                onStatus("Saved " + file.getFileName());
            if (onSessionChanged)
                onSessionChanged();
        });
    });
}

} // namespace tf::app
