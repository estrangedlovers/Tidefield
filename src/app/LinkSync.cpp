#include "LinkSync.h"

#if TIDEFIELD_WITH_LINK
#include <ableton/Link.hpp>
#endif

namespace tf::app {
#if TIDEFIELD_WITH_LINK
struct LinkSync::Impl
{
    ableton::Link link { 90.0 };
};

LinkSync::LinkSync() : impl(std::make_unique<Impl>()) {}
LinkSync::~LinkSync() { impl->link.enable(false); }
bool LinkSync::isAvailable() noexcept { return true; }

void LinkSync::setEnabled(bool on)
{
    impl->link.enable(on);
    enabled.store(on, std::memory_order_relaxed);
}

int LinkSync::numPeers() const { return static_cast<int>(impl->link.numPeers()); }

void LinkSync::setTempo(double bpm)
{
    auto state = impl->link.captureAppSessionState();
    state.setTempo(bpm, impl->link.clock().micros());
    impl->link.commitAppSessionState(state);
}

void LinkSync::apply(engine::Engine& engine, int, double outputLatencySeconds) noexcept
{
    if (! enabled.load(std::memory_order_relaxed))
        return;
    const auto state = impl->link.captureAudioSessionState();
    const auto at = impl->link.clock().micros() + std::chrono::microseconds(static_cast<long long>(outputLatencySeconds * 1.0e6));
    engine.setHostTransport(state.tempo(), state.beatAtTime(at, 4.0), true);
}
#else
struct LinkSync::Impl
{
};

LinkSync::LinkSync() = default;
LinkSync::~LinkSync() = default;
bool LinkSync::isAvailable() noexcept { return false; }
void LinkSync::setEnabled(bool) {}
int LinkSync::numPeers() const { return 0; }
void LinkSync::setTempo(double) {}
void LinkSync::apply(engine::Engine&, int, double) noexcept {}
#endif
}
