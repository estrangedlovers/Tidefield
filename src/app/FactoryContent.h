#pragma once

#include <dsp/core/SampleBuffer.h>
#include <engine/Engine.h>
#include <io/Session.h>

#include <memory>
#include <vector>

namespace tf::app {

/** A sound compiled into the app. Tonal sounds carry the note they sound at, so
    loading one into Bloom tunes it; textures have rootNote < 0. */
struct FactorySound
{
    const char* name;
    const char* category; // "Tonal", "Pad", "Texture"
    const char* resource; // BinaryData resource name
    int rootNote;
};

const std::vector<FactorySound>& factorySounds();

/** Decodes a factory sound (message thread or worker); null if it is missing. */
std::shared_ptr<const dsp::SampleBuffer> loadFactorySound(const FactorySound& sound);

/** What a new session starts as: the defaults, factory sounds in Cloud 1 and Bloom,
    and a terrain of starter scenes so the surface plays from the first touch. */
io::SessionData makeStarterSession(const engine::Engine& engine);

} // namespace tf::app
