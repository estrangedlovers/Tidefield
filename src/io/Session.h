#pragma once

#include <dsp/core/SampleBuffer.h>
#include <engine/control/Telemetry.h>

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::engine {
class Engine;
class FxManager;
class SceneManager;
} // namespace tf::engine

namespace tf::io {

/** Everything a performance needs, independent of any engine instance. Parameter
    and slot references are stored by their stable string IDs, so a session survives
    parameters being added or reordered. */
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
    std::map<std::string, std::string> fx;   // slot id -> processor type ("" = empty)
    juce::var midi;                          // MIDI mappings (phase 5)
    std::map<std::string, std::shared_ptr<const dsp::SampleBuffer>> samples; // "cloud1".."cloud4", "bloom"

    /** Things recall could not apply (unknown IDs from a newer version, etc.). */
    std::vector<std::string> warnings;
};

/** Message thread: snapshot the running state. `latest` provides parameter targets. */
SessionData captureSession(const engine::Engine& engine, const engine::TelemetryFrame& latest, const engine::SceneManager& scenes,
                           const engine::FxManager& fx);

/** Message thread: apply a session. With `snap` true, parameters jump (use while the
    master is faded out); otherwise they glide through their smoothers. The live layer
    is released. Returns warnings. */
std::vector<std::string> applySession(const SessionData& session, engine::Engine& engine, engine::SceneManager& scenes,
                                      engine::FxManager& fx, bool snap);

/** The default state: every parameter at its default, no scenes, default FX, no samples. */
SessionData defaultSession(const engine::Engine& engine);

/** Any thread: write / read a .tidefield file (a zip of session.json + FLAC audio).
    Saving writes to a temporary file first and swaps it in, so a crash mid-save never
    destroys the previous file. */
bool saveSession(const SessionData& session, const juce::File& file, juce::String& error);
std::optional<SessionData> loadSession(const juce::File& file, juce::String& error);

/** JSON form (no audio), exposed for tests and tooling. */
juce::var sessionToJson(const SessionData& session);
std::optional<SessionData> sessionFromJson(const juce::var& json, juce::String& error);

inline constexpr const char* kSessionExtension = ".tidefield";

} // namespace tf::io
