#pragma once

#include <engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <cstdint>

namespace tf::app {
class BlockClock
{
public:
    void mark(std::uint64_t sampleTime, double milliseconds, double sampleRate, int numSamples) noexcept
    {
        sequence.fetch_add(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        sample.store(sampleTime, std::memory_order_relaxed);
        ms.store(milliseconds, std::memory_order_relaxed);
        rate.store(sampleRate, std::memory_order_relaxed);
        blockMs.store(sampleRate > 0.0 ? 1000.0 * numSamples / sampleRate : 0.0, std::memory_order_relaxed);
        sequence.fetch_add(1, std::memory_order_release);
    }

    struct Reading
    {
        std::uint64_t sampleTime = 0;
        double ms = 0.0, sampleRate = 0.0, blockMs = 0.0;
    };

    bool read(Reading& out) const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto before = sequence.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;
            out.sampleTime = sample.load(std::memory_order_relaxed);
            out.ms = ms.load(std::memory_order_relaxed);
            out.sampleRate = rate.load(std::memory_order_relaxed);
            out.blockMs = blockMs.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence.load(std::memory_order_relaxed) == before)
                return out.sampleRate > 0.0;
        }
        return false;
    }

private:
    std::atomic<std::uint32_t> sequence { 0 };
    std::atomic<std::uint64_t> sample { 0 };
    std::atomic<double> ms { 0.0 }, rate { 0.0 }, blockMs { 0.0 };
};

class Host
{
public:
    virtual ~Host() = default;

    virtual engine::Engine& getEngine() noexcept = 0;
    virtual juce::PropertiesFile& getSettings() noexcept = 0;
    virtual juce::AudioDeviceManager* getDeviceManager() noexcept { return nullptr; }
    virtual double getCpuLoad() const { return 0.0; }
    virtual bool isRunning() const = 0;
    virtual juce::String describeOutput() const = 0;
    virtual bool isPlugin() const noexcept { return false; }
    virtual class LinkSync* getLink() noexcept { return nullptr; }
    virtual const BlockClock* getBlockClock() const noexcept { return nullptr; }
};
}
