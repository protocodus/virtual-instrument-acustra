// The Original capture contour belongs to the summed pressure. These checks
// compare its complex response with an independent analog prototype, retain
// the measured interference, and exercise its bank-local histories.
#include "DSP/AcustraEngine.h"
#include "DSP/CaptureVoicingData.h"
#include "DSP/MeasuredBodyData.h"
#include "DSP/ModelConvergenceData.h"
#include "DSP/SteelBodyBlend.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto bank(const AcustraEngine& engine) { return engine.bodyBank_; }
    static auto filter(const AcustraEngine& engine) { return engine.bodyBank_.captureFilter; }
    static auto body(AcustraEngine& engine, float force, float moment)
    { return engine.renderBody(force, moment); }
    template <typename Filter>
    static std::array<float, 2> sample(Filter& filter, float left, float right)
    {
        AcustraEngine::BodyOutput output { left, right, right };
        filter.render(output);
        return { output.left, output.right };
    }
    static float fade(const AcustraEngine& engine) { return engine.bodyModelFade_; }
    static float analogGain(float frequency) { return AcustraEngine::captureVoicingGain(frequency); }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Complex = std::complex<double>;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0;
void expect(bool pass, const std::string& message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

Complex target(double frequency, double rate)
{
    // Evaluate the analog prototypes at the bilinear frequency. This uses
    // neither runtime digital coefficients nor the runtime gain helper.
    Complex response = std::pow(10.0,
        (double(acustra::detail::captureVoicingLevelDb) + double(1.2172f)) / 20.0);
    for (const auto& section : acustra::detail::captureVoicingSections)
    {
        // Independent authored capture refinement; keep the prototype
        // separate from the production coefficient/helper implementation.
        const double gainDb = double(section.gainDb)
            - (section.kind == acustra::detail::CaptureVoicingKind::LowShelf
               && section.frequencyHz == 120.0f ? 3.0 : 0.0)
            - (section.kind == acustra::detail::CaptureVoicingKind::Peak
               && section.frequencyHz == 125.0f ? 2.75 : 0.0);
        const double a = std::pow(10.0, gainDb / 40.0);
        const Complex s(0.0, std::tan(pi * frequency / rate)
            / std::tan(pi * double(section.frequencyHz) / rate));
        const auto s2 = s * s;
        const double q = section.q;
        if (section.kind == acustra::detail::CaptureVoicingKind::Peak)
            response *= (s2 + a * s / q + 1.0) / (s2 + s / (a * q) + 1.0);
        else if (section.kind == acustra::detail::CaptureVoicingKind::LowShelf)
            response *= a * (s2 + std::sqrt(a) * s / q + a)
                / (a * s2 + std::sqrt(a) * s / q + 1.0);
        else
            response *= a * (a * s2 + std::sqrt(a) * s / q + 1.0)
                / (s2 + std::sqrt(a) * s / q + a);
    }
    return response;
}

Complex responseAt(const std::vector<float>& signal, double frequency, double rate)
{
    Complex response {};
    const auto step = std::polar(1.0, -2.0 * pi * frequency / rate);
    Complex phase = 1.0;
    for (float sample : signal) { response += double(sample) * phase; phase *= step; }
    return response;
}

void testDigitalTransferAndReset(double rate)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(rate, 64);
    auto filter = Access::filter(*engine);
    expect(filter.enabled && filter.activeSections == 6,
           "Original must configure the complete summed-pressure contour");
    const int length = int(rate * .25);
    std::vector<float> left(static_cast<std::size_t>(length)), right(left.size());
    for (int n = 0; n < length; ++n)
    {
        const auto output = Access::sample(filter, n == 0 ? .25f : 0.0f,
                                           n == 17 ? -.125f : 0.0f);
        left[std::size_t(n)] = output[0]; right[std::size_t(n)] = output[1];
    }
    double worst = 0.0;
    for (double frequency : { 0.0, 40.0, 82.4069, 125.0, 250.0, 392.0, 494.0,
                              500.0, 800.0, 1000.0, 1400.0, 2800.0, .45 * rate })
    {
        const auto expected = target(frequency, rate);
        const auto l = responseAt(left, frequency, rate) / .25;
        const auto r = responseAt(right, frequency, rate)
            / (-.125 * std::polar(1.0, -2.0 * pi * frequency * 17.0 / rate));
        worst = std::max({ worst, std::abs(l - expected) / std::abs(expected),
                                 std::abs(r - expected) / std::abs(expected) });
    }
    std::cout << "Original capture " << rate << " Hz complex response error " << worst << '\n';
    expect(worst < 1e-5, "Original complex capture transfer missed its independent prototype");
    filter.reset();
    bool same = true;
    for (int n = 0; n < length; ++n)
    {
        const auto output = Access::sample(filter, n == 0 ? .25f : 0.0f,
                                           n == 17 ? -.125f : 0.0f);
        same = same && output[0] == left[std::size_t(n)] && output[1] == right[std::size_t(n)];
    }
    expect(same, "capture reset did not reproduce the cleared impulse");
}

