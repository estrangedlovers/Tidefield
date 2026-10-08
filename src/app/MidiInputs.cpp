#include "MidiInputs.h"

namespace tf::app {
namespace {
constexpr auto kDisabledKey = "midiDisabledInputs";
}

MidiInputs::MidiInputs(engine::Engine& e, juce::PropertiesFile& s) : engine(e), settings(s)
{
    connection = juce::MidiDeviceListConnection::make([this] { refresh(); });
    refresh();
}

MidiInputs::~MidiInputs()
{
    for (auto& in : inputs)
        if (in.input != nullptr)
            in.input->stop();
}

void MidiInputs::refresh()
{
    const auto disabled = juce::StringArray::fromTokens(settings.getValue(kDisabledKey), "\n", {});

    for (auto& in : inputs)
    {
        if (in.input != nullptr)
            in.input->stop();
        in.input.reset();
        in.callback.reset();
    }

    devices.clear();
    int nextPort = 0;
    for (const auto& info : juce::MidiInput::getAvailableDevices())
    {
        DeviceState d;
        d.info = info;
        d.enabled = ! disabled.contains(info.identifier);
        if (d.enabled && nextPort < engine::kMaxMidiPorts)
        {
            auto& slot = inputs[static_cast<std::size_t>(nextPort)];
            slot.callback = std::make_unique<PortCallback>(engine, nextPort);
            slot.input = juce::MidiInput::openDevice(info.identifier, slot.callback.get());
            if (slot.input != nullptr)
            {
                d.port = nextPort++;
                d.open = true;
                slot.input->start();
            }
            else
            {
                slot.callback.reset();
            }
        }
        devices.push_back(d);
    }
    if (onDevicesChanged)
        onDevicesChanged();
}

void MidiInputs::setEnabled(const juce::String& identifier, bool enabled)
{
    auto disabled = juce::StringArray::fromTokens(settings.getValue(kDisabledKey), "\n", {});
    disabled.removeString(identifier);
    if (! enabled)
        disabled.add(identifier);
    settings.setValue(kDisabledKey, disabled.joinIntoString("\n"));
    settings.saveIfNeeded();
    refresh();
}

void MidiInputs::PortCallback::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.getRawDataSize() > 3 || message.isSysEx() || message.isMidiClock() || message.isActiveSense())
        return;
    const auto* raw = message.getRawData();
    engine::RawMidi m;
    m.status = raw[0];
    m.data1 = message.getRawDataSize() > 1 ? raw[1] : 0;
    m.data2 = message.getRawDataSize() > 2 ? raw[2] : 0;
    engine.postMidi(port, m);
}
}
