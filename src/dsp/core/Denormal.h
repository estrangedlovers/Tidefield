#pragma once

#include <cmath>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #include <immintrin.h>
 #define TF_DENORMAL_X86 1
#elif defined(__aarch64__)
 #define TF_DENORMAL_ARM64 1
#endif

namespace tf::dsp {
inline float flushDenormal(float x) noexcept
{
    return std::fabs(x) < 1.0e-15f ? 0.0f : x;
}

inline double flushDenormal(double x) noexcept
{
    return std::fabs(x) < 1.0e-30 ? 0.0 : x;
}

class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if TF_DENORMAL_X86
        previous = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned int>(previous) | 0x8040u);
#elif TF_DENORMAL_ARM64
        std::uint64_t fpcr = 0;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        previous = fpcr;
        fpcr |= (1ull << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedFlushDenormals() noexcept
    {
#if TF_DENORMAL_X86
        _mm_setcsr(static_cast<unsigned int>(previous));
#elif TF_DENORMAL_ARM64
        __asm__ __volatile__("msr fpcr, %0" : : "r"(previous));
#endif
    }

    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
    std::uint64_t previous = 0;
};
}