void testMeasuredResiduesAreNotEqualized()
{
    namespace d = acustra::detail;
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000, 64);
    const auto bank = Access::bank(*engine);
    // At the shipped Spruce/Dreadnought anchor the shape and wood factors
    // are unity. Its authored pressure scale remains 0.93, with bass 1.28.
    // Check complex force AND moment residues of the low-mid modes directly:
    // a post-sum filter must not also leave the former modal weighting here.
    for (int index : { 1, 2, 3, 4, 5, 6, 7, 8 })
    {
        const auto i = static_cast<std::size_t>(index);
        const auto& mode = d::measuredSteelBodyModes[i];
        const double frequency = std::atan2(bank.poleImaginary[i], bank.poleReal[i]) * 48000. / (2. * pi);
        const double share = mode.frequency < d::steelBlendJointBandHz
            ? 1.0 - d::steelBlendJointBodyWeight
            : 1.0 - d::steelBlendJointBodyWeight * d::steelBlendHighJointFraction;
        const double scale = .93 * (1.0 + .28 * std::exp(-frequency / 520.0))
            * share * d::modelConvergenceGain(float(frequency), false);
        const std::array<Complex, 4> expected {{
            scale * Complex(mode.leftReal, mode.leftImaginary),
            scale * Complex(mode.upperReal, mode.upperImaginary),
            scale * Complex(mode.leftMomentReal, mode.leftMomentImaginary),
            scale * Complex(mode.upperMomentReal, mode.upperMomentImaginary)
        }};
        const std::array<Complex, 4> actual {{
            { bank.leftReal[i], bank.leftImaginary[i] },
            { bank.rightReal[i], bank.rightImaginary[i] },
            { bank.leftMomentReal[i], bank.leftMomentImaginary[i] },
            { bank.rightMomentReal[i], bank.rightMomentImaginary[i] }
        }};
        for (std::size_t axis = 0; axis < actual.size(); ++axis)
            expect(std::abs(actual[axis] - expected[axis]) < 5e-5 * std::abs(expected[axis]),
                   "capture weighting remains in measured force/moment residues");
    }
}

template <typename Bank>
Complex modalResponse(const Bank& bank, double frequency, double arm, bool legacyWeight)
{
    const auto inverseZ = std::polar(1.0, -2.0 * pi * frequency / 48000.0);
    Complex result {};
    for (int index = 0; index < bank.count; ++index)
    {
        const auto i = static_cast<std::size_t>(index);
        const Complex pole(bank.poleReal[i], bank.poleImaginary[i]);
        if (pole.imag() == 0.0) continue;
        Complex residue(bank.leftReal[i] + arm * bank.leftMomentReal[i],
                        bank.leftImaginary[i] + arm * bank.leftMomentImaginary[i]);
        if (legacyWeight)
            residue *= Access::analogGain(float(std::arg(pole) * 48000.0 / (2.0 * pi)));
        result += residue / (1.0 - pole * inverseZ)
            + std::conj(residue) / (1.0 - std::conj(pole) * inverseZ);
    }
    return result;
}

