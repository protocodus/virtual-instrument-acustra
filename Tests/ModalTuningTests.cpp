// The acoustic fundamental is a family of damped coupled poles. A short FFT
// peak can lie outside that family when unresolved modes cancel: old G3 and D3
// are negative controls documented in the chorus math-oracle receipt. This test
// independently reconstructs the ACTUAL digital bridge, all twelve string-loop
// transfers and the microphone observer. It never calls the tuning helper.
// Twelve bounded complex-root starts cover these five adversarial fixtures;
// residual and mode-count checks prevent accepting a missed or spurious pole.
// The 3-second radiated centre and dominant mode must stay within two cents,
// while both polarisations retain one speaking length to protect higher partials.

#include "DSP/AcustraEngine.h"

#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>

using C = std::complex<double>;
using A = std::array<C, 3>;
constexpr double pi = 3.14159265358979323846;

namespace acustra
{
struct AcustraEngineTestAccess
{
    static A bodyY(const AcustraEngine& e, C x)
    {
        C q = std::exp(-x), q2 = q * q;
        const auto& b = e.bridgeLoad_;
        A y{b.immediateHeave, b.immediateCross, b.immediateRock};
        for (int k = 0; k < b.activeModeCount; ++k)
        {
            int i = b.activeModes[k];
            const auto& m = b.heaveModes[i];
            C r = (m.numerator1 * q + m.numerator2 * q2) /
                  (1. + m.denominator1 * q + m.denominator2 * q2);
            y[0] += double(b.residueHeave[i]) * r;
            y[1] += double(b.residueCross[i]) * r;
            y[2] += double(b.residueRock[i]) * r;
        }
        return y;
    }

    static A load(A y, A g)
    {
        C a = 1. + y[0] * g[0] + y[1] * g[1], b = y[0] * g[1] + y[1] * g[2],
          c = y[1] * g[0] + y[2] * g[1], d = 1. + y[1] * g[1] + y[2] * g[2], det = a * d - b * c;
        return A{(d * y[0] - b * y[1]) / det, (d * y[1] - b * y[2]) / det,
                 (a * y[2] - c * y[1]) / det};
    }

    static C loop(const AcustraEngine::StringLoop& l, C x, double extra = 0.)
    {
        C q = std::exp(-x), q2 = q * q;
        auto ap = [&](double a, double b)
        {
            return (b + a * q + q2) / (1. + a * q + b * q2);
        };
        auto shelf = [&](const auto& s, double coefficient, double mix)
        {
            double pole = s.remapped ? s.ratePole : coefficient,
                   b1 = s.remapped ? s.delayedInputGain : 0.;
            return 1. + mix * ((1. - pole - b1 + b1 * q) / (1. - pole * q) - 1.);
        };
        C h = shelf(l.broadLossFilter, l.broadLossCoefficient, l.broadLossMix) *
              shelf(l.lossFilter, l.lowpassCoefficient, l.highLossMix);
        if (l.bendingLossActive)
            h *= 1. - (1. - q) * (double(1.f - l.bendingLossGain) - double(l.bendingLossA2) * q) /
                          (1. + double(l.bendingLossA1) * q + double(l.bendingLossA2) * q2);
        if (l.constantLossActive)
            h *= 1. - (1. - q) *
                          (double(1.f - l.constantLossGain) +
                           double(l.constantLossN2 - l.constantLossA2) * q) /
                          (1. + double(l.constantLossA1) * q + double(l.constantLossA2) * q2);
        h *= ap(l.dispersionA1, l.dispersionA2);
        if (l.secondDispersionActive)
            h *= ap(l.secondDispersionA1, l.secondDispersionA2);
        double d = l.currentDelay + extra;
        int n = int(std::floor(d - 1.1));
        double f = d - n, a = -2 * (f - 2) / (f + 1), b = (f - 2) * (f - 1) / ((f + 1) * (f + 2));
        return double(l.loopGain) * double(l.appliedReleaseGain) * h * std::exp(-x * double(n)) *
               ap(a, b);
    }

    struct Evaluation
    {
        C determinant{};
        std::array<C, 4> matrix{};
        A y{}, eff{};
    };

    static C fullDet(const AcustraEngine& e, C x)
    {
        A y = bodyY(e, x), g{}, g0{};
        C product = 1.;
        double eta = e.saddleHeightRatio();
        for (int i = 0; i < 6; ++i)
        {
            auto& v = e.voices_[i];
            double z = v.characteristicImpedance * v.appliedBendImpedanceScale, u = (i - 2.5) / 2.;
            for (int p = 0; p < 2; ++p)
            {
                C h = loop(v.loops[p], x), l = z * (1. + h) / (1. - h);
                product *= 1. - h;
                if (p == 0)
                {
                    g[0] += l;
                    g[1] += u * l;
                    g[2] += u * u * l;
                    g0[0] += z;
                    g0[1] += u * z;
                    g0[2] += u * u * z;
                }
                else
                {
                    g[2] += eta * eta * l;
                    g0[2] += eta * eta * z;
                }
            }
        }
        auto det = [&](A a)
        {
            return (1. + y[0] * a[0] + y[1] * a[1]) * (1. + y[1] * a[1] + y[2] * a[2]) -
                   (y[0] * a[1] + y[1] * a[2]) * (y[1] * a[0] + y[2] * a[1]);
        };
        return product * det(g) / det(g0);
    }

