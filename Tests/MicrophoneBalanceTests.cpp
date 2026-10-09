// Broad microphone balance must change the native harmonic observation while
// retaining every physical pole, residue, bridge trajectory and pickup sample.
#include "DSP/AcustraEngine.h"
#include "DSP/MicrophoneBalanceData.h"

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
    static auto filter(const AcustraEngine& engine) { return engine.bodyBank_.captureFilter; }
    template <typename Filter>
    static float filterSample(Filter& filter, float sample)
    {
        AcustraEngine::BodyOutput output { sample, sample, sample };
        filter.render(output);
        return output.left;
    }
    template <typename Designer>
    static void restorePreviousCapture(AcustraEngine& engine, Designer designer)
    {
        auto& filter = engine.bodyBank_.captureFilter;
        const bool classical = filter.bellidoModel;
        for (int index = 0; index < filter.activeSections; ++index)
            filter.coefficients[static_cast<std::size_t>(index)]
                = designer(index, engine.sampleRate_, classical);
        filter.gain = classical ? 1.0f : static_cast<float>(std::pow(10.0, 4.12 / 20.0));
        filter.reset();
    }
    static bool samePhysicalBody(const AcustraEngine& a, const AcustraEngine& b)
    {
        const auto& x = a.bodyBank_;
        const auto& y = b.bodyBank_;
        return x.count == y.count && x.ordered == y.ordered
            && x.poleReal == y.poleReal && x.poleImaginary == y.poleImaginary
            && x.leftReal == y.leftReal && x.leftImaginary == y.leftImaginary
            && x.rightReal == y.rightReal && x.rightImaginary == y.rightImaginary
            && x.leftMomentReal == y.leftMomentReal
            && x.leftMomentImaginary == y.leftMomentImaginary
            && x.rightMomentReal == y.rightMomentReal
            && x.rightMomentImaginary == y.rightMomentImaginary
            && a.bridgeLoad_.residueHeave == b.bridgeLoad_.residueHeave
            && a.bridgeLoad_.residueCross == b.bridgeLoad_.residueCross
            && a.bridgeLoad_.residueRock == b.bridgeLoad_.residueRock;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Complex = std::complex<double>;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0;
bool previousCaptureNegativeControl = false;

void expect(bool pass, const std::string& message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct Section { bool shelf; double frequency, gain, q; };
Section sectionAt(int index, bool classical, bool previous)
{
    // Independent authored contracts: no production section refinement helper
    // or production digital coefficient calculation is used by this fixture.
    constexpr std::array<Section, 6> original {{
        { true, 120.0, .58, .7 }, { false, 125.0, 2.75, 1.2 },
        { false, 250.0, -6.0, 1.2 }, { false, 500.0, -6.0, 1.2 },
        { false, 1000.0, 3.28, 1.2 }, { false, 1400.0, 6.0, 1.2 }
    }};
    if (classical)
        return index == 0 ? Section { false, 500.0, previous ? -6.0 : -3.0, 1.2 }
            : Section { false, 1400.0, previous ? 6.0 : 0.0, 1.2 };
    auto section = original[static_cast<std::size_t>(index)];
    if (!previous && index == 0) section.gain = -2.42;
    if (!previous && index == 1) section.gain = 0.0;
    return section;
}

Complex prototype(double frequency, double rate, bool classical, bool previous)
{
    const double level = classical ? 0.0 : 4.12
        + (previous ? 0.0 : acustra::detail::originalCaptureLevelAdjustmentDb);
    Complex result = std::pow(10.0, level / 20.0);
    for (int index = 0; index < (classical ? 2 : 6); ++index)
    {
        const auto section = sectionAt(index, classical, previous);
        const double a = std::pow(10.0, section.gain / 40.0);
        const Complex s(0.0, std::tan(pi * frequency / rate)
            / std::tan(pi * section.frequency / rate));
        if (section.shelf)
            result *= a * (s * s + std::sqrt(a) * s / section.q + a)
                / (a * s * s + std::sqrt(a) * s / section.q + 1.0);
        else
            result *= (s * s + a * s / section.q + 1.0)
                / (s * s + s / (a * section.q) + 1.0);
    }
    return result;
}

std::array<double, 5> previousCoefficients(int index, double rate, bool classical)
{
    const auto section = sectionAt(index, classical, true);
    const double a = std::pow(10.0, section.gain / 40.0);
    // Bilinear substitution into the continuous numerator/denominator
    // polynomials, independent of the runtime trigonometric RBJ equations.
    const auto transform = [k = 1.0 / std::tan(pi * section.frequency / rate)]
        (double quadratic, double linear, double constant)
    {
        return std::array<double, 3> {
            quadratic * k * k + linear * k + constant,
            -2.0 * quadratic * k * k + 2.0 * constant,
            quadratic * k * k - linear * k + constant };
    };
    const auto numerator = section.shelf
        ? transform(a, a * std::sqrt(a) / section.q, a * a)
        : transform(1.0, a / section.q, 1.0);
    const auto denominator = section.shelf
        ? transform(a, std::sqrt(a) / section.q, 1.0)
        : transform(1.0, 1.0 / (a * section.q), 1.0);
    std::array<double, 5> result { numerator[0] / denominator[0],
        numerator[1] / denominator[0], numerator[2] / denominator[0],
        denominator[1] / denominator[0], denominator[2] / denominator[0] };
    if (classical)
        for (auto& value : result) value = static_cast<float>(value);
    return result;
}

Complex transform(const std::vector<float>& samples, double frequency, double rate)
{
    const auto step = std::polar(1.0, -2.0 * pi * frequency / rate);
    Complex phase = 1.0, result {};
    for (float sample : samples) { result += double(sample) * phase; phase *= step; }
    return result;
}

void testActualFilter(double rate, bool classical)
{
    auto engine = std::make_unique<Engine>();
    acustra::EngineParameters parameters;
    parameters.guitarModel = classical ? acustra::GuitarModel::Bellido1978
        : acustra::GuitarModel::Original;
    engine->setParameters(parameters);
    engine->prepare(rate, 64);
    if (previousCaptureNegativeControl)
        Access::restorePreviousCapture(*engine, previousCoefficients);
    auto filter = Access::filter(*engine);
    std::vector<float> impulse(static_cast<std::size_t>(.35 * rate));
    for (std::size_t i = 0; i < impulse.size(); ++i)
        impulse[i] = Access::filterSample(filter, i == 0 ? .25f : 0.0f);
    double worst = 0.0;
    for (double frequency : { 55.0, 110.0, 125.0, 220.0, 440.0, 659.255,
                              880.0, 1320.0, 1400.0, 2500.0, 5000.0 })
    {
        const auto expected = prototype(frequency, rate, classical, false);
        worst = std::max(worst, std::abs(transform(impulse, frequency, rate) / .25
            - expected) / std::abs(expected));
    }
    expect(worst < (classical ? 8e-5 : 1e-5),
           "deployed balanced microphone filter misses independent complex prototype");
    std::cout << (classical ? "Classical" : "Original") << " capture " << rate
              << " Hz relative complex error " << worst << '\n';
}

double harmonicEnergy(const std::vector<float>& left, const std::vector<float>& right,
                      double frequency, double rate)
{
    const auto first = static_cast<std::size_t>(.25 * rate);
    const auto last = static_cast<std::size_t>(.75 * rate);
    const auto step = std::polar(1.0, -2.0 * pi * frequency / rate);
    Complex phase = 1.0, l {}, r {};
    for (std::size_t i = first; i < last; ++i)
    {
        const double window = .5 - .5 * std::cos(2.0 * pi * double(i - first)
            / double(last - first - 1));
        l += double(left[i]) * window * phase;
        r += double(right[i]) * window * phase;
        phase *= step;
    }
    return std::norm(l) + std::norm(r);
}

void testNativeNote(double rate, acustra::BodyMaterial wood,
                    acustra::PickingTechnique style, acustra::CaptureType capture,
                    int note, int channel)
{
    const bool classical = note != 45;
    auto current = std::make_unique<Engine>();
    auto previous = std::make_unique<Engine>();
    acustra::EngineParameters parameters;
    parameters.guitarModel = classical ? acustra::GuitarModel::Bellido1978
        : acustra::GuitarModel::Original;
    parameters.shape = acustra::BodyShape::Auditorium;
    parameters.bodyMaterial = wood;
    parameters.picking = style;
    parameters.capture = capture;
    parameters.room = 0.0f;
    for (auto* engine : { current.get(), previous.get() })
    {
        engine->setParameters(parameters);
        engine->prepare(rate, 64);
        engine->setStringPerChannelMode(true);
    }
    Access::restorePreviousCapture(*previous, previousCoefficients);
    if (previousCaptureNegativeControl)
        Access::restorePreviousCapture(*current, previousCoefficients);
    expect(Access::samePhysicalBody(*current, *previous),
           "capture refinement modified physical radiation/bridge poles or residues");
    current->noteOn(note, 100.0f / 127.0f, channel);
    previous->noteOn(note, 100.0f / 127.0f, channel);
    const auto length = static_cast<std::size_t>(rate);
    std::vector<float> newLeft(length), newRight(length), oldLeft(length), oldRight(length);
    bool sameMechanics = true, samePickup = true, finite = true;
    float peak = 0.0f;
    for (std::size_t i = 0; i < length; ++i)
    {
        float newPickup = 0.0f, oldPickup = 0.0f;
        const Engine::OutputBuses newBuses { &newPickup }, oldBuses { &oldPickup };
        current->process(&newLeft[i], &newRight[i], newBuses, 1);
        previous->process(&oldLeft[i], &oldRight[i], oldBuses, 1);
        samePickup = samePickup && newPickup == oldPickup;
        sameMechanics = sameMechanics
            && current->getLastBridgeReactionForce() == previous->getLastBridgeReactionForce()
            && current->getLastBridgeBodyForce() == previous->getLastBridgeBodyForce()
            && current->getLastBridgeVelocity() == previous->getLastBridgeVelocity();
        for (float value : { newLeft[i], newRight[i], oldLeft[i], oldRight[i] })
        {
            finite = finite && std::isfinite(value);
            peak = std::max(peak, std::abs(value));
        }
    }
    expect(sameMechanics, "microphone refinement altered native bridge force/motion trajectories");
    expect(samePickup, "microphone refinement altered a dedicated piezo sample");
    expect(finite && peak < .89125094f,
           "native balance fixture must remain finite and below the limiter knee");
    const double f0 = 440.0 * std::exp2(double(note - 69) / 12.0);
    // Original A2: bass H1 relative to H2. Classical A4/E5: the presence-band
    // H3/H2 relative to H1. These compare native channel energies, without
    // per-note normalization or an output trim influencing the ratio.
    const int numerator = classical ? (note == 69 ? 3 : 2) : 1;
    const int denominator = classical ? 1 : 2;
    const double oldNumerator = harmonicEnergy(oldLeft, oldRight, f0 * numerator, rate);
    const double oldDenominator = harmonicEnergy(oldLeft, oldRight, f0 * denominator, rate);
    const double newNumerator = harmonicEnergy(newLeft, newRight, f0 * numerator, rate);
    const double newDenominator = harmonicEnergy(newLeft, newRight, f0 * denominator, rate);
    expect(std::min({ oldNumerator, oldDenominator, newNumerator, newDenominator }) > 1e-8,
           "native harmonic balance fixture did not excite both measured harmonics");
    const double delta = 10.0 * std::log10((newNumerator / newDenominator)
        / (oldNumerator / oldDenominator));
    const auto relative = [&] (double frequency)
    { return prototype(frequency, rate, classical, false)
        / prototype(frequency, rate, classical, true); };
    const double expected = 20.0 * std::log10(std::abs(relative(f0 * numerator)
        / relative(f0 * denominator)));
    // The listener rejected the first presence-only refinement. Require
    // enough native contrast change to exclude its 2-3 dB treble reduction.
    expect(delta < (classical ? -5.0 : -2.0),
           "native note retained the previous bass/presence harmonic imbalance");
    expect(std::abs(delta - expected) < .30,
           "native note balance disagrees with the independent capture transfer ratio");
    std::cout << "note " << note << " rate " << rate << " wood " << int(wood)
              << " style " << int(style) << " capture " << int(capture)
              << " harmonic contrast delta " << delta << " dB; expected " << expected << '\n';
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--old-capture-negative-control")
        previousCaptureNegativeControl = true;
    else if (argc != 1)
        return 2;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        testActualFilter(rate, false);
        testActualFilter(rate, true);
        for (auto wood : { acustra::BodyMaterial::Spruce, acustra::BodyMaterial::Mahogany,
                           acustra::BodyMaterial::Maple })
            for (auto style : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Pick,
                                acustra::PickingTechnique::Thumb })
            {
                // Both microphone routes are represented across every rate,
                // wood and technique without duplicating the entire matrix.
                const auto capture = (int(wood) + int(style)) % 2 == 0
                    ? acustra::CaptureType::StereoMic : acustra::CaptureType::MonoMic;
                testNativeNote(rate, wood, style, capture, 45, 2);
                testNativeNote(rate, wood, style, capture, 69, 6);
                testNativeNote(rate, wood, style, capture, 76, 6);
            }
    }
    return failures == 0 ? 0 : 1;
}
