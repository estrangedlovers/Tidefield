#pragma once

#include "MathUtil.h"

#include <cmath>
#include <complex>
#include <vector>

namespace tf::dsp {

/** In-place iterative radix-2 complex FFT. prepare() allocates the twiddle and
    bit-reversal tables (off the audio thread); transforms are realtime-safe.
    Unnormalised: inverse(forward(x)) == size * x. */
class Fft
{
public:
    using Complex = std::complex<float>;

    void prepare(int order)
    {
        size = 1 << order;
        twiddles.resize(static_cast<std::size_t>(size / 2));
        for (int k = 0; k < size / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / size;
            twiddles[static_cast<std::size_t>(k)] = { static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)) };
        }
        reversed.resize(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                r |= ((i >> b) & 1) << (order - 1 - b);
            reversed[static_cast<std::size_t>(i)] = r;
        }
    }

    int getSize() const noexcept { return size; }

    void forward(Complex* data) const noexcept { transform(data, false); }
    void inverse(Complex* data) const noexcept { transform(data, true); }

private:
    void transform(Complex* x, bool inv) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const int r = reversed[static_cast<std::size_t>(i)];
            if (r > i)
                std::swap(x[i], x[r]);
        }
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size / len;
            for (int start = 0; start < size; start += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = twiddles[static_cast<std::size_t>(k * step)];
                    if (inv)
                        w = std::conj(w);
                    const Complex t = w * x[start + k + half];
                    x[start + k + half] = x[start + k] - t;
                    x[start + k] += t;
                }
        }
    }

    int size = 0;
    std::vector<Complex> twiddles;
    std::vector<int> reversed;
};

} // namespace tf::dsp
