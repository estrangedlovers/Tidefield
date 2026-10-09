#include "SessionController.h"

#include <thread>

namespace tf::app {
namespace {
constexpr float kSwapFadeSeconds = 1.5f;
constexpr int kBaselineTicks = 15;
const char* const kWildcard = io::kSessionWildcard;
}

SessionController::SessionController(engine::Engine& e, engine::SceneManager& s, engine::FxManager& f, engine::MidiManager* m,
                                     engine::SeasonManager* sm, engine::PathManager* pm,
                                     engine::GestureManager* gm, engine::ModRouteManager* mm)
    : engine(e), scenes(s), fx(f), midi(m), seasons(sm), path(pm), gestures(gm), mod(mm)
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
    whenSafeToDiscard("starting a new session", [this] { newSessionNow(); });
}

void SessionController::newSessionNow()
{
    current = juce::File();
    apply(std::make_shared<io::SessionData>(makeNewSession ? makeNewSession() : io::defaultSession(engine)));
}

void SessionController::open()
{
    whenSafeToDiscard("opening another session", [this] {
    chooser = std::make_unique<juce::FileChooser>("Open session", current.getParentDirectory(), kWildcard);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, token = alive](const juce::FileChooser& fc) {
                             if (*token && fc.getResult() != juce::File())
                                 openFileNow(fc.getResult());
                         });
    });
}

void SessionController::openFile(const juce::File& file)
{
    whenSafeToDiscard("opening " + file.getFileNameWithoutExtension(), [this, file] { openFileNow(file); });
}

void SessionController::openFileNow(const juce::File& file)
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
                replaceAfterOpen = false;
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not open session", error);
                return;
            }
            current = replaceAfterOpen ? afterOpen : file;
            changedSinceSave = replaceAfterOpen;
            replaceAfterOpen = false;
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
    auto warnings = io::applySession(*pending, engine, scenes, fx, true, midi, seasons, path, gestures, mod);
    if (onApplied)
        onApplied(*pending);
    const auto fadeIt = pending->params.find("master.fadeSeconds");
    const float sessionFade = fadeIt != pending->params.end() ? fadeIt->second
                                                              : engine.getRegistry().spec(engine::P::MasterFadeSecs).defaultValue;
    pending.reset();
    baseline.reset();
    baselineDue = kBaselineTicks;

    if (resume)
    {
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

void SessionController::save(std::function<void(bool)> then)
{
    if (current == juce::File())
        saveAs(std::move(then));
    else
        saveTo(current.withFileExtension(io::kSessionExtension), std::move(then));
}

void SessionController::saveAs(std::function<void(bool)> then)
{
    chooser = std::make_unique<juce::FileChooser>("Save project",
                                                  current == juce::File() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                                          : current.withFileExtension(io::kSessionExtension),
                                                  "*.tide");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, token = alive, then = std::move(then)](const juce::FileChooser& fc) {
                             if (! *token)
                                 return;
                             if (fc.getResult() == juce::File())
                             {
                                 if (then)
                                     then(false);
                                 return;
                             }
                             saveTo(fc.getResult().withFileExtension(io::kSessionExtension), then);
                         });
}

void SessionController::openRecovered(const juce::File& recovery, const juce::File& original)
{
    if (busy)
        return;
    afterOpen = original;
    replaceAfterOpen = true;
    openFileNow(recovery);
}

void SessionController::autosaveTo(const juce::File& file, std::function<void(bool)> done)
{
    if (busy || autosaving)
        return;
    autosaving = true;
    auto data = std::make_shared<io::SessionData>(io::captureSession(engine, latest, scenes, fx, midi, seasons, path, gestures, mod));
    data->name = getName().toStdString();
    runInBackground([this, file, data, done = std::move(done), token = alive] {
        juce::String error;
        file.getParentDirectory().createDirectory();
        const bool ok = io::saveSession(*data, file, error);
        juce::MessageManager::callAsync([this, token, ok, done] {
            if (! *token)
                return;
            autosaving = false;
            if (done)
                done(ok);
        });
    });
}

io::SessionData SessionController::captureNow() const
{
    return io::captureSession(engine, latest, scenes, fx, midi, seasons, path, gestures, mod);
}

bool SessionController::hasUnsavedChanges() const
{
    if (changedSinceSave)
        return true;
    if (baseline == nullptr)
        return false;
    return ! io::sameContent(*baseline, captureNow());
}

void SessionController::tick()
{
    if (baselineDue > 0 && ! busy && pending == nullptr && --baselineDue == 0)
        baseline = std::make_shared<const io::SessionData>(captureNow());
}

void SessionController::whenSafeToDiscard(const juce::String& action, std::function<void()> proceed)
{
    if (asking)
        return;
    if (! askBeforeDiscard || ! hasUnsavedChanges())
    {
        proceed();
        return;
    }
    asking = true;
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Save changes to " + getName() + "?")
                       .withMessage("The session has changes that are not saved. Save them before " + action + "?")
                       .withButton("Save")
                       .withButton("Don't Save")
                       .withButton("Cancel");
    juce::AlertWindow::showAsync(options, [this, token = alive, proceed = std::move(proceed)](int result) {
        if (! *token)
            return;
        asking = false;
        if (result == 1)
            save([proceed](bool ok) {
                if (ok)
                    proceed();
            });
        else if (result == 2)
            proceed();
    });
}

void SessionController::saveTo(const juce::File& file, std::function<void(bool)> then)
{
    if (busy)
    {
        if (then)
            then(false);
        return;
    }
    busy = true;
    auto data = std::make_shared<io::SessionData>(captureNow());
    data->name = file.getFileNameWithoutExtension().toStdString();
    if (onStatus)
        onStatus("Saving " + file.getFileName() + "...");
    runInBackground([this, file, data, then = std::move(then), token = alive] {
        juce::String error;
        const bool ok = io::saveSession(*data, file, error);
        juce::MessageManager::callAsync([this, token, file, data, ok, error, then] {
            if (! *token)
                return;
            busy = false;
            if (! ok)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not save session", error);
                if (then)
                    then(false);
                return;
            }
            current = file;
            baseline = data;
            baselineDue = 0;
            changedSinceSave = false;
            if (onStatus)
                onStatus("Saved " + file.getFileName());
            if (onSessionChanged)
                onSessionChanged();
            if (then)
                then(true);
        });
    });
}
}
