#pragma once

#include <cmath>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
 #include <immintrin.h>
 #define TF_DENORMAL_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
 #define TF_DENORMAL_ARM64 1
#endif

namespace tf::dsp {

/** Flushes values too small to matter to exact zero. Use on every recursive state. */
inline float flushDenormal(float x) noexcept
{
    return std::fabs(x) < 1.0e-15f ? 0.0f : x;
}

inline double flushDenormal(double x) noexcept
{
    return std::fabs(x) < 1.0e-30 ? 0.0 : x;
}

/** RAII: enables flush-to-zero / denormals-are-zero on the current thread. */
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if TF_DENORMAL_X86
        previous = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned int>(previous) | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#elif TF_DENORMAL_ARM64
        std::uint64_t fpcr = 0;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        previous = fpcr;
        fpcr |= (1ull << 24); // FZ
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

} // namespace tf::dsp
