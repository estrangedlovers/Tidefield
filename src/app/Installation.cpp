#include "Installation.h"

#include "AppCore.h"

namespace tf::app {
namespace {
constexpr auto kOn = "installation.on";
constexpr auto kSchedule = "installation.schedule";
constexpr auto kStart = "installation.start";
constexpr auto kStop = "installation.stop";
constexpr auto kAwake = "installation.keepAwake";
constexpr auto kSession = "installation.session";
constexpr int kPanicRecoverSeconds = 10;
constexpr int kDeviceRetrySeconds = 10;
}

Installation::Installation(AppCore& c) : core(c) {}

Installation::~Installation()
{
    stopTimer();
    if (awakeHeld)
        juce::Desktop::getInstance().setScreenSaverEnabled(true);
}

bool Installation::isEnabled() const { return core.host.getSettings().getBoolValue(kOn, false); }

void Installation::setEnabled(bool on)
{
    core.host.getSettings().setValue(kOn, on);
    core.host.getSettings().saveIfNeeded();
    log(on ? "Installation mode on" : "Installation mode off");
    lastWanted = -1;
    if (on)
        startTimer(1000);
    timerCallback();
}

bool Installation::hasSchedule() const { return core.host.getSettings().getBoolValue(kSchedule, false); }

void Installation::setSchedule(bool on)
{
    core.host.getSettings().setValue(kSchedule, on);
    core.host.getSettings().saveIfNeeded();
    lastWanted = -1;
}

DailyWindow Installation::getWindow() const
{
    DailyWindow w;
    w.start = juce::jlimit(0, DailyWindow::kMinutesPerDay - 1, core.host.getSettings().getIntValue(kStart, w.start));
    w.stop = juce::jlimit(0, DailyWindow::kMinutesPerDay - 1, core.host.getSettings().getIntValue(kStop, w.stop));
    return w;
}

void Installation::setWindow(DailyWindow window)
{
    core.host.getSettings().setValue(kStart, window.start);
    core.host.getSettings().setValue(kStop, window.stop);
    core.host.getSettings().saveIfNeeded();
    lastWanted = -1;
}

bool Installation::getKeepAwake() const { return core.host.getSettings().getBoolValue(kAwake, true); }

void Installation::setKeepAwake(bool on)
{
    core.host.getSettings().setValue(kAwake, on);
    core.host.getSettings().saveIfNeeded();
    timerCallback();
}

juce::File Installation::getSessionFile() const
{
    const auto path = core.host.getSettings().getValue(kSession);
    return path.isNotEmpty() && juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}

void Installation::setSessionFile(const juce::File& file)
{
    core.host.getSettings().setValue(kSession, file.getFullPathName());
    core.host.getSettings().saveIfNeeded();
}

juce::File Installation::getLogFile() const { return core.getRecordingsFolder().getChildFile("Installation log.txt"); }

int Installation::minuteNow()
{
    const auto now = juce::Time::getCurrentTime();
    return now.getHours() * 60 + now.getMinutes();
}

bool Installation::shouldPlay() const { return ! hasSchedule() || getWindow().contains(minuteNow()); }

void Installation::launch()
{
    if (! isEnabled())
        return;
    log("Started");
    if (const auto file = getSessionFile(); file.existsAsFile())
        core.session.openFile(file);
    else if (getSessionFile() != juce::File())
        log("The installation session is missing: " + getSessionFile().getFullPathName());
    pendingFadeIn = shouldPlay();
    lastWanted = shouldPlay() ? 1 : 0;
    startTimer(1000);
}

void Installation::timerCallback()
{
    const bool on = isEnabled();
    const bool holdAwake = on && getKeepAwake();
    if (holdAwake != awakeHeld)
    {
        juce::Desktop::getInstance().setScreenSaverEnabled(! holdAwake);
        awakeHeld = holdAwake;
    }
    if (! on)
    {
        stopTimer();
        pendingFadeIn = false;
        return;
    }

    const auto& frame = core.latest();
    const bool wanted = shouldPlay();
    if (lastWanted >= 0 && static_cast<int>(wanted) != lastWanted)
    {
        log(wanted ? "Schedule: fading in" : "Schedule: fading out");
        pendingFadeIn = wanted;
        if (! wanted)
            core.engine.command(engine::Command::FadeOut);
    }
    lastWanted = wanted ? 1 : 0;

    if (auto* devices = core.host.getDeviceManager())
    {
        if (! core.host.isRunning())
        {
            if (! deviceLost)
            {
                log("The audio device stopped");
                core.status("The audio device stopped. Installation mode will keep trying to reopen it.", true);
                deviceLost = true;
                deviceSeconds = 0;
            }
            if (++deviceSeconds % kDeviceRetrySeconds == 0)
            {
                devices->restartLastAudioDevice();
                if (devices->getCurrentAudioDevice() == nullptr)
                    devices->initialiseWithDefaultDevices(2, 2);
            }
        }
        else if (deviceLost)
        {
            deviceLost = false;
            log("The audio device is back: " + core.host.describeOutput());
            core.status("The audio device is back.");
            pendingFadeIn = wanted;
        }
    }

    if (frame.panicActive)
    {
        if (++panicSeconds == kPanicRecoverSeconds)
        {
            log("Recovered from a panic");
            core.engine.command(engine::Command::ResumeFromPanic);
            pendingFadeIn = wanted;
        }
    }
    else
        panicSeconds = 0;

    if (pendingFadeIn && wanted && ! core.session.isBusy() && core.host.isRunning() && ! frame.panicActive)
    {
        if (frame.fadeState == engine::FadeState::Silent || frame.fadeState == engine::FadeState::FadingOut)
        {
            core.engine.command(engine::Command::FadeIn);
            log("Fading in");
        }
        pendingFadeIn = false;
    }
}

juce::String Installation::describe() const
{
    if (! isEnabled())
        return "Off";
    juce::String text = getSessionFile() != juce::File() ? "Opens " + getSessionFile().getFileNameWithoutExtension() : juce::String("Opens the starter session");
    if (hasSchedule())
    {
        const auto w = getWindow();
        const int until = w.minutesUntilChange(minuteNow());
        text << ". Plays " << formatClock(w.start) << " to " << formatClock(w.stop);
        if (until >= 0)
            text << (shouldPlay() ? ", fades out in " : ", fades in in ") << until / 60 << " h " << until % 60 << " min";
    }
    else
        text << " and plays as soon as it launches";
    if (deviceLost)
        text << ". Waiting for the audio device";
    return text;
}

void Installation::log(const juce::String& message)
{
    const auto file = getLogFile();
    if (! file.getParentDirectory().createDirectory())
        return;
    if (file.getSize() > 4 * 1024 * 1024)
        file.moveFileTo(file.getSiblingFile("Installation log (old).txt"));
    file.appendText(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S  ") + message + "\n", false, false, "\n");
}
}
