#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace tf::tools {
namespace {
float toDb(double linear) { return linear <= 1.0e-6 ? -120.0f : static_cast<float>(20.0 * std::log10(linear)); }
}

Analysis Analysis::run(const std::vector<std::vector<float>>& channels, double sampleRate, float ceilingDb)
{
    Analysis a;
    if (channels.empty() || channels[0].empty())
        return a;

    const auto numSamples = channels[0].size();
    const auto perSecond = static_cast<std::size_t>(sampleRate);
    const float ceiling = std::pow(10.0f, ceilingDb / 20.0f) + 1.0e-6f;
    const float silence = std::pow(10.0f, -90.0f / 20.0f);

    a.seconds = static_cast<double>(numSamples) / sampleRate;
    double peak = 0.0, sumSq = 0.0, secondSum = 0.0;
    std::size_t silentRun = 0, longestSilentRun = 0, secondCount = 0;

    for (const auto& ch : channels)
    {
        double dc = 0.0;
        for (float x : ch)
            dc += static_cast<double>(x);
        a.dcOffset = std::max(a.dcOffset, static_cast<float>(std::fabs(dc / static_cast<double>(ch.size()))));
    }

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        double frameSq = 0.0;
        float framePeak = 0.0f;
        for (const auto& ch : channels)
        {
            const float x = ch[i];
            if (! std::isfinite(x))
            {
                ++a.nonFiniteSamples;
                continue;
            }
            const float ax = std::fabs(x);
            framePeak = std::max(framePeak, ax);
            if (ax > ceiling)
                ++a.samplesAboveCeiling;
            frameSq += static_cast<double>(x) * static_cast<double>(x);
        }
        peak = std::max(peak, static_cast<double>(framePeak));
        frameSq /= static_cast<double>(channels.size());
        sumSq += frameSq;
        secondSum += frameSq;

        silentRun = framePeak < silence ? silentRun + 1 : 0;
        longestSilentRun = std::max(longestSilentRun, silentRun);

        if (++secondCount == perSecond || i + 1 == numSamples)
        {
            a.rmsPerSecondDb.push_back(toDb(std::sqrt(secondSum / static_cast<double>(secondCount))));
            secondSum = 0.0;
            secondCount = 0;
        }
    }

    a.peakDb = toDb(peak);
    a.rmsDb = toDb(std::sqrt(sumSq / static_cast<double>(numSamples)));
    a.longestSilenceSeconds = static_cast<float>(static_cast<double>(longestSilentRun) / sampleRate);
    return a;
}

juce::var Analysis::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("seconds", seconds);
    obj->setProperty("peakDb", peakDb);
    obj->setProperty("rmsDb", rmsDb);
    obj->setProperty("dcOffset", dcOffset);
    obj->setProperty("nonFiniteSamples", static_cast<juce::int64>(nonFiniteSamples));
    obj->setProperty("samplesAboveCeiling", static_cast<juce::int64>(samplesAboveCeiling));
    obj->setProperty("longestSilenceSeconds", longestSilenceSeconds);
    juce::Array<juce::var> env;
    for (float v : rmsPerSecondDb)
        env.add(std::round(v * 10.0f) / 10.0f);
    obj->setProperty("rmsPerSecondDb", env);
    return juce::var(obj);
}
}
