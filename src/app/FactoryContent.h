#pragma once

#include <dsp/core/SampleBuffer.h>
#include <engine/Engine.h>
#include <io/Presets.h>
#include <io/Session.h>

#include <memory>
#include <vector>

namespace tf::app {
struct FactorySound
{
    const char* name;
    const char* category;
    const char* resource;
    int rootNote;
};

const std::vector<FactorySound>& factorySounds();

std::shared_ptr<const dsp::SampleBuffer> loadFactorySound(const FactorySound& sound);

io::SessionData makeStarterSession(const engine::Engine& engine);

std::string presetPrefix(const std::string& kind);

void addFactoryPresets(io::PresetLibrary& library);
}
