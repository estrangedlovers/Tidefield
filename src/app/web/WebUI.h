#pragma once

#include "../AppCore.h"

#include <io/UiProtocol.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <optional>

namespace tf::app {

/** The React front end in a WebView. Everything crosses one native function,
    `tidefield(method, ...args)`, and a handful of events pushed to the page:

      telemetry  ~30 Hz, parameters as deltas (io::TelemetryEncoder)
      scenes, fx, samples, midi, session   when they change
      status     short messages (Catch results, MIDI learned, warnings)

    The page is served from the built UI folder through a resource provider, or from
    a Vite dev server when TIDEFIELD_UI_DEV is set (hot reload). */
class WebUI final : public juce::Component, private juce::Timer
{
public:
    /** Where the built UI lives, if anywhere (bundle Resources/ui, next to the
        executable, or the source tree's ui/dist in development builds). */
    static std::optional<juce::File> findUiRoot();
    static juce::String devServerUrl();

    WebUI(AppCore& core, juce::File uiRoot);
    ~WebUI() override;

    void resized() override;

private:
    void timerCallback() override;
    juce::var call(const juce::String& method, const juce::Array<juce::var>& args);
    juce::var hello();
    juce::var describeMidi() const;
    juce::var describeSession() const;
    juce::var describeRecording() const;
    void chooseRecordingsFolder();
    void pushIfChanged(const juce::String& event, const juce::var& value, juce::String& last);
    void emit(const juce::String& event, const juce::var& value);
    std::optional<juce::WebBrowserComponent::Resource> serve(const juce::String& path) const;
    void chooseSample(int slot);
    void showAudioSettings();

    AppCore& core;
    juce::File root;
    io::TelemetryEncoder encoder;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastScenes, lastFx, lastSamples, lastMidi, lastSession, lastRecord;
    juce::String lastActivity;
    bool pageReady = false;
    int slowTick = 0;
};

} // namespace tf::app
