#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tf::app {

/** What runs the engine: the standalone app's own audio device (AudioHost) or a
    DAW (the plugin). Everything above it, the app core and the interface, is the
    same in both. */
class Host
{
public:
    virtual ~Host() = default;

    virtual engine::Engine& getEngine() noexcept = 0;
    /** Per-user settings (recordings folder, the rig's MIDI mapping, ...). */
    virtual juce::PropertiesFile& getSettings() noexcept = 0;
    /** The audio device, when this host owns one (the standalone app); null in a DAW. */
    virtual juce::AudioDeviceManager* getDeviceManager() noexcept { return nullptr; }
    /** 0..1 share of the audio deadline, when the host measures it. */
    virtual double getCpuLoad() const { return 0.0; }
    /** True while audio is being processed (something to record). */
    virtual bool isRunning() const = 0;
    /** Short description of where the sound goes, for the top bar. */
    virtual juce::String describeOutput() const = 0;
    /** Running inside a DAW: leave transport keys and the audio device to the host. */
    virtual bool isPlugin() const noexcept { return false; }
};

} // namespace tf::app
