#pragma once

#include "SceneSet.h"

#include "../control/Telemetry.h"
#include "../params/ParamRegistry.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::engine {

class Engine;

/** One point on the terrain. Values are sparse: a parameter a scene does not mention
    takes its default, so adding parameters later never breaks saved scenes. */
struct Scene
{
    std::string name;
    Point2 position;
    std::map<ParamIndex, float> values;
};

/** Message-thread owner of the editable terrain. Every edit rebuilds a SceneSet and
    publishes it to the engine. Never touched by the audio thread. */
class SceneManager
{
public:
    explicit SceneManager(Engine& engine);

    const std::vector<Scene>& getScenes() const noexcept { return scenes; }
    int size() const noexcept { return static_cast<int>(scenes.size()); }
    bool isFull() const noexcept { return size() >= kMaxScenes; }

    /** Returns the new index, or -1 if the terrain is full. */
    int addScene(Scene scene);

    /** Captures what is sounding now (every terrain-bound target, live layer
        included) as a new scene at `at`. Clears the live layer, since its values
        now live in the scene. */
    int captureScene(const std::string& name, Point2 at, const TelemetryFrame& now);

    void removeScene(int index);
    void moveScene(int index, Point2 to);
    void renameScene(int index, const std::string& name);
    void setSceneValue(int index, ParamIndex param, float value);

    /** Writes the live layer's values into a scene and hands those parameters back
        to the terrain. */
    void commitLiveLayer(int index, const TelemetryFrame& now);
    void releaseLiveLayer();
    void releaseParam(ParamIndex param);

    /** Pinned parameters ignore the terrain entirely (e.g. live input gain). */
    void setPinned(ParamIndex param, bool pinned);
    bool isPinned(ParamIndex param) const noexcept { return param < pinned.size() && pinned[param]; }

    /** Index of the scene closest to `p`, or -1 when empty. */
    int nearestScene(Point2 p) const noexcept;

    /** A unique default name like "Scene 4". */
    std::string nextSceneName() const;

    void clear();

    /** Rebuilds and publishes. If the engine still holds too many unretired
        snapshots (edits faster than the audio thread consumes them) the terrain is
        marked dirty and tick() publishes the latest state later. Nothing is lost. */
    bool publish();

    /** Call regularly on the message thread (UI timer, harness loop): frees retired
        snapshots and publishes any edit that could not be published yet. */
    void tick();

    bool hasPendingPublish() const noexcept { return dirty; }

    /** Builds the snapshot without publishing (tests, render harness, session save). */
    std::unique_ptr<SceneSet> build() const;

private:
    Engine& engine;
    const ParamRegistry& registry;
    std::vector<Scene> scenes;
    std::vector<bool> pinned;
    std::uint64_t version = 0;
    bool dirty = false;
};

} // namespace tf::engine
