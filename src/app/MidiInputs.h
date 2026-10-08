#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <memory>
#include <vector>

namespace tf::app {
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

    std::function<void()> onDevicesChanged;

private:
    struct PortCallback final : juce::MidiInputCallback
    {
        PortCallback(engine::Engine& e, int p) : engine(e), port(p) {}
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
        engine::Engine& engine;
        const int port;
    };

    struct OpenInput
    {
        std::unique_ptr<PortCallback> callback;
        std::unique_ptr<juce::MidiInput> input;
    };

    engine::Engine& engine;
    juce::PropertiesFile& settings;
    std::vector<DeviceState> devices;
    std::array<OpenInput, engine::kMaxMidiPorts> inputs;
    juce::MidiDeviceListConnection connection;
};
}
