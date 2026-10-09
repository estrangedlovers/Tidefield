#pragma once

#include <dsp/core/SampleBuffer.h>
#include <engine/control/Telemetry.h>
#include <engine/params/ParamRegistry.h>

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::engine {
class Engine;
class FxManager;
class MidiManager;
class PathManager;
class GestureManager;
class SceneManager;
class SeasonManager;
class ModRouteManager;
}

namespace tf::io {
struct SessionData
{
    static constexpr int kCurrentVersion = 1;

    struct SceneData
    {
        std::string name;
        float x = 0.5f, y = 0.5f;
        std::map<std::string, float> values;
    };

    int version = kCurrentVersion;
    std::string name;
    std::map<std::string, float> params;
    std::vector<SceneData> scenes;
    std::vector<std::string> pins;
    std::map<std::string, std::string> fx;
    std::map<std::string, std::string> fxState;
    juce::var midi;
    juce::var seasons;
    juce::var modRoutes;
    juce::var macros;
    std::vector<float> bloomRoots;
    juce::var performance;
    std::vector<engine::Point2> path;
    juce::var gesture;
    std::map<std::string, std::shared_ptr<const dsp::SampleBuffer>> samples;

    std::vector<std::string> warnings;
};

SessionData captureSession(const engine::Engine& engine, const engine::TelemetryFrame& latest, const engine::SceneManager& scenes,
                           const engine::FxManager& fx, const engine::MidiManager* midi = nullptr,
                           const engine::SeasonManager* seasons = nullptr, const engine::PathManager* path = nullptr,
                           const engine::GestureManager* gestures = nullptr, const engine::ModRouteManager* mod = nullptr);

std::vector<std::string> applySession(const SessionData& session, engine::Engine& engine, engine::SceneManager& scenes,
                                      engine::FxManager& fx, bool snap, engine::MidiManager* midi = nullptr,
                                      engine::SeasonManager* seasons = nullptr, engine::PathManager* path = nullptr,
                                      engine::GestureManager* gestures = nullptr, engine::ModRouteManager* mod = nullptr);

juce::var midiToJson(const engine::MidiManager& midi, const engine::ParamRegistry& registry);
std::vector<std::string> applyMidiJson(const juce::var& json, engine::MidiManager& midi, const engine::ParamRegistry& registry);

juce::var seasonsToJson(const engine::SeasonManager& seasons, const engine::ParamRegistry& registry);
std::vector<std::string> applySeasonsJson(const juce::var& json, engine::SeasonManager& seasons, const engine::ParamRegistry& registry);

juce::var modRoutesToJson(const engine::ModRouteManager& mod, const engine::ParamRegistry& registry);
std::vector<std::string> applyModRoutesJson(const juce::var& json, engine::ModRouteManager& mod, const engine::ParamRegistry& registry);

juce::var macrosToJson(const engine::ModRouteManager& mod, const engine::ParamRegistry& registry);
std::vector<std::string> applyMacrosJson(const juce::var& json, engine::ModRouteManager& mod, const engine::ParamRegistry& registry);

juce::var gestureToJson(const engine::GestureTake& take, const engine::ParamRegistry& registry);
std::vector<std::string> applyGestureJson(const juce::var& json, engine::GestureManager& gestures, const engine::ParamRegistry& registry);

SessionData defaultSession(const engine::Engine& engine);

bool saveSession(const SessionData& session, const juce::File& file, juce::String& error);
std::optional<SessionData> loadSession(const juce::File& file, juce::String& error);

bool writeSession(const SessionData& session, juce::OutputStream& out, juce::String& error);
std::optional<SessionData> readSession(const void* data, std::size_t size, juce::String& error);

juce::var sessionToJson(const SessionData& session);
bool sameContent(const SessionData& a, const SessionData& b);
std::optional<SessionData> sessionFromJson(const juce::var& json, juce::String& error);

inline constexpr const char* kSessionExtension = ".tide";
inline constexpr const char* kLegacySessionExtension = ".tidefield";
inline constexpr const char* kSessionWildcard = "*.tide;*.tidefield";

inline bool isSessionFile(const juce::File& file)
{
    return file.hasFileExtension(kSessionExtension) || file.hasFileExtension(kLegacySessionExtension);
}
}
