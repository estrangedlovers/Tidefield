#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <memory>
#include <vector>

namespace tf::app {

/** Opens MIDI input devices and forwards their messages to the engine. Each open
    device gets its own engine port (and so its own single-producer queue), because a
    platform may deliver different devices on different threads. Follows hot-plugging.
    Enabled/disabled choices persist in the app settings. */
class MidiInputs final
{
public:
    MidiInputs(engine::Engine& engine, juce::PropertiesFile& settings);
    ~MidiInputs();

    struct DeviceState
    {
        juce::MidiDeviceInfo info;
        bool enabled = true;
        bool open = false;
        int port = -1;
    };

    const std::vector<DeviceState>& getDevices() const noexcept { return devices; }
    void setEnabled(const juce::String& identifier, bool enabled);
    void refresh();

    /** Called on the message thread when devices appear or vanish. */
    std::function<void()> onDevicesChanged;

private:
    /** One per open device: knows its engine port, so the MIDI thread never looks
        anything up in state the message thread may be changing. */
    struct PortCallback final : juce::MidiInputCallback
    {
        PortCallback(engine::Engine& e, int p) : engine(e), port(p) {}
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
        engine::Engine& engine;
        const int port;
    };

    struct OpenInput
    {
        std::unique_ptr<PortCallback> callback; // destroyed after the input (declared first)
        std::unique_ptr<juce::MidiInput> input;
    };

    engine::Engine& engine;
    juce::PropertiesFile& settings;
    std::vector<DeviceState> devices;
    std::array<OpenInput, engine::kMaxMidiPorts> inputs;
    juce::MidiDeviceListConnection connection;
};

} // namespace tf::app
