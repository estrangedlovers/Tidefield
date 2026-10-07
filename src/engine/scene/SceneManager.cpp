#include "SceneManager.h"

#include "../Engine.h"

#include <algorithm>
#include <cmath>

namespace tf::engine {

SceneManager::SceneManager(Engine& e) : engine(e), registry(e.getRegistry())
{
    pinned.assign(registry.size(), false);
}

int SceneManager::addScene(Scene scene)
{
    if (isFull())
        return -1;
    scene.position.x = std::clamp(scene.position.x, 0.0f, 1.0f);
    scene.position.y = std::clamp(scene.position.y, 0.0f, 1.0f);
    for (auto& [param, value] : scene.values)
        value = registry.spec(param).clamp(value);
    scenes.push_back(std::move(scene));
    publish();
    return size() - 1;
}

int SceneManager::captureScene(const std::string& name, Point2 at, const TelemetryFrame& now)
{
    Scene s;
    s.name = name.empty() ? nextSceneName() : name;
    s.position = at;
    for (std::size_t i = 0; i < registry.size(); ++i)
        if ((registry.spec(static_cast<ParamIndex>(i)).flags & ParamFlag::kTerrainBound) != 0)
            s.values[static_cast<ParamIndex>(i)] = now.paramTargets[i];
    const int index = addScene(std::move(s));
    if (index >= 0)
        releaseLiveLayer();
    return index;
}

void SceneManager::removeScene(int index)
{
    if (index < 0 || index >= size())
        return;
    scenes.erase(scenes.begin() + index);
    publish();
}

void SceneManager::moveScene(int index, Point2 to)
{
    if (index < 0 || index >= size())
        return;
    scenes[static_cast<std::size_t>(index)].position = { std::clamp(to.x, 0.0f, 1.0f), std::clamp(to.y, 0.0f, 1.0f) };
    publish();
}

void SceneManager::renameScene(int index, const std::string& name)
{
    if (index >= 0 && index < size())
        scenes[static_cast<std::size_t>(index)].name = name; // names do not affect sound
}

void SceneManager::setSceneValue(int index, ParamIndex param, float value)
{
    if (index < 0 || index >= size() || param >= registry.size())
        return;
    scenes[static_cast<std::size_t>(index)].values[param] = registry.spec(param).clamp(value);
    publish();
}

void SceneManager::commitLiveLayer(int index, const TelemetryFrame& now)
{
    if (index < 0 || index >= size())
        return;
    auto& scene = scenes[static_cast<std::size_t>(index)];
    for (std::size_t i = 0; i < registry.size(); ++i)
        if (now.live[i] != 0)
            scene.values[static_cast<ParamIndex>(i)] = now.paramTargets[i];
    publish();
    releaseLiveLayer();
}

void SceneManager::releaseLiveLayer() { engine.command(Command::ReleaseLiveLayer); }

void SceneManager::releaseParam(ParamIndex param) { engine.post(ControlEvent::releaseParam(param)); }

void SceneManager::setPinned(ParamIndex param, bool shouldPin)
{
    if (param >= pinned.size() || pinned[param] == shouldPin)
        return;
    pinned[param] = shouldPin;
    publish();
}

int SceneManager::nearestScene(Point2 p) const noexcept
{
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < size(); ++i)
    {
        const auto& q = scenes[static_cast<std::size_t>(i)].position;
        const float d = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

std::string SceneManager::nextSceneName() const
{
    for (int n = 1;; ++n)
    {
        const auto candidate = "Scene " + std::to_string(n);
        if (std::none_of(scenes.begin(), scenes.end(), [&](const Scene& s) { return s.name == candidate; }))
            return candidate;
    }
}

void SceneManager::clear()
{
    scenes.clear();
    publish();
}

std::unique_ptr<SceneSet> SceneManager::build() const
{
    auto set = std::make_unique<SceneSet>();
    set->version = version;
    set->numScenes = size();

    for (std::size_t i = 0; i < registry.size(); ++i)
    {
        const auto& spec = registry.spec(static_cast<ParamIndex>(i));
        if ((spec.flags & ParamFlag::kTerrainBound) == 0 || pinned[i])
            continue;
        SceneSet::Column c;
        c.param = static_cast<ParamIndex>(i);
        if ((spec.flags & ParamFlag::kDiscrete) != 0)
            c.blend = SceneSet::Blend::Discrete;
        else if (spec.taper == Taper::Log && spec.minValue > 0.0f)
            c.blend = SceneSet::Blend::Log;
        set->columns.push_back(c);
    }

    set->values.reserve(scenes.size() * set->columns.size());
    for (std::size_t s = 0; s < scenes.size(); ++s)
    {
        set->positions[s] = scenes[s].position;
        for (const auto& c : set->columns)
        {
            const auto it = scenes[s].values.find(c.param);
            const float v = it != scenes[s].values.end() ? it->second : registry.spec(c.param).defaultValue;
            set->values.push_back(c.blend == SceneSet::Blend::Log ? std::log(v) : v);
        }
    }
    return set;
}

bool SceneManager::publish()
{
    ++version;
    dirty = ! engine.publishScenes(build());
    return ! dirty;
}

void SceneManager::tick()
{
    engine.collectGarbage();
    if (dirty)
        publish();
}

} // namespace tf::engine
