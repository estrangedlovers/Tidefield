#pragma once

#include "MathUtil.h"

#include <cmath>
#include <complex>
#include <vector>

namespace tf::dsp {
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
                    const auto& w = twiddles[static_cast<std::size_t>(k * step)];
                    const float wr = w.real();
                    const float wi = inv ? -w.imag() : w.imag();
                    auto& a = x[start + k];
                    auto& b = x[start + k + half];
                    const float br = b.real(), bi = b.imag();
                    const float tr = wr * br - wi * bi;
                    const float ti = wr * bi + wi * br;
                    const float ar = a.real(), ai = a.imag();
                    b = { ar - tr, ai - ti };
                    a = { ar + tr, ai + ti };
                }
        }
    }

    int size = 0;
    std::vector<Complex> twiddles;
    std::vector<int> reversed;
};
}
