#include "DSP/NativeComplexDivision.h"

#include <array>
#include <cfenv>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
using Complex = std::complex<double>;

// Keep the comparison against the actual runtime, with neither side folded
// into the other by the optimizer.
__attribute__((noinline)) Complex libraryDivision(Complex numerator, Complex divisor)
{
    return numerator / divisor;
}

__attribute__((noinline)) Complex engineDivision(Complex numerator, Complex divisor)
{
    return acustra::nativeComplexDivision::divide(numerator, divisor);
}

std::uint64_t bits(double value)
{
    std::uint64_t word;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

double fromBits(std::uint64_t word)
{
    double value;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

bool matches(Complex numerator, Complex divisor, int rounding)
{
    std::feclearexcept(FE_ALL_EXCEPT);
    const Complex expected = libraryDivision(numerator, divisor);
    const int expectedFlags = std::fetestexcept(FE_ALL_EXCEPT);
    std::feclearexcept(FE_ALL_EXCEPT);
    const Complex actual = engineDivision(numerator, divisor);
    const int actualFlags = std::fetestexcept(FE_ALL_EXCEPT);
    if (bits(expected.real()) == bits(actual.real())
        && bits(expected.imag()) == bits(actual.imag())
        && expectedFlags == actualFlags && std::fegetround() == rounding)
        return true;
    std::fprintf(stderr,
        "Complex division changed at rounding mode %d for (%a,%a)/(%a,%a). "
        "Expected (%a,%a) with flags %d, got (%a,%a) with flags %d.\n",
        rounding, numerator.real(), numerator.imag(), divisor.real(), divisor.imag(),
        expected.real(), expected.imag(), expectedFlags,
        actual.real(), actual.imag(), actualFlags);
    return false;
}

std::uint64_t nextWord(std::uint64_t& state)
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545f4914f6cdd1dull;
}

double sample(std::uint64_t& state)
{
    const auto mantissa = nextWord(state) & 0x800fffffffffffffull;
    const auto exponent = 895ull + (nextWord(state) & 255ull);
    return fromBits(mantissa | (exponent << 52));
}

bool exercise(int rounding)
{
    // Signed zeros, a subnormal, the guard's lower and upper neighbors,
    // a unit value, the largest finite, infinities, and two NaN forms.
    constexpr std::array<std::uint64_t, 11> magnitudes {
        0x0000000000000000ull, 0x0000000000000001ull,
        0x37efffffffffffffull, 0x37f0000000000000ull,
        0x3ff0000000000000ull, 0x47efffffffffffffull,
        0x47f0000000000000ull, 0x7fefffffffffffffull,
        0x7ff0000000000000ull, 0x7ff8000000000009ull,
        0x7ff0000000000041ull
    };
    std::array<double, 2 * magnitudes.size()> edge {};
    for (std::size_t i = 0; i < magnitudes.size(); ++i)
    {
        edge[2 * i] = fromBits(magnitudes[i]);
        edge[2 * i + 1] = fromBits(magnitudes[i] | 0x8000000000000000ull);
    }
    for (const double a : edge)
        for (const double b : edge)
            for (const double c : edge)
                for (const double d : edge)
                    if (!matches({ a, b }, { c, d }, rounding))
                        return false;

    std::uint64_t state = 0x45ba68c944c74aaaull;
    for (int i = 0; i < 4096; ++i)
    {
        const double a = sample(state), b = sample(state);
        const double c = sample(state), d = sample(state);
        if (!matches({ a, b }, { c, d }, rounding)
            || !matches({ a, a }, { c, -c }, rounding))
            return false;
    }
    return true;
}
}

int main()
{
    std::fenv_t saved {};
    if (std::fegetenv(&saved) != 0)
        return 2;
    bool passed = true;
    for (const int rounding : { FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO })
    {
        if (std::fesetround(rounding) != 0 || !exercise(rounding))
        {
            passed = false;
            break;
        }
    }
    if (std::fesetenv(&saved) != 0 || !passed)
        return 1;
    std::printf("Native complex division matched the runtime in four rounding modes "
                "(bounded Apple arm64 path %s).\n",
                acustra::nativeComplexDivision::hasFastPath ? "enabled" : "disabled");
}
