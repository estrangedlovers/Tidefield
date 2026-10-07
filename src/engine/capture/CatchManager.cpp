#include "CatchManager.h"

#include "../Engine.h"

#include <dsp/core/MathUtil.h>

#include <algorithm>
#include <cmath>

namespace tf::engine {

CatchManager::CatchManager(Engine& e) : engine(e) {}

int CatchManager::chooseCloud(int requested) const
{
    if (requested >= 1 && requested <= kNumClouds)
        return requested - 1;

    for (int k = 0; k < kNumClouds; ++k)
        if (engine.getCloudSample(k) == nullptr)
            return k;

    int oldest = 0;
    for (int k = 1; k < kNumClouds; ++k)
        if (lastCaught[static_cast<std::size_t>(k)] < lastCaught[static_cast<std::size_t>(oldest)])
            oldest = k;
    return oldest;
}

bool CatchManager::handle(const EngineNotice& notice)
{
    if (notice.type != EngineNotice::Type::CatchReady)
        return false;

    auto buffer = std::make_shared<dsp::SampleBuffer>();
    if (! engine.copyCatch(notice, *buffer))
    {
        if (onRejected)
            onRejected("Catch arrived too late to copy; try again.");
        return true;
    }

    float peak = 0.0f;
    for (float x : buffer->left)
        peak = std::max(peak, std::fabs(x));
    for (float x : buffer->right)
        peak = std::max(peak, std::fabs(x));
    if (peak < dsp::dbToGain(kSilenceDb))
    {
        if (onRejected)
            onRejected(notice.source == 1 ? "Nothing to catch: the live input is silent." : "Nothing to catch: the output is silent.");
        return true;
    }

    // Normalise and fade the edges so grains never start on a click.
    const float gain = dsp::dbToGain(kTargetPeakDb) / peak;
    const auto n = buffer->size();
    const auto fade = std::min(n / 2, static_cast<std::size_t>(kFadeSeconds * buffer->sampleRate));
    auto shape = [&](std::vector<float>& ch) {
        for (std::size_t i = 0; i < n; ++i)
        {
            float g = gain;
            if (i < fade)
                g *= static_cast<float>(i) / static_cast<float>(fade);
            else if (i >= n - fade)
                g *= static_cast<float>(n - 1 - i) / static_cast<float>(fade);
            ch[i] *= g;
        }
    };
    shape(buffer->left);
    if (buffer->isStereo())
        shape(buffer->right);

    const int cloud = chooseCloud(notice.target);
    ++counter;
    buffer->name = "Catch " + std::to_string(counter) + (notice.source == 1 ? " (input)" : "");
    const auto name = buffer->name;
    if (! engine.loadCloudSample(cloud, std::move(buffer)))
    {
        if (onRejected)
            onRejected("The cloud is still swapping; catch again in a moment.");
        return true;
    }
    lastCaught[static_cast<std::size_t>(cloud)] = counter;
    if (onCaught)
        onCaught(cloud, name);
    return true;
}

} // namespace tf::engine
