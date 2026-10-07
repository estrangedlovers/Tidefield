#pragma once

#include <cstdint>

namespace tf::dsp {

/** xoshiro128+ seeded through splitmix64. Small, fast, deterministic, allocation-free. */
class Random
{
public:
    explicit Random(std::uint64_t seed = 0x7469646566696c64ull) noexcept { setSeed(seed); }

    void setSeed(std::uint64_t seed) noexcept
    {
        for (auto& word : s)
        {
            seed += 0x9e3779b97f4a7c15ull;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            word = static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
        }
        if ((s[0] | s[1] | s[2] | s[3]) == 0)
            s[0] = 1;
    }

    std::uint32_t nextUInt() noexcept
    {
        const std::uint32_t result = s[0] + s[3];
        const std::uint32_t t = s[1] << 9;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = (s[3] << 11) | (s[3] >> 21);
        return result;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return static_cast<float>(nextUInt() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    float nextRange(float lo, float hi) noexcept { return lo + (hi - lo) * nextFloat(); }

    int nextInt(int maxExclusive) noexcept
    {
        return maxExclusive <= 0 ? 0 : static_cast<int>(nextUInt() % static_cast<std::uint32_t>(maxExclusive));
    }

    bool chance(float probability) noexcept { return nextFloat() < probability; }

private:
    std::uint32_t s[4] {};
};

} // namespace tf::dsp
