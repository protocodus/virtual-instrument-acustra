#pragma once

#include <cmath>

namespace acustra::pickProjection
{
inline double phaseAngle(double real, double imaginary) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    const double y = __builtin_fabs(imaginary);
    const double x = __builtin_fabs(real);
#else
    const double y = std::abs(imaginary);
    const double x = std::abs(real);
#endif
    constexpr double pi = 3.141592653589793238462643383279502884;
    const bool swap = y > x;
    double ratio = swap ? x / y : y / x;
    double offset = 0.0;
    if (ratio > 0.41421356237309505)
    {
        ratio = (ratio - 1.0) / (ratio + 1.0);
        offset = 0.25 * pi;
    }
    const double t = ratio * ratio;
    // Range reduction bounds the first omitted term below 5e-10 radians.
    double angle = offset + ratio * (1.0 + t * (-1.0 / 3.0
        + t * (1.0 / 5.0 + t * (-1.0 / 7.0 + t * (1.0 / 9.0
        + t * (-1.0 / 11.0 + t * (1.0 / 13.0 + t * (-1.0 / 15.0
        + t * (1.0 / 17.0 - t / 19.0)))))))));
    if (swap)
        angle = 0.5 * pi - angle;
    if (real < 0.0)
        angle = pi - angle;
    return imaginary < 0.0 ? -angle : angle;
}
} // namespace acustra::pickProjection
