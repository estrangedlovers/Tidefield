#pragma once

#include "../core/Denormal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tf::dsp {
inline constexpr int kMaxSpeakers = 8;

inline float wrapDegrees(float deg) noexcept
{
    const float w = std::fmod(deg + 180.0f, 360.0f);
    return (w < 0.0f ? w + 360.0f : w) - 180.0f;
}

inline float speakerAzimuth(int k, int n) noexcept
{
    return wrapDegrees(-180.0f / static_cast<float>(n) + static_cast<float>(k) * 360.0f / static_cast<float>(n));
}

inline void ringGains(float azimuthDeg, int n, float* gains) noexcept
{
    std::fill_n(gains, kMaxSpeakers, 0.0f);
    if (n <= 0)
        return;
    const float sector = 360.0f / static_cast<float>(n);
    float pos = (wrapDegrees(azimuthDeg) + 180.0f / static_cast<float>(n)) / sector;
    pos = std::fmod(pos, static_cast<float>(n));
    if (pos < 0.0f)
        pos += static_cast<float>(n);
    const int k0 = std::min(n - 1, static_cast<int>(pos));
    const int k1 = (k0 + 1) % n;
    const float frac = pos - static_cast<float>(k0);
    constexpr float kHalfPi = 1.57079632679f;
    gains[k0] += std::cos(frac * kHalfPi);
    gains[k1] += std::sin(frac * kHalfPi);
}

class BinauralSource
{
public:
    static constexpr float kHeadRadius = 0.0875f;
    static constexpr float kSpeedOfSound = 343.0f;

    void prepare(double sampleRate)
    {
        fs = static_cast<float>(sampleRate);
        const int needed = static_cast<int>(std::ceil(0.0008 * sampleRate)) + 4;
        size = 1;
        while (size < needed)
            size <<= 1;
        line.assign(static_cast<std::size_t>(size), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill(line.begin(), line.end(), 0.0f);
        write = 0;
        shadowL = shadowR = rearL = rearR = 0.0f;
        current = target;
    }

    void setAzimuth(float deg) noexcept
    {
        const float theta = wrapDegrees(deg) * 0.01745329252f;
        const float lateral = std::sin(theta);
        const float side = std::fabs(lateral);
        const float front = std::cos(theta);
        const float lateralAngle = std::asin(std::min(1.0f, side));
        const float itd = kHeadRadius / kSpeedOfSound * (lateralAngle + side) * fs;
        const float farCut = 18000.0f * std::pow(0.1f, side);
        const float rearCut = front < 0.0f ? 18000.0f * std::pow(0.35f, -front) : 20000.0f;
        Params p;
        const bool right = lateral >= 0.0f;
        p.delayL = right ? itd : 0.0f;
        p.delayR = right ? 0.0f : itd;
        p.gainNear = 1.0f + 0.15f * side;
        p.gainFar = 1.0f - 0.3f * side;
        p.coefFar = coefficient(farCut);
        p.coefRear = coefficient(rearCut);
        p.leftIsFar = right && side > 1.0e-4f;
        p.rightIsFar = ! right && side > 1.0e-4f;
        target = p;
    }

    void snap() noexcept { current = target; }

    void processAdd(const float* in, float* outL, float* outR, int n, float scale) noexcept
    {
        if (n <= 0)
            return;
        const float inv = 1.0f / static_cast<float>(n);
        const float dL = (target.delayL - current.delayL) * inv, dR = (target.delayR - current.delayR) * inv;
        const float dNear = (target.gainNear - current.gainNear) * inv, dFar = (target.gainFar - current.gainFar) * inv;
        const float dCoef = (target.coefFar - current.coefFar) * inv, dRear = (target.coefRear - current.coefRear) * inv;
        const int mask = size - 1;
        for (int i = 0; i < n; ++i)
        {
            current.delayL += dL;
            current.delayR += dR;
            current.gainNear += dNear;
            current.gainFar += dFar;
            current.coefFar += dCoef;
            current.coefRear += dRear;
            line[static_cast<std::size_t>(write)] = in[i];
            const float l = read(current.delayL, mask);
            const float r = read(current.delayR, mask);
            write = (write + 1) & mask;

            const float leftCoef = target.leftIsFar ? current.coefFar : 1.0f;
            const float rightCoef = target.rightIsFar ? current.coefFar : 1.0f;
            shadowL += leftCoef * (l - shadowL);
            shadowR += rightCoef * (r - shadowR);
            rearL += current.coefRear * (shadowL - rearL);
            rearR += current.coefRear * (shadowR - rearR);
            const float gl = target.leftIsFar ? current.gainFar : current.gainNear;
            const float gr = target.rightIsFar ? current.gainFar : current.gainNear;
            outL[i] += rearL * gl * scale;
            outR[i] += rearR * gr * scale;
        }
        current = target;
        shadowL = flushDenormal(shadowL);
        shadowR = flushDenormal(shadowR);
        rearL = flushDenormal(rearL);
        rearR = flushDenormal(rearR);
    }

private:
    struct Params
    {
        float delayL = 0.0f, delayR = 0.0f;
        float gainNear = 1.0f, gainFar = 1.0f;
        float coefFar = 1.0f, coefRear = 1.0f;
        bool leftIsFar = false, rightIsFar = false;
    };

    float coefficient(float cutoff) const noexcept
    {
        if (cutoff >= 0.45f * fs)
            return 1.0f;
        return 1.0f - std::exp(-6.28318530718f * cutoff / fs);
    }

    float read(float delay, int mask) const noexcept
    {
        const float pos = static_cast<float>(write) - delay;
        const float floorPos = std::floor(pos);
        const float frac = pos - floorPos;
        const int i0 = static_cast<int>(floorPos) & mask;
        const int i1 = (i0 + 1) & mask;
        return line[static_cast<std::size_t>(i0)] * (1.0f - frac) + line[static_cast<std::size_t>(i1)] * frac;
    }

    float fs = 48000.0f;
    int size = 64;
    int write = 0;
    std::vector<float> line;
    float shadowL = 0.0f, shadowR = 0.0f, rearL = 0.0f, rearR = 0.0f;
    Params current, target;
};
}