    static Evaluation evaluate(const AcustraEngine& e, C x, int st, double extra = 0.)
    {
        A y = bodyY(e, x), g{};
        double eta = e.saddleHeightRatio();
        for (int i = 0; i < 6; ++i)
        {
            if (i == st)
                continue;
            const auto& v = e.voices_[i];
            double z = v.characteristicImpedance * v.appliedBendImpedanceScale, u = (i - 2.5) / 2.;
            for (int p = 0; p < 2; ++p)
            {
                C h = loop(v.loops[p], x), l = z * (1. + h) / (1. - h);
                if (p == 0)
                {
                    g[0] += l;
                    g[1] += u * l;
                    g[2] += u * u * l;
                }
                else
                    g[2] += eta * eta * l;
            }
        }
        A eff = load(y, g);
        double arm = (st - 2.5) / 2.,
               z = e.voices_[st].characteristicImpedance * e.voices_[st].appliedBendImpedanceScale,
               Y0 = 1. / z;
        C n = eff[0] + 2 * arm * eff[1] + arm * arm * eff[2], p = eta * eta * eff[2],
          t = -eta * (eff[1] + arm * eff[2]), den = (Y0 + n) * (Y0 + p) - t * t;
        C rn = ((Y0 - n) * (Y0 + p) + t * t) / den, rp = ((Y0 - p) * (Y0 + n) + t * t) / den,
          rt = -2 * Y0 * t / den;
        C hn = loop(e.voices_[st].loops[0], x, extra), hp = loop(e.voices_[st].loops[1], x, extra);
        std::array<C, 4> m{hn * rn, hn * rt, hp * rt, hp * rp};
        return {(1. - m[0]) * (1. - m[3]) - m[1] * m[2], m, y, eff};
    }

    static std::array<C, 4> bodyRadiation(const AcustraEngine& e, C x)
    {
        C q = std::exp(-x);
        std::array<C, 4> out{};
        auto& b = e.bodyBank_;
        for (int i = 0; i < b.count; ++i)
        {
            C pole(b.poleReal[i], b.poleImaginary[i]), d = 1. / (1. - pole * q),
                                                       mirror = 1. / (1. - std::conj(pole) * q);
            std::array<C, 4> r{C(b.leftReal[i], b.leftImaginary[i]),
                               C(b.leftMomentReal[i], b.leftMomentImaginary[i]),
                               C(b.rightReal[i], b.rightImaginary[i]),
                               C(b.rightMomentReal[i], b.rightMomentImaginary[i])};
            for (int k = 0; k < 4; ++k)
                out[k] += r[k] * d + std::conj(r[k]) * mirror;
        }
        // The force derivative spans one 48 kHz reference sample. It uses
        // the same linearly interpolated history at noninteger rate ratios.
        const float referenceSamples = static_cast<float>(e.sampleRate_) / 48000.0f;
        const int whole = static_cast<int>(referenceSamples);
        const float fraction = referenceSamples - static_cast<float>(whole);
        const C derivative = 1. - std::pow(q, whole)
            * (1. - static_cast<double>(fraction) + static_cast<double>(fraction) * q);
        for (auto& r : out)
            r *= derivative;
        return out;
    }

    static double weight(const AcustraEngine& e, C x, int st, double time)
    {
        auto ev = evaluate(e, x, st);
        auto m = ev.matrix;
        auto& v = e.voices_[st];
        double eta = e.saddleHeightRatio(), u = (st - 2.5) / 2.,
               z = v.characteristicImpedance * v.appliedBendImpedanceScale;
        // adj(I-M) times one physical contact, normalized by the determinant derivative.
        C sn = std::sqrt(double(v.polarisationMix)),
          sp = double(v.pluckParallelSign) * std::sqrt(1. - double(v.polarisationMix));
        C an = (1. - m[3]) * sn + m[1] * sp, ap = m[2] * sn + (1. - m[0]) * sp;
        double ds = 1e-7;
        C deriv =
            (evaluate(e, x + ds, st).determinant - evaluate(e, x - ds, st).determinant) / (2 * ds);
        an /= deriv;
        ap /= deriv;
        A gt{z, u * z, (u * u + eta * eta) * z};
        auto y = load(ev.eff, gt);
        C f0 = 2 * z * an, f1 = 2 * z * (u * an - eta * ap);
        C heave = y[0] * f0 + y[1] * f1, rock = y[1] * f0 + y[2] * f1;
        C den = ev.y[0] * ev.y[2] - ev.y[1] * ev.y[1],
          force = (ev.y[2] * heave - ev.y[1] * rock) / den,
          moment = (ev.y[0] * rock - ev.y[1] * heave) / den;
        auto rad = bodyRadiation(e, x);
        C left = rad[0] * force + rad[1] * moment, right = rad[2] * force + rad[3] * moment;
        C mono = .5 * (left + right);
        left = mono + double(e.width_) * (left - mono);
        right = mono + double(e.width_) * (right - mono);
        double power = std::norm(left) + std::norm(right);
        return power * std::exp(2 * x.real() * e.sampleRate_ * time);
    }

