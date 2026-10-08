#pragma once

#include <engine/Engine.h>

#include <atomic>
#include <memory>

namespace tf::app {
class LinkSync final
{
public:
    LinkSync();
    ~LinkSync();

    static bool isAvailable() noexcept;

    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }
    int numPeers() const;
    void setTempo(double bpm);

    void prepare(double sampleRate) noexcept { rate = sampleRate; }
    void apply(engine::Engine& engine, int numSamples, double outputLatencySeconds) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<bool> enabled { false };
    double rate = 48000.0;
};
}
