#pragma once

#include "Host.h"

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_osc/juce_osc.h>

#include <array>
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

class CycleMidiOut final : private juce::HighResolutionTimer
{
public:
    static constexpr int kMaxScheduled = 2048;

    CycleMidiOut(engine::Engine& engine, juce::PropertiesFile& settings, const BlockClock* clock);
    ~CycleMidiOut() override;

    juce::String getDeviceId() const { return deviceId; }
    bool isOpen() const noexcept { return output != nullptr; }
    void setDevice(const juce::String& identifier);
    void allNotesOff() noexcept { wantAllOff.store(true, std::memory_order_release); }
    int getSounding() const noexcept { return soundingCount.load(std::memory_order_relaxed); }

private:
    struct Scheduled
    {
        double due = 0.0;
        std::uint8_t status = 0, data1 = 0, data2 = 0;
    };

    void hiResTimerCallback() override;
    void send(const Scheduled& s);
    void sendAllOff();
    double dueTime(const engine::MidiOutEvent& e, double now) const;

    engine::Engine& engine;
    juce::PropertiesFile& settings;
    const BlockClock* clock;
    juce::String deviceId;
    std::unique_ptr<juce::MidiOutput> output;
    std::array<Scheduled, kMaxScheduled> scheduled {};
    int head = 0, count = 0;
    std::array<std::array<std::uint8_t, 128>, 16> sounding {};
    std::atomic<bool> wantAllOff { false };
    std::atomic<int> soundingCount { 0 };
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
