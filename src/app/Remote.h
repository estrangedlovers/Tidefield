#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_osc/juce_osc.h>

#include <atomic>
#include <functional>
#include <memory>

namespace tf::app {
class AppCore;

class MidiClockOut final : private juce::HighResolutionTimer
{
public:
    explicit MidiClockOut(juce::PropertiesFile& settings);
    ~MidiClockOut() override;

    juce::String getDeviceId() const { return deviceId; }
    void setDevice(const juce::String& identifier);
    void update(float bpm, bool running);

private:
    void hiResTimerCallback() override;
    void open();

    juce::PropertiesFile& settings;
    juce::String deviceId;
    std::unique_ptr<juce::MidiOutput> output;
    std::atomic<float> tempo { 90.0f };
    std::atomic<bool> wantRunning { false };
    bool sentRunning = false;
    double nextTick = 0.0;
};

class OscRemote final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    OscRemote(AppCore& core, juce::PropertiesFile& settings);
    ~OscRemote() override;

    int getReceivePort() const noexcept { return receivePort; }
    bool isReceiving() const noexcept { return receiving; }
    void setReceivePort(int port);

    juce::String getSendTarget() const { return sendHost + ":" + juce::String(sendPort); }
    bool isSending() const noexcept { return sending; }
    void setSendTarget(const juce::String& hostAndPort);
    void stopSending();

    void sendFrame(const engine::TelemetryFrame& frame);

    std::function<void(int scene, bool jump)> onScene;

private:
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;
    void handle(const juce::OSCMessage& message);
    float number(const juce::OSCMessage& m, int index, float fallback = 0.0f) const;

    AppCore& core;
    juce::PropertiesFile& settings;
    juce::OSCReceiver receiver;
    juce::OSCSender sender;
    int receivePort = 0;
    bool receiving = false;
    juce::String sendHost = "127.0.0.1";
    int sendPort = 9001;
    bool sending = false;
    int frameCounter = 0;
};
}