    static bool check(AcustraEngine& e, int st, int note, float velocity,
                      std::size_t minimumModes)
    {
        e.setStringPerChannelMode(true);
        e.noteOn(note, velocity, st + 1);
        std::vector<float> l(3 * static_cast<int>(e.sampleRate_)), r(l.size());
        e.process(l.data(), r.data(), int(l.size()));
        double f = 440 * std::exp2((note - 69.) / 12), omega = 2 * pi * f / e.sampleRate_;
        std::vector<C> roots;
        for (double cents : {-24., -12., -4., 4., 12., 24.})
            for (double alpha : {.65, 3.5})
            {
                C x(-alpha / e.sampleRate_, omega * std::exp2(cents / 1200.));
                bool ok = false;
                for (int iter = 0; iter < 30; ++iter)
                {
                    C y = fullDet(e, x);
                    double ds = 1e-7;
                    C slope = (fullDet(e, x + ds) - fullDet(e, x - ds)) / (2 * ds);
                    if (!std::isfinite(std::abs(slope)) || std::abs(slope) < 1e-12)
                        break;
                    C step = y / slope;
                    if (std::abs(step) > .003 * omega)
                        step *= .003 * omega / std::abs(step);
                    x -= step;
                    if (std::abs(step) < 1e-12)
                    {
                        ok = true;
                        break;
                    }
                }
                if (!ok || x.real() >= 0 || std::abs(1200 * std::log2(x.imag() / omega)) > 60)
                    continue;
                bool seen = false;
                for (auto old : roots)
                    if (std::abs(old - x) < 1e-9)
                        seen = true;
                if (!seen)
                    roots.push_back(x);
            }
        std::sort(roots.begin(), roots.end(),
                  [](C a, C b)
                  {
                      return a.imag() < b.imag();
                  });
        double sumWeight = 0., sumCents = 0., largestWeight = 0., dominantCents = 0.;
        bool valid = roots.size() >= minimumModes;
        for (C x : roots)
        {
            const double residual = std::abs(fullDet(e, x));
            const double cents = 1200 * std::log2(x.imag() / omega), w = weight(e, x, st, 3.);
            valid &= std::isfinite(residual) && residual < 1e-8 && std::isfinite(w) && w >= 0.;
            if (w > largestWeight)
            {
                largestWeight = w;
                dominantCents = cents;
            }
            sumWeight += w;
            sumCents += w * cents;
        }
        const double centre = sumCents / sumWeight;
        valid &= std::isfinite(centre) && sumWeight > 0. && std::abs(centre) < 2.;
        valid &= std::abs(dominantCents) < 2.;
        valid &= e.voices_[st].loops[0].targetDelay == e.voices_[st].loops[1].targetDelay;
        valid &= e.voices_[st].loops[0].currentDelay == e.voices_[st].loops[1].currentDelay;
        std::cout << "Modal tuning rate=" << e.sampleRate_ << " shape=" << int(e.parameters_.shape)
                  << " string=" << st << " note=" << note << " modes=" << roots.size()
                  << " centre=" << centre << " cents, dominant=" << dominantCents << " cents\n";
        if (!valid)
            std::cerr << "FAIL: physical modal tuning or shared speaking length\n";
        return valid;
    }
};
}

int main()
{
    struct Fixture
    {
        acustra::BodyShape shape;
        int string;
        int note;
        float velocity;
        std::size_t minimumModes;
    };
    const std::array<Fixture, 5> fixtures {{
        { acustra::BodyShape::Dreadnought, 3, 55, .6f, 2 },
        { acustra::BodyShape::Dreadnought, 0, 52, .6f, 2 },
        { acustra::BodyShape::Dreadnought, 1, 50, .6f, 4 },
        { acustra::BodyShape::Jumbo, 0, 40, .6f, 2 },
        { acustra::BodyShape::Dreadnought, 2, 52, .72f, 4 }
    }};
    bool valid = true;
    for (double rate : {44100., 48000., 96000.})
        for (const auto& fixture : fixtures)
        {
            auto e = std::make_unique<acustra::AcustraEngine>();
            acustra::EngineParameters p;
            p.shape = fixture.shape;
            p.room = 0;
            p.releaseNoise = 0;
            e->setParameters(p);
            e->prepare(rate, 128);
            valid &= acustra::AcustraEngineTestAccess::check(
                *e, fixture.string, fixture.note, fixture.velocity, fixture.minimumModes);
        }
    if (valid)
        std::cout << "Physical modal tuning and shared-plane coherence passed\n";
    return valid ? 0 : 1;
}
