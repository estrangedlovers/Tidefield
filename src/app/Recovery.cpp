#include "Recovery.h"

#include "AppCore.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tf::app {
namespace {
constexpr auto kOn = "autosave.on";
constexpr auto kMinutes = "autosave.minutes";
}

Recovery::Recovery(AppCore& c) : core(c)
{
    crashed = getMarkerFile().existsAsFile() && getRecoveryFile().existsAsFile();
    if (! crashed)
        getRecoveryFile().deleteFile();
    writeMarker(core.session.getFile());
    startTimer(1000);
}

Recovery::~Recovery()
{
    *alive = false;
    stopTimer();
    finish();
}

bool Recovery::isEnabled() const { return core.host.getSettings().getBoolValue(kOn, true); }

void Recovery::setEnabled(bool on)
{
    core.host.getSettings().setValue(kOn, on);
    core.host.getSettings().saveIfNeeded();
    seconds = 0;
}

int Recovery::getMinutes() const
{
    const int stored = core.host.getSettings().getIntValue(kMinutes, 2);
    for (const int m : kIntervals)
        if (m == stored)
            return m;
    return 2;
}

void Recovery::setMinutes(int minutes)
{
    core.host.getSettings().setValue(kMinutes, minutes);
    core.host.getSettings().saveIfNeeded();
    seconds = 0;
}

juce::File Recovery::getRecoveryFile() const { return core.host.getSettings().getFile().getSiblingFile("Recovery.tide"); }

juce::File Recovery::getMarkerFile() const { return core.host.getSettings().getFile().getSiblingFile("Running.txt"); }

void Recovery::writeMarker(const juce::File& original)
{
    if (finished)
        return;
    const auto marker = getMarkerFile();
    marker.getParentDirectory().createDirectory();
    marker.replaceWithText(original.getFullPathName());
}

juce::File Recovery::originalFromMarker() const
{
    const auto path = getMarkerFile().loadFileAsString().trim();
    return path.isNotEmpty() && juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}

void Recovery::offerRestore()
{
    if (! crashed)
        return;
    if (core.installation != nullptr && core.installation->isEnabled())
    {
        crashed = false;
        return;
    }
    const auto original = originalFromMarker();
    const auto recovered = getRecoveryFile();
    const auto when = recovered.getLastModificationTime().formatted("%H:%M on %d %b");
    const juce::String name = original != juce::File() ? original.getFileNameWithoutExtension() : juce::String("an untitled session");
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Tidefield did not quit normally")
                       .withMessage("Tidefield kept a copy of " + name + " from " + when + ". Restore it?")
                       .withButton("Restore")
                       .withButton("Discard")
                       .withButton("Keep for later");
    juce::AlertWindow::showAsync(options, [this, token = std::weak_ptr<bool>(alive), recovered, original, name](int result) {
        if (token.expired())
            return;
        crashed = false;
        if (result == 1)
        {
            core.session.openRecovered(recovered, original);
            core.status("Restored the copy Tidefield kept before it stopped. Save it to keep it.");
            return;
        }
        if (result == 0)
        {
            const auto folder = core.getRecordingsFolder();
            folder.createDirectory();
            const auto kept = folder.getNonexistentChildFile("Recovered " + name + juce::Time::getCurrentTime().formatted(" %Y-%m-%d %H%M"),
                                                             io::kSessionExtension, false);
            if (recovered.copyFileTo(kept))
                core.status("The recovered copy is in " + folder.getFileName() + ": " + kept.getFileName());
            else
                core.status("Could not keep the recovered copy.", true);
        }
        recovered.deleteFile();
    });
}

void Recovery::saveNow()
{
    if (finished || crashed || core.session.isBusy())
        return;
    const auto original = core.session.getFile();
    core.session.autosaveTo(getRecoveryFile(), [this, token = std::weak_ptr<bool>(alive), original](bool ok) {
        if (token.expired() || ! ok)
            return;
        lastSaved = juce::Time::getCurrentTime();
        writeMarker(original);
    });
}

void Recovery::finish()
{
    if (finished)
        return;
    finished = true;
    stopTimer();
    if (! crashed)
        getRecoveryFile().deleteFile();
    getMarkerFile().deleteFile();
}

juce::String Recovery::describe() const
{
    if (! isEnabled())
        return "Off. If Tidefield stops unexpectedly, unsaved changes are lost.";
    if (lastSaved == juce::Time())
        return "Keeps a copy every " + juce::String(getMinutes()) + " min. Nothing kept yet.";
    return "Keeps a copy every " + juce::String(getMinutes()) + " min. Last kept at " + lastSaved.formatted("%H:%M:%S") + ".";
}

void Recovery::timerCallback()
{
    if (! isEnabled())
    {
        seconds = 0;
        return;
    }
    if (++seconds >= getMinutes() * 60)
    {
        seconds = 0;
        saveNow();
    }
}
}
