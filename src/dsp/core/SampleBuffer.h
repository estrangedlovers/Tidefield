#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace tf::dsp {
struct SampleBuffer
{
    std::vector<float> left;
    std::vector<float> right;
    double sampleRate = 48000.0;
    std::string name;

    std::size_t size() const noexcept { return left.size(); }
    bool isStereo() const noexcept { return ! right.empty(); }
    const float* channel(int ch) const noexcept { return ch == 1 && isStereo() ? right.data() : left.data(); }
    double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double>(size()) / sampleRate : 0.0; }

    std::vector<std::vector<float>> mipLeft, mipRight;

    int mipLevels() const noexcept { return static_cast<int>(mipLeft.size()); }
    const float* mipChannel(int ch, int level) const noexcept
    {
        if (level <= 0 || level > mipLevels())
            return channel(ch);
        const auto& v = ch == 1 && isStereo() ? mipRight[static_cast<std::size_t>(level - 1)] : mipLeft[static_cast<std::size_t>(level - 1)];
        return v.data();
    }
    std::size_t mipSize(int level) const noexcept
    {
        return level <= 0 || level > mipLevels() ? size() : mipLeft[static_cast<std::size_t>(level - 1)].size();
    }
};

inline int mipLevelFor(double absIncrement, int available) noexcept
{
    if (absIncrement <= 1.1 || available <= 0)
        return 0;
    const int level = static_cast<int>(std::ceil(std::log2(absIncrement / 1.1)));
    return std::min(level, available);
}

inline void buildMips(SampleBuffer& b, int levels = 3)
{
    constexpr int kHalf = 31;
    std::vector<float> h(2 * kHalf + 1);
    constexpr double kCut = 0.225;
    double sum = 0.0;
    for (int k = -kHalf; k <= kHalf; ++k)
    {
        const double x = static_cast<double>(k);
        const double sinc = k == 0 ? 2.0 * kCut : std::sin(2.0 * 3.14159265358979323846 * kCut * x) / (3.14159265358979323846 * x);
        const double t = (x + kHalf) / (2.0 * kHalf);
        const double blackman = 0.42 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * t) + 0.08 * std::cos(4.0 * 3.14159265358979323846 * t);
        h[static_cast<std::size_t>(k + kHalf)] = static_cast<float>(sinc * blackman);
        sum += sinc * blackman;
    }
    for (auto& c : h)
        c = static_cast<float>(c / sum);

    auto halve = [&](const std::vector<float>& in) {
        std::vector<float> out((in.size() + 1) / 2);
        const auto n = static_cast<long>(in.size());
        for (std::size_t j = 0; j < out.size(); ++j)
        {
            const long centre = static_cast<long>(2 * j);
            double acc = 0.0;
            for (int k = -kHalf; k <= kHalf; ++k)
            {
                const long idx = centre + k;
                if (idx >= 0 && idx < n)
                    acc += static_cast<double>(in[static_cast<std::size_t>(idx)]) * h[static_cast<std::size_t>(k + kHalf)];
            }
            out[j] = static_cast<float>(acc);
        }
        return out;
    };

    b.mipLeft.clear();
    b.mipRight.clear();
    b.mipLeft.reserve(static_cast<std::size_t>(levels));
    b.mipRight.reserve(static_cast<std::size_t>(levels));
    const std::vector<float>* srcL = &b.left;
    const std::vector<float>* srcR = &b.right;
    for (int level = 0; level < levels && srcL->size() > 64; ++level)
    {
        b.mipLeft.push_back(halve(*srcL));
        if (b.isStereo())
            b.mipRight.push_back(halve(*srcR));
        srcL = &b.mipLeft.back();
        if (b.isStereo())
            srcR = &b.mipRight.back();
    }
}
}
