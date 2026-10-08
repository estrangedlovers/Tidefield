#pragma once

#include "../core/SampleBuffer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace tf::dsp {
struct PitchEstimate
{
    float hz = 0.0f;
    float midiNote = 0.0f;
    float confidence = 0.0f;
};

inline std::optional<float> yinFrame(const float* x, int window, int minLag, int maxLag, double sampleRate, float& clarity)
{
    std::vector<float> d(static_cast<std::size_t>(maxLag + 2), 0.0f);
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double diff = static_cast<double>(x[i]) - x[i + tau];
            sum += diff * diff;
        }
        d[static_cast<std::size_t>(tau)] = static_cast<float>(sum);
    }
    std::vector<float> cmnd(d.size(), 1.0f);
    double running = 0.0;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        running += d[static_cast<std::size_t>(tau)];
        cmnd[static_cast<std::size_t>(tau)] = running > 0.0 ? static_cast<float>(d[static_cast<std::size_t>(tau)] * tau / running) : 1.0f;
    }
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
        if (cmnd[static_cast<std::size_t>(tau)] < 0.15f)
        {
            while (tau + 1 <= maxLag && cmnd[static_cast<std::size_t>(tau + 1)] < cmnd[static_cast<std::size_t>(tau)])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        best = static_cast<int>(std::min_element(cmnd.begin() + minLag, cmnd.begin() + maxLag + 1) - cmnd.begin());
        if (cmnd[static_cast<std::size_t>(best)] > 0.35f)
            return std::nullopt;
    }
    clarity = 1.0f - cmnd[static_cast<std::size_t>(best)];
    const float a = cmnd[static_cast<std::size_t>(best - 1)], b = cmnd[static_cast<std::size_t>(best)], c = cmnd[static_cast<std::size_t>(best + 1)];
    const float denom = a - 2.0f * b + c;
    const float shift = std::abs(denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
    return static_cast<float>(sampleRate / (static_cast<double>(best) + std::clamp(shift, -0.5f, 0.5f)));
}

inline std::optional<PitchEstimate> detectPitch(const SampleBuffer& b, float lowHz = 40.0f, float highHz = 2000.0f)
{
    const double fs = b.sampleRate;
    if (b.size() < 4096 || fs <= 0.0)
        return std::nullopt;
    std::vector<float> mono(b.size());
    for (std::size_t i = 0; i < b.size(); ++i)
        mono[i] = b.isStereo() ? 0.5f * (b.left[i] + b.right[i]) : b.left[i];

    const int minLag = std::max(2, static_cast<int>(fs / highHz));
    const int maxLag = static_cast<int>(fs / lowHz);
    const int window = std::max(1024, maxLag);
    const int hop = window / 2;
    const auto total = static_cast<int>(mono.size());
    const int start = std::min(total / 10, static_cast<int>(0.05 * fs));
    std::vector<float> notes, clarities;
    for (int at = start; at + window + maxLag + 2 < total && notes.size() < 24; at += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += static_cast<double>(mono[static_cast<std::size_t>(at + i)]) * mono[static_cast<std::size_t>(at + i)];
        if (energy / window < 1.0e-6)
            continue;
        float clarity = 0.0f;
        if (const auto hz = yinFrame(mono.data() + at, window, minLag, maxLag, fs, clarity))
        {
            notes.push_back(69.0f + 12.0f * std::log2(*hz / 440.0f));
            clarities.push_back(clarity);
        }
    }
    if (notes.size() < 3)
        return std::nullopt;
    auto sorted = notes;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
    const float median = sorted[sorted.size() / 2];
    int agreeing = 0;
    float clarity = 0.0f;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (std::abs(notes[i] - median) < 0.5f)
        {
            ++agreeing;
            clarity += clarities[i];
        }
    const float confidence = static_cast<float>(agreeing) / static_cast<float>(notes.size()) * (agreeing > 0 ? clarity / static_cast<float>(agreeing) : 0.0f);
    if (confidence < 0.5f)
        return std::nullopt;
    return PitchEstimate { 440.0f * std::exp2((median - 69.0f) / 12.0f), median, confidence };
}
}
