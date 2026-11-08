#pragma once

#include <cmath>

namespace acustra::pickBending
{
// Static, pinned stiff string: q_n / q_n,flex = 1/(1 + B n^2)
// at the same applied force (Woodhouse, Acta Acustica 90 (2004), §3.1).
// In unit round-trip phase this is (1 - width^2 d^2/dphase^2)^-1,
// width = sqrt(B)/(2 pi). The flexible Gaussian triangle's second
// derivative is two opposite Gaussian corners. Add width^2 times their
// screened Green response to the existing shape, without changing its
// separately authored release velocity or the total saddle reaction.
class Profile
{
public:
    Profile(double sigma, double width) noexcept
        : sigma_(sigma), width_(width),
          ratio_(width > 0.0 ? sigma / width : 0.0),
          reach_(40.0 * width + 12.0 * sigma)
    {}

    double corner(double phase) const noexcept
    {
        if (!(width_ > 0.0))
            return 0.0;
        const double wrapped = phase - std::floor(phase + 0.5);
        double sum = atDistance(std::abs(wrapped));
        // Outside this range the normal-Laplace density is bounded by
        // (exp(-40) + P(|Z| > 12))/(2 width). Sum nearby periodic images
        // for broad footprints and retain the same bound at either edge.
        for (int image = 1; image - std::abs(wrapped) < reach_; ++image)
            sum += atDistance(image + wrapped) + atDistance(image - wrapped);
        return sum;
    }

private:
    static double scaledErfc(double x) noexcept
    {
        if (x < 26.0)
            return std::exp(x * x) * std::erfc(x);
        // The first omitted asymptotic term is below 1.7e-17 relative.
        constexpr double inverseSqrtPi = 0.56418958354775628695;
        const double inverse = 1.0 / x;
        const double r = 0.5 * inverse * inverse;
        return inverseSqrtPi * inverse * (1.0 + r * (-1.0 + r * (3.0
            + r * (-15.0 + r * (105.0 + r * (-945.0 + 10395.0 * r))))));
    }

    double atDistance(double distance) const noexcept
    {
        if (distance >= reach_)
            return 0.0;
        constexpr double inverseSqrtTwo = 0.70710678118654752440;
        const double t = distance / sigma_;
        const double left = (ratio_ - t) * inverseSqrtTwo;
        const double right = (ratio_ + t) * inverseSqrtTwo;
        const double gaussian = std::exp(-0.5 * t * t);
        // Scale the positive erfc arguments before multiplication so the
        // normal-Laplace form stays finite even as bending tends to zero.
        const double lower = left >= 0.0 ? gaussian * scaledErfc(left)
            : std::exp(ratio_ * (0.5 * ratio_ - t))
                * (left < -6.0 ? 2.0 : std::erfc(left));
        return 0.25 * width_ * (lower + gaussian * scaledErfc(right));
    }

    double sigma_, width_, ratio_, reach_;
};
} // namespace acustra::pickBending
