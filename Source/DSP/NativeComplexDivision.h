#pragma once

#include <complex>
#include <cstdint>

namespace acustra::nativeComplexDivision
{
// Apple Clang clang-2100.1.1.101 links the arm64 compiler-rt __divdc3
// archive member with these three FMAs. Its power-of-two normalization can
// be removed over this bounded range. Nonzero input magnitudes are at least
// 2^-128 and less than 2^128, so raw and normalized products, cancellations
// and quotients remain normal or exact zero. Round-to-nearest commutes with
// these exact positive powers of two, including the signs of zero.
#if defined(__APPLE__) && defined(__aarch64__) \
    && defined(__apple_build_version__) && __apple_build_version__ == 21000101 \
    && !defined(__FAST_MATH__)
inline constexpr bool hasFastPath = true;

inline bool appleComplexDivisionComponent(double value) noexcept
{
    std::uint64_t word;
    __builtin_memcpy(&word, &value, sizeof(word));
    word &= 0x7fffffffffffffffull;
    constexpr std::uint64_t first = 895ull << 52;
    constexpr std::uint64_t past = 1151ull << 52;
    return word == 0 || word - first < past - first;
}

inline std::complex<double> divide(const std::complex<double>& z,
                                   const std::complex<double>& w) noexcept
{
    const double a = z.real(), b = z.imag(), c = w.real(), d = w.imag();
    if (appleComplexDivisionComponent(a) && appleComplexDivisionComponent(b)
        && appleComplexDivisionComponent(c) && appleComplexDivisionComponent(d)
        && (c != 0.0 || d != 0.0) && __builtin_flt_rounds() == 1)
    {
        const double denominator = __builtin_fma(c, c, d * d);
        return { __builtin_fma(a, c, b * d) / denominator,
                 __builtin_fma(b, c, -(a * d)) / denominator };
    }
    return z / w;
}
#else
inline constexpr bool hasFastPath = false;

inline std::complex<double> divide(const std::complex<double>& z,
                                   const std::complex<double>& w) noexcept
{
    return z / w;
}
#endif
} // namespace acustra::nativeComplexDivision