void testLowMidCancellationRegression()
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000, 64);
    auto bank = Access::bank(*engine);
    // Exclude the statistically generated continuation so this is solely
    // the measured bank and the nine retained joint-body modes.
    bank.count = 141;
    auto dry = bank;
    dry.captureFilter.enabled = false;
    constexpr double frequency = 493.883301; // B4, treble-string force/moment.
    constexpr double arm = 1.25;
    const auto raw = modalResponse(bank, frequency, arm, false);
    const auto old = modalResponse(bank, frequency, arm, true);
    const auto desired = target(frequency, 48000.0);
    const double oldDb = 20.0 * std::log10(std::abs(old / raw));
    const double targetDb = 20.0 * std::log10(std::abs(desired));
    // The authored scalar restores phrase loudness; assess the tonal cut
    // relative to that scalar, so releveling cannot obscure its sign.
    const double referenceDb = double(acustra::detail::captureVoicingLevelDb)
        + double(1.2172f);
    expect(oldDb - referenceDb > 0.0 && targetDb - referenceDb < -1.0,
           "regression no longer exposes the old cut-to-boost cancellation error");
    std::vector<float> actual(48000), reference(actual.size());
    for (int n = 0; n < 48000; ++n)
    {
        const float force = n == 0 ? .2f : 0.0f;
        actual[std::size_t(n)] = bank.render(force, float(arm) * force).left;
        reference[std::size_t(n)] = dry.render(force, float(arm) * force).left;
    }
    const auto measured = responseAt(actual, frequency, 48000.0)
        / responseAt(reference, frequency, 48000.0);
    expect(std::abs(measured - desired) < .001 * std::abs(desired),
           "rendered pressure did not retain cancellation through the intended capture cut");
    std::cout << "B4 capture: former modal weighting " << oldDb
              << " dB; summed-pressure contour " << 20. * std::log10(std::abs(measured)) << " dB\n";
}

void testOriginalHistoryAndReprepare(double rate)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(rate, 64);
    for (int n = 0; n < int(.015 * rate); ++n)
        Access::body(*engine, .02f * std::sin(.03f * float(n)), .001f);
    const auto before = Access::filter(*engine);
    acustra::EngineParameters p;
    p.shape = acustra::BodyShape::Parlor;
    engine->setParameters(p);
    const auto after = Access::filter(*engine);
    expect(before.stateLeft == after.stateLeft && before.stateRight == after.stateRight,
           "Original construction change erased the capture's ringing history");
    expect(Access::fade(*engine) == 0.0f, "construction change did not begin its bank fade");
    bool finite = true;
    for (int n = 0; n < int(.06 * rate); ++n)
    {
        const auto out = Access::body(*engine, .01f * std::sin(.06f * float(n)), 0.0f);
        finite = finite && std::isfinite(out.left) && std::isfinite(out.right);
    }
    expect(finite && Access::fade(*engine) == 1.0f, "Original capture fade did not settle safely");
    engine->allSoundOff();
    const auto cleared = Access::filter(*engine);
    expect(cleared.stateLeft == decltype(cleared.stateLeft) {}
               && cleared.stateRight == decltype(cleared.stateRight) {},
           "all sound off retained capture filter energy");
    engine->prepare(rate * 2.0, 64);
    auto fresh = std::make_unique<Engine>();
    fresh->setParameters(p);
    fresh->prepare(rate * 2.0, 64);
    bool same = true;
    for (int n = 0; n < 1024; ++n)
    {
        const auto expected = Access::body(*fresh, n == 0 ? .2f : 0.0f, 0.0f);
        const auto actual = Access::body(*engine, n == 0 ? .2f : 0.0f, 0.0f);
        same = same && actual.left == expected.left && actual.right == expected.right;
    }
    expect(same, "Original capture reprepare differs from the fresh-rate engine");
}
}

int main()
{
    for (double rate : { 8000., 24000., 44100., 48000., 96000., 192000., 384000. })
        testDigitalTransferAndReset(rate);
    testMeasuredResiduesAreNotEqualized();
    testLowMidCancellationRegression();
    for (double rate : { 44100., 48000., 96000. }) testOriginalHistoryAndReprepare(rate);
    return failures == 0 ? 0 : 1;
}
