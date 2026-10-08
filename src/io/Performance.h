#pragma once

#include "Session.h"

#include <engine/perform/Gesture.h>

#include <atomic>
#include <functional>
#include <set>
#include <vector>

namespace tf::io {
struct Performance
{
    enum class LaneKind : std::uint8_t { Param, Notes, Actions };

    struct Lane
    {
        LaneKind kind = LaneKind::Param;
        engine::ParamIndex param = 0;
        bool operator==(const Lane& o) const noexcept { return kind == o.kind && (kind != LaneKind::Param || param == o.param); }
        bool operator<(const Lane& o) const noexcept { return kind != o.kind ? kind < o.kind : (kind == LaneKind::Param && param < o.param); }
    };

    SessionData start;
    std::vector<engine::GestureEvent> events;
    std::uint64_t length = 0;
    double sampleRate = 48000.0;
    bool startedOpen = true;
    std::set<Lane> muted;

    bool empty() const noexcept { return length == 0; }
    double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double>(length) / sampleRate : 0.0; }
    static Lane laneOf(const engine::ControlEvent& e) noexcept;
    std::vector<Lane> lanes() const;
    bool isMuted(const Lane& lane) const { return muted.count(lane) != 0; }
    void setMuted(const Lane& lane, bool mute);

    void erase(double fromSeconds, double toSeconds, const Lane* onlyLane = nullptr);
    void smooth(const Lane& lane, double windowSeconds);
    void trim(double fromSeconds, double toSeconds);
    std::vector<engine::GestureEvent> playable() const;
    std::uint64_t toSamples(double seconds) const noexcept;
};

juce::var performanceToJson(const Performance& p, const engine::ParamRegistry& registry);
std::optional<Performance> performanceFromSession(const SessionData& session, const engine::ParamRegistry& registry);

struct RenderOptions
{
    juce::File folder;
    bool stems = false;
    double sampleRate = 48000.0;
    double loopCrossfadeSeconds = 0.0;
    std::function<void(float progress)> onProgress;
    const std::atomic<bool>* cancel = nullptr;
};

struct RenderResult
{
    bool ok = false;
    juce::String error;
    std::vector<std::string> warnings;
    juce::File master;
};

RenderResult renderPerformance(const Performance& performance, const RenderOptions& options);
}
