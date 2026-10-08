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

struct Scene
{
    std::string name;
    Point2 position;
    std::map<ParamIndex, float> values;
};

class SceneManager
{
public:
    explicit SceneManager(Engine& engine);

    const std::vector<Scene>& getScenes() const noexcept { return scenes; }
    int size() const noexcept { return static_cast<int>(scenes.size()); }
    bool isFull() const noexcept { return size() >= kMaxScenes; }

    int addScene(Scene scene);

    int captureScene(const std::string& name, Point2 at, const TelemetryFrame& now);

    void removeScene(int index);
    void moveScene(int index, Point2 to);
    void renameScene(int index, const std::string& name);
    void setSceneValue(int index, ParamIndex param, float value);

    void commitLiveLayer(int index, const TelemetryFrame& now);
    void releaseLiveLayer();
    void releaseParam(ParamIndex param);

    void setPinned(ParamIndex param, bool pinned);
    bool isPinned(ParamIndex param) const noexcept { return param < pinned.size() && pinned[param]; }

    int nearestScene(Point2 p) const noexcept;

    std::string nextSceneName() const;

    void clear();

    void replaceAll(std::vector<Scene> newScenes, const std::vector<ParamIndex>& newPins);

    std::vector<ParamIndex> getPins() const;

    bool publish();

    void tick();

    bool hasPendingPublish() const noexcept { return dirty; }

    std::uint64_t getVersion() const noexcept { return version; }

    std::unique_ptr<SceneSet> build() const;

private:
    Engine& engine;
    const ParamRegistry& registry;
    std::vector<Scene> scenes;
    std::vector<bool> pinned;
    std::uint64_t version = 0;
    bool dirty = false;
};
}
