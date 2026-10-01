#include "DSP/AcustraEngine.h"
#include "DSP/GuitarModelData.h"
#include "DSP/MeasuredBodyData.h"
#include "DSP/MeasuredBridgeData.h"
#include "DSP/MeasuredJointBodyData.h"
#include "DSP/SteelBodyBlend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto bridge(const AcustraEngine& e) { return e.bridgeLoad_; }
    static const auto& voices(const AcustraEngine& e) { return e.voices_; }
    static float phase(const AcustraEngine& e, float f, int string)
    { return e.bridgePhaseDelay(f, string); }
    static bool rebasePending(const AcustraEngine& e)
    { return e.bridgeDerivativesCrossRelease_ || e.bridgeDerivativesCrossConfigure_; }
    static bool bodySettled(const AcustraEngine& e, BodyShape shape)
    { return e.configuredBodyShape_ == shape && e.bodyModelFade_ == 1.0f; }
    static float saddle(const AcustraEngine& e) { return e.saddleHeightRatio(); }
    static const PhysicalCalibration& calibration(const AcustraEngine& e)
    { return e.physicalCalibration_; }
    static std::array<float, 2> radiationPole(const EngineParameters& p,
                                              const PhysicalCalibration& c, int index)
    { return AcustraEngine::radiationModePole(p, c, index); }
    static std::array<int, 2> bodyCounts(const AcustraEngine& e)
    { return { e.bodyBank_.count, e.bodyBank_.ordered }; }
    static constexpr int continuationSlots = AcustraEngine::radiationContinuationSlots;
    // The radiation bank's microphones for one input sample.
    static auto body(AcustraEngine& e, float force, float moment)
    { return e.renderBody(force, moment); }
    // The body bank's digital pole in slot i, as {real, imaginary}.
    static std::array<double, 2> bodyPole(const AcustraEngine& e, int i)
    { return { e.bodyBank_.poleReal[std::size_t(i)], e.bodyBank_.poleImaginary[std::size_t(i)] }; }
    // The largest pole radius of the body bank's active modes.
    static double bodyPoleRadius(const AcustraEngine& e)
    {
        double worst = 0.0;
        for (int i = 0; i < e.bodyBank_.count; ++i)
            worst = std::max(worst, std::hypot(double(e.bodyBank_.poleReal[std::size_t(i)]),
                                               double(e.bodyBank_.poleImaginary[std::size_t(i)])));
        return worst;
    }
    // The normal port a string drains into: bridge and anchors in parallel.
    static std::complex<float> port(const AcustraEngine& e, float f, int string)
    { return e.bridgePortMobility(f, string).normal; }
    // The frequencies of the tuning table's first `ordered` modes: steel's
    // own bridge modes (B's), which sit on their radiation's poles.
    static std::vector<float> ownBridgePoles(const AcustraEngine& e)
    {
        const auto& table = e.bridgeMobilityTable();
        const float rate = static_cast<float>(e.sampleRate_);
        std::vector<float> poles;
        for (int i = 0; i < table.ordered; ++i)
            poles.push_back(rate / std::numbers::pi_v<float> * std::atan(
                table.modes[std::size_t(i)].omega / (2.0f * rate)));
        return poles;
    }
    // Leaves only B's own modes in the tuning table, at full level (each
    // mode's blended residues over its `shares` entry): the bridge B+D would
    // play with this build's Q. The table is a cache keyed on its inputs, so
    // it stays.
    static void keepOwnBridgeOnly(AcustraEngine& e, const std::vector<float>& shares)
    {
        const auto& cached = e.bridgeMobilityTable();
        auto& table = e.bridgeMobilityTable_;
        (void) cached;
        table.count = table.ordered;
        for (int i = 0; i < table.ordered; ++i)
        {
            auto& mode = table.modes[std::size_t(i)];
            const float share = shares[std::size_t(i)];
            mode.heave /= share;
            mode.cross /= share;
            mode.rock /= share;
        }
    }
};
}

namespace
{
int failures = 0;
void expect(bool pass, const char* message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Complex = std::complex<double>;
using Matrix = std::array<Complex, 3>;
constexpr double pi = std::numbers::pi;

acustra::BodyShape nativeShape(acustra::GuitarModel model)
{
    return model == acustra::GuitarModel::Original
        ? acustra::BodyShape::Dreadnought : acustra::BodyShape::Auditorium;
}

// Read the ACTUAL configured digital section, including float rounding and
// the floor. This does not reuse the engine's analog mode/shape formula.
Matrix mobility(const auto& bridge, double frequency, double rate)
{
    const Complex z = std::polar(1.0, -2*pi*frequency/rate);
    Matrix result { bridge.immediateHeave, bridge.immediateCross,
                    bridge.immediateRock };
    for (std::size_t i = 0; i < bridge.heaveModes.size(); ++i)
    {
        const auto& mode = bridge.heaveModes[i];
        const auto past = (double(mode.numerator1)*z
            + double(mode.numerator2)*z*z)
            / (1.0+double(mode.denominator1)*z+double(mode.denominator2)*z*z);
        result[0] += double(bridge.residueHeave[i])*past;
        result[1] += double(bridge.residueCross[i])*past;
        result[2] += double(bridge.residueRock[i])*past;
    }
    return result;
}

double expectedPhase(const Engine& e, double frequency, int string, double rate)
{
    const auto y = mobility(Access::bridge(e), frequency, rate);
    const Complex s(0, 2*rate*std::tan(pi*frequency/rate));
    std::array<double, 3> stiffness {};
    // The anchor stubs also hold the saddle crown sideways, which the
    // parallel polarisation's rocking port adds as (h/a)^2 of each stub.
    const double eta = Access::saddle(e);
    for (int i = 0; i < 6; ++i)
    {
        const double u = (i-2.5)/2;
        const double k = Access::voices(e)[i].bridgeTailStiffness;
        stiffness[0] += k; stiffness[1] += u*k; stiffness[2] += (u*u+eta*eta)*k;
    }
    // Apply a unit force at this string and solve (I+Y*K/s)*motion=Y*[1,u].
    // This direct solve works for both rank-one and rocking banks, without
    // the engine's determinant-of-Y reduction or scalar special case.
    const double u = (string-2.5)/2;
    const Complex a = 1.0+(y[0]*stiffness[0]+y[1]*stiffness[1])/s;
    const Complex b = (y[0]*stiffness[1]+y[1]*stiffness[2])/s;
    const Complex c = (y[1]*stiffness[0]+y[2]*stiffness[1])/s;
    const Complex d = 1.0+(y[1]*stiffness[1]+y[2]*stiffness[2])/s;
    const Complex r0 = y[0]+u*y[1], r1 = y[1]+u*y[2];
    const Complex effective = ((d*r0-b*r1)+u*(a*r1-c*r0))/(a*d-b*c);
    const double port = 1.0/Access::voices(e)[string].characteristicImpedance;
    return -std::arg((port-effective)/(port+effective))/(2*pi*frequency/rate);
}

void testPhaseAndPassivity()
{
    double worstCents = 0, minimumReal = 0;
    std::array<int,6> worstCase {};
    for (int model = 0; model < 2; ++model)
        for (int rate : { 8000, 24000, 48000, 96000 })
        {
            acustra::EngineParameters p;
            p.guitarModel = static_cast<acustra::GuitarModel>(model);
            p.shape = nativeShape(p.guitarModel);
            auto e = std::make_unique<Engine>();
            e->setParameters(p); e->prepare(rate, 64);
            const auto reference = Access::bridge(*e);
            for (int shape = 0; shape < 4; ++shape)
            {
                p.shape = static_cast<acustra::BodyShape>(shape);
                e->setParameters(p);
                // A change inside the previous one's 20 ms bridge fade waits
                // for it to end, so let each settle before reading it.
                std::vector<float> left(64), right(64);
                for (int done = 0; done < rate / 20; done += 64)
                    e->process(left.data(), right.data(), 64);
                const auto actual = Access::bridge(*e);
                // The authored high-band floor has no measured geometry
                // map, so even non-native shapes must preserve it exactly.
                const auto last = actual.heaveModes.size()-1;
                expect(actual.heaveModes[last].denominator1
                           == reference.heaveModes[last].denominator1
                    && actual.heaveModes[last].denominator2
                           == reference.heaveModes[last].denominator2
                    && actual.residueHeave[last] == reference.residueHeave[last],
                    "shape changed the unmapped conductance floor");
                for (int string = 0; string < 6; ++string)
                    for (int fret : { 0, 7, 19 })
                    {
                        const double f = 440*std::exp2(
                            (Access::voices(*e)[string].openMidi+fret-69)/12.0);
                        const double error = std::abs(expectedPhase(*e,f,string,rate)
                            -Access::phase(*e,static_cast<float>(f),string));
                        const double cents = 1200/std::log(2.0)*error*f/rate;
                        if (cents > worstCents)
                        {
                            worstCents = cents;
                            worstCase = {model,rate,shape,string,fret,
                                         static_cast<int>(std::round(f))};
                        }
                    }
                for (int bin = 0; bin < 60; ++bin)
                {
                    const auto y = mobility(actual,
                        60*std::pow(std::min(10000.0,.40*rate)/60,bin/59.0),rate);
                    for (int string = 0; string < 6; ++string)
                    {
                        const double u = (string-2.5)/2;
                        const double real = (y[0]+2*u*y[1]+u*u*y[2]).real();
                        minimumReal = std::min(minimumReal,real);
                        expect(real >= -2e-6,
                               "shaped digital bridge acquired active mobility");
                    }
                }
            }
        }
    std::cout << "shape actual-biquad phase error cents=" << worstCents
              << " minimum mobility real=" << minimumReal << " case=";
    for (int field : worstCase) std::cout << field << ',';
    std::cout << '\n';
    // The analytic phase approximation and rounded float biquads already
    // differ by 0.289307 cents in the frozen native-bank baseline. Shape's
    // current worst case is 0.492829 cents. Keep a sub-cent reconstruction
    // budget; the separate end-to-end note-tuning gates are unchanged.
    expect(worstCents < 1.0, "shape tuning differs from its actual digital bridge");
}

void process(Engine& e, int frames, double* energy = nullptr, float* peak = nullptr);
double sectionFrequency(const auto& mode, double rate);

// The denominators configureBridge builds from a mode's float frequency and
// Q, in the engine's double arithmetic (prewarped bilinear, 2*damping = w/q).
std::array<double, 2> denominators(float frequency, float q, double rate)
{
    const double bilinear = 2.0*rate;
    const double omega = bilinear*std::tan(double(3.14159265358979323846f)*frequency/rate);
    const double damping = omega/(2.0*q);
    const double d0 = bilinear*bilinear + 2.0*damping*bilinear + omega*omega;
    return { (-2.0*bilinear*bilinear + 2.0*omega*omega)/d0,
             (bilinear*bilinear - 2.0*damping*bilinear + omega*omega)/d0 };
}

// Steel's own bridge is g21's, the radiation's guitar: every bridge mode that
// is the same resonance as a radiation mode (the generator's twin) takes that
// mode's engine pole, frequency and Q to the float, under every Shape and
// Wood, so a partial on a radiation peak meets the same conductance peak.
// The anchor no longer leaves this bridge as fitted: the wide steel anchor
// and the plate-Q rule move it with the radiation.
void testSteelOwnBridgeSharesTheRadiationPoles()
{
    const auto& twins = acustra::detail::steelBridgeRadiationTwins;
    int paired = 0;
    for (const auto twin : twins) paired += twin >= 0;
    expect(paired == 18, "steel's own bridge does not twin its 18 resolved modes");
    double worst = 0;
    for (int shape = 0; shape < 4; ++shape)
        for (int wood = 0; wood < 3; ++wood)
        {
            acustra::EngineParameters p;
            p.shape = static_cast<acustra::BodyShape>(shape);
            p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
            auto e = std::make_unique<Engine>();
            e->setParameters(p); e->prepare(48000, 64);
            const auto bridge = Access::bridge(*e);
            for (std::size_t i = 0; i < twins.size(); ++i)
            {
                if (twins[i] < 0) continue;
                const auto pole = Access::radiationPole(p, Access::calibration(*e), twins[i]);
                const auto expected = denominators(pole[0], pole[1], 48000.0);
                const auto& mode = bridge.heaveModes[i];
                worst = std::max({ worst,
                    std::abs(mode.denominator1-expected[0])/std::abs(expected[0]),
                    std::abs(mode.denominator2-expected[1])/std::abs(expected[1]) });
            }
            // Changing Wood live reaches the bridge as a fresh engine does.
            auto live = std::make_unique<Engine>();
            auto first = p; first.bodyMaterial = acustra::BodyMaterial::Spruce;
            live->setParameters(first); live->prepare(48000, 64);
            live->setParameters(p); process(*live, 64);
            const auto liveBridge = Access::bridge(*live);
            bool same = true;
            for (std::size_t i = 0; i < bridge.heaveModes.size(); ++i)
                same = same && liveBridge.heaveModes[i].denominator1 == bridge.heaveModes[i].denominator1
                            && liveBridge.heaveModes[i].denominator2 == bridge.heaveModes[i].denominator2;
            expect(same, "a live Wood change did not move steel's own bridge");
        }
    std::cout << "steel own bridge twin pole relative error=" << worst << '\n';
    // A one-ulp change of a float frequency or Q moves these by ~1e-7.
    expect(worst < 1e-12, "a twinned bridge mode is not its radiation mode's pole");
}

// The Bellido's bridge belongs to its radiation's guitar too: it keeps each
// mode that is the same resonance as a radiation mode at its measured offset
// from it, under every Shape and Wood (steel's own bridge takes its twin's
// pole, above). Before, its bridge ignored Wood: a Maple Bellido's drains sat
// 60-85 cents under their radiation peaks (audit F10). The twins are the
// generator's test (GenerateMeasuredBridge.py), recomputed here from the banks.
void testBellidoBridgeKeepsItsDrainsOnItsRadiation()
{
    using namespace acustra::detail;
    const std::vector<MeasuredBridgeMode> bridgeModes(bellidoBridgeModes.begin(), bellidoBridgeModes.end());
    const std::vector<MeasuredBodyMode> radiation(bellidoBodyModes.begin(), bellidoBodyModes.end());
    std::vector<int> twins;
    for (const auto& mode : bridgeModes)
    {
        std::size_t k = 0;
        for (std::size_t j = 1; j < radiation.size(); ++j)
            if (std::abs(double(radiation[j].frequency)-mode.frequency)
                < std::abs(double(radiation[k].frequency)-mode.frequency)) k = j;
        const double below = k > 0 ? double(radiation[k].frequency)-radiation[k-1].frequency : 1e30;
        const double above = k+1 < radiation.size() ? double(radiation[k+1].frequency)-radiation[k].frequency : 1e30;
        const double spacing = k > 0 && k+1 < radiation.size() ? .5*(below+above) : std::min(below, above);
        const bool inside = std::abs(double(mode.frequency)-radiation[k].frequency)
            < radiation[k].frequency/(2.0*radiation[k].q);
        twins.push_back(inside && double(mode.frequency)/mode.q < spacing ? int(k) : -1);
    }
    int paired = 0;
    for (int twin : twins) paired += twin >= 0;
    expect(paired >= 9, "the Bellido's bridge twins too few of its modes");
    // The measured offsets, which every Shape and Wood keeps.
    std::vector<double> measuredOffset(twins.size(), 0.0);
    for (std::size_t i = 0; i < twins.size(); ++i)
        if (twins[i] >= 0)
            measuredOffset[i] = 1200*std::log2(double(bridgeModes[i].frequency)
                                               / radiation[std::size_t(twins[i])].frequency);
    double worst = 0;
    for (int shape = 0; shape < 4; ++shape)
        for (int wood = 0; wood < 3; ++wood)
        {
            acustra::EngineParameters p;
            p.guitarModel = acustra::GuitarModel::Bellido1978;
            p.shape = static_cast<acustra::BodyShape>(shape);
            p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
            auto e = std::make_unique<Engine>();
            e->setParameters(p); e->prepare(48000, 64);
            const auto bridge = Access::bridge(*e);
            double sum = 0;
            for (std::size_t i = 0; i < twins.size(); ++i)
            {
                if (twins[i] < 0) continue;
                const double engine = 1200*std::log2(sectionFrequency(bridge.heaveModes[i], 48000)
                    / Access::radiationPole(p, Access::calibration(*e), twins[i])[0]);
                worst = std::max(worst, std::abs(engine-measuredOffset[i]));
                sum += std::abs(engine);
            }
            if (shape == 1 && (wood == 1 || wood == 2))
                std::cout << "Bellido" << (wood == 1 ? " Auditorium/Mahogany" : " Auditorium/Maple")
                          << ": mean |bridge - radiation twin| " << sum/paired << " cents over "
                          << paired << " pairs\n";
            // Changing Wood live reaches the bridge as a fresh engine does.
            auto live = std::make_unique<Engine>();
            auto first = p; first.bodyMaterial = acustra::BodyMaterial::Spruce;
            live->setParameters(first); live->prepare(48000, 64);
            live->setParameters(p); process(*live, 64);
            const auto liveBridge = Access::bridge(*live);
            bool same = true;
            for (std::size_t i = 0; i < bridge.heaveModes.size(); ++i)
                same = same && liveBridge.heaveModes[i].denominator1 == bridge.heaveModes[i].denominator1
                            && liveBridge.heaveModes[i].denominator2 == bridge.heaveModes[i].denominator2;
            expect(same, "a live Wood change did not move the Bellido's bridge");
        }
    std::cout << "Bellido bridge's twin offsets vs its measurement, worst cents=" << worst << '\n';
    expect(worst < 0.01, "a bridge drain left its radiation twin under Shape or Wood");
}

// The analog frequency a configured bridge section was designed at, from its
// prewarped bilinear denominators: 1+d1+d2 = 4w^2/d0 and 1-d1+d2 = 4B^2/d0.
double sectionFrequency(const auto& mode, double rate)
{
    const double ratio = (1.0+mode.denominator1+mode.denominator2)
                       / (1.0-mode.denominator1+mode.denominator2);
    return rate/pi*std::atan(std::sqrt(ratio));
}

// Shape moves a bridge bank by the same three class factors (A0 group, T1
// group, plate) as the radiation it belongs to, so each bridge mode must move
// as the radiation mode beside it does: the nearest one in log frequency, on
// the same side of the A0 group's 150 Hz limit. The Bellido's bridge T1
// (216.4 Hz, just over its radiation T1 at 212.2 Hz) took the plate factor
// instead and landed 0.8-1.2 semitones off it on Parlor, Dreadnought and
// Jumbo (audit F9).
void testBridgeModesMoveWithTheirRadiationNeighbours()
{
    struct Bank { acustra::GuitarModel model; const char* name; };
    const Bank banks[] {
        { acustra::GuitarModel::Original, "steel own" },
        { acustra::GuitarModel::Bellido1978, "Bellido" },
    };
    const auto bridgeBank = [] (const Bank& b)
    {
        using namespace acustra::detail;
        if (b.model == acustra::GuitarModel::Bellido1978)
            return std::vector<MeasuredBridgeMode>(bellidoBridgeModes.begin(), bellidoBridgeModes.end());
        return std::vector<MeasuredBridgeMode>(measuredSteelBridgeModes.begin(), measuredSteelBridgeModes.end());
    };
    const auto radiationBank = [] (const Bank& b)
    {
        using namespace acustra::detail;
        std::vector<float> f;
        if (b.model == acustra::GuitarModel::Bellido1978)
            for (const auto& m : bellidoBodyModes) f.push_back(m.frequency);
        else
            for (const auto& m : measuredSteelBodyModes) f.push_back(m.frequency);
        return f;
    };
    double worst = 0;
    const char* worstBank = "";
    int worstShape = -1;
    double worstHz = 0;
    for (const auto& b : banks)
    {
        const auto bridgeModes = bridgeBank(b);
        const auto radiation = radiationBank(b);
        acustra::EngineParameters p;
        p.guitarModel = b.model;
        p.shape = nativeShape(b.model);
        const auto anchorShape = p.shape;
        auto anchor = std::make_unique<Engine>();
        anchor->setParameters(p); anchor->prepare(48000, 64);
        const auto anchorBridge = Access::bridge(*anchor);
        for (int shape = 0; shape < 4; ++shape)
        {
            if (static_cast<acustra::BodyShape>(shape) == anchorShape) continue;
            auto q = p; q.shape = static_cast<acustra::BodyShape>(shape);
            auto e = std::make_unique<Engine>();
            e->setParameters(q); e->prepare(48000, 64);
            const auto bridge = Access::bridge(*e);
            for (std::size_t i = 0; i < bridgeModes.size(); ++i)
            {
                const double measured = bridgeModes[i].frequency;
                if (measured > 0.4*48000) continue;
                std::size_t nearest = 0;
                for (std::size_t j = 1; j < radiation.size(); ++j)
                    if (std::abs(std::log(radiation[j]/measured))
                        < std::abs(std::log(radiation[nearest]/measured)))
                        nearest = j;
                if ((measured < 150.0) != (radiation[nearest] < 150.0f)) continue;
                const double bridgeCents = 1200*std::log2(
                    sectionFrequency(bridge.heaveModes[i], 48000)
                    / sectionFrequency(anchorBridge.heaveModes[i], 48000));
                const double radiationCents = 1200*std::log2(
                    double(Access::radiationPole(q, Access::calibration(*e), int(nearest))[0])
                    / Access::radiationPole(p, Access::calibration(*anchor), int(nearest))[0]);
                const double gap = std::abs(bridgeCents-radiationCents);
                if (gap > worst) { worst = gap; worstBank = b.name; worstShape = shape; worstHz = measured; }
            }
        }
    }
    std::cout << "bridge vs neighbouring radiation shape move, worst cents=" << worst
              << " (" << worstBank << ", shape " << worstShape << ", " << worstHz << " Hz)\n";
    expect(worst < 0.5, "Shape moved a bridge mode by another class than its radiation neighbour");
}

// The steel blend (SteelBodyBlend.h): on steel's own bridge the bridge is
// B's aligned modes and the joint-pole body's modes below its band, each at
// its share, and on the Original guitar the radiation is g21's
// bank and the joint body's radiation below the band. Every section keeps a
// positive semidefinite residue matrix, so each string's sum stays positive
// real (testPhaseAndPassivity reads the digital bridge, and below for every
// Shape and Wood); here the parts, their levels and a long ring and a
// sustained strum, which must not grow.
void testSteelBlend()
{
    namespace d = acustra::detail;
    const double w = d::steelBlendJointBodyWeight;
    const bool joint = w > 0.0;
    std::size_t jointModes = 0, jointBridge = 0;
    for (const auto& mode : d::measuredSteelJointBodyModes)
        if (joint && mode.frequency < d::steelBlendJointBandHz)
        {
            ++jointModes;
            jointBridge += mode.heave > 0.0f || mode.rock > 0.0f;
        }
    const std::size_t own = d::measuredSteelBridgeModes.size();
    const int body = int(d::measuredSteelBodyModes.size() + jointModes);
    // B's share of its own mode at measured frequency hz: (1 - E) below E's
    // band, whole above it.
    const auto ownShare = [&] (float hz)
    {
        return float(hz < d::steelBlendJointBandHz ? 1.0 - w : 1.0);
    };

    acustra::EngineParameters p;
    auto e = std::make_unique<Engine>();
    e->setParameters(p); e->prepare(48000, 64);
    const auto bridge = Access::bridge(*e);
    const double scale = Access::calibration(*e).bridgeMobilityScale;
    expect(bridge.activeModeCount == int(own + jointBridge) + 1,
           "the blended bridge does not play each part's modes and the plate floor once");
    // Then the slots continuing the radiation above the fitted band.
    expect(Access::bodyCounts(*e)[0] == body + Access::continuationSlots
               && Access::bodyCounts(*e)[1] == int(d::measuredSteelBodyModes.size()),
           "the blended radiation does not play each part, g21's bank in order "
           "first, and its continuation");
    double worstLevel = 0.0, worstDefinite = 0.0;
    const auto level = [&] (std::size_t slot, const d::MeasuredBridgeMode& source, double share)
    {
        const double expected = source.heave * scale * share;
        worstLevel = std::max(worstLevel,
            std::abs(bridge.residueHeave[slot] - expected) / expected);
    };
    for (std::size_t i = 0; i < own; ++i)
        level(i, d::measuredSteelBridgeModes[i],
              ownShare(d::measuredSteelBridgeModes[i].frequency) * d::steelTopMobilityRatio);
    for (std::size_t i = 0, slot = own; i < d::measuredSteelJointBodyModes.size() && joint; ++i)
    {
        const auto& mode = d::measuredSteelJointBodyModes[i];
        if (!(mode.frequency < d::steelBlendJointBandHz) || !(mode.heave > 0.0f || mode.rock > 0.0f))
            continue;
        if (mode.heave > 0.0f)
            level(slot, { mode.frequency, mode.q, mode.heave, mode.cross, mode.rock },
                  w * d::steelJointTopMobilityRatio);
        ++slot;
    }
    for (std::size_t i = 0; i < bridge.heaveModes.size(); ++i)
    {
        const double h = bridge.residueHeave[i], c = bridge.residueCross[i], r = bridge.residueRock[i];
        expect(h >= 0.0 && r >= 0.0, "a blended bridge section has a negative residue");
        // Rank-one sections (h r = c^2) carry float rounding, 1e-7 of h r.
        if (h > 0.0 && r > 0.0)
            worstDefinite = std::min(worstDefinite, (h * r - c * c) / (h * r));
        else
            expect(c == 0.0, "a blended bridge section couples without rocking");
    }
    std::cout << "steel blend: " << bridge.activeModeCount - 1 << " bridge modes, "
              << Access::bodyCounts(*e)[0] << " radiation modes, worst part level error "
              << worstLevel << ", least relative residue determinant " << worstDefinite << '\n';
    expect(worstLevel < 1e-5, "a blended bridge part is not at its share");
    expect(worstDefinite >= -1e-6, "a blended bridge section is not positive semidefinite");

    // A passive part in parallel is not a linear share at the string: the
    // string drains into the port, (Y^-1 + K/s)^-1 with the anchors' K, whose
    // peaks sit at the summed Y's zeros, so a part ringing at other
    // frequencies moves them. At each of B's aligned poles the port must keep
    // at least that mode's share of the conductance B's modes alone give it
    // (another guitar's measured bridge, unaligned, blended in at 0.3 left
    // 0.26-0.49 of it at 100 and 178-190 Hz; Docs/decisions.md).
    {
        std::vector<float> shares;
        for (const auto& mode : d::measuredSteelBridgeModes)
            shares.push_back(ownShare(mode.frequency));
        const auto poles = Access::ownBridgePoles(*e);
        std::vector<std::array<float, 6>> blended(poles.size());
        for (std::size_t k = 0; k < poles.size(); ++k)
            for (int s = 0; s < 6; ++s)
                blended[k][std::size_t(s)] = Access::port(*e, poles[k], s).real();
        Access::keepOwnBridgeOnly(*e, shares);
        double worstPort = 1e9; float worstAt = 0.0f;
        for (std::size_t k = 0; k < poles.size(); ++k)
            for (int s = 0; s < 6; ++s)
            {
                const double alone = Access::port(*e, poles[k], s).real();
                if (!(alone > 0.0)) continue;
                const double ratio = blended[k][std::size_t(s)] / alone / shares[k];
                if (ratio < worstPort) { worstPort = ratio; worstAt = poles[k]; }
            }
        std::cout << "steel blend: " << poles.size() << " own-bridge poles, least port "
                  << "conductance over B's alone, per its share, " << worstPort << " at "
                  << worstAt << " Hz\n";
        expect(!poles.empty() && poles.size() == own, "the tuning table does not start with B's own modes");
        expect(worstPort >= 1.0,
               "a blended part pulls the string's drain off one of B's aligned poles");
    }

    // Every Shape and Wood: the digital bridge stays passive at every string
    // (60 Hz to 10 kHz), every bridge section and body mode is stable, and a
    // chord left to ring decays.
    {
        double least = 0.0, largestBody = 0.0, largestBridge = 0.0, worstRing = 0.0;
        for (int shape = 0; shape < 4; ++shape)
            for (int wood = 0; wood < 3; ++wood)
            {
                acustra::EngineParameters q;
                q.shape = static_cast<acustra::BodyShape>(shape);
                q.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
                q.outputGain = 0.04f;
                auto x = std::make_unique<Engine>();
                x->setParameters(q); x->prepare(48000, 64);
                const auto b = Access::bridge(*x);
                for (int bin = 0; bin < 400; ++bin)
                {
                    const auto y = mobility(b, 60 * std::pow(10000.0 / 60, bin / 399.0), 48000);
                    for (int string = 0; string < 6; ++string)
                    {
                        const double u = (string - 2.5) / 2;
                        least = std::min(least, (y[0] + 2 * u * y[1] + u * u * y[2]).real());
                    }
                }
                for (std::size_t i = 0; i < b.heaveModes.size(); ++i)
                {
                    // Roots of 1 + d1 z^-1 + d2 z^-2.
                    const Complex d1 = b.heaveModes[i].denominator1, d2 = b.heaveModes[i].denominator2;
                    const Complex root = std::sqrt(d1 * d1 - 4.0 * d2);
                    largestBridge = std::max({ largestBridge, std::abs((-d1 + root) / 2.0),
                                               std::abs((-d1 - root) / 2.0) });
                }
                largestBody = std::max(largestBody, Access::bodyPoleRadius(*x));
                for (int note : { 40, 45, 50, 55, 59, 64 }) x->noteOn(note, 1.0f);
                double first = 0.0, last = 0.0;
                process(*x, 48000, &first);
                process(*x, 48000 * 7, nullptr);
                process(*x, 48000, &last);
                worstRing = std::max(worstRing, last / first);
            }
        std::cout << "steel blend, every Shape and Wood: least string mobility real " << least
                  << ", largest bridge pole radius " << largestBridge << ", largest body pole radius "
                  << largestBody << ", ring second 9 over second 1 " << worstRing << '\n';
        expect(least >= -2e-6, "a Shape or Wood made the blended bridge active");
        expect(largestBridge < 1.0 && largestBody < 1.0, "a Shape or Wood made a blended mode unstable");
        expect(worstRing < 0.1, "a Shape or Wood's blended ring did not decay");
    }

    // A full chord left to ring for 20 s and a strum every 250 ms for 20 s:
    // no one-second window louder than every earlier one after the first
    // three, and the sustained level not creeping up.
    for (int sustained = 0; sustained < 2; ++sustained)
    {
        p.outputGain = 0.04f;
        auto ring = std::make_unique<Engine>();
        ring->setParameters(p); ring->prepare(48000, 64);
        const std::array<std::array<int, 6>, 2> chords {{ { 40, 47, 52, 56, 59, 64 },
                                                          { 43, 47, 50, 55, 59, 67 } }};
        std::vector<double> seconds;
        for (int second = 0, strum = 0; second < 20; ++second)
        {
            double energy = 0.0;
            for (int quarter = 0; quarter < 4; ++quarter)
            {
                if ((second == 0 && quarter == 0) || sustained)
                {
                    if (strum > 0)
                        for (int note : chords[std::size_t((strum - 1) % 2)]) ring->noteOff(note);
                    for (int note : chords[std::size_t(strum % 2)]) ring->noteOn(note, sustained ? 0.8f : 1.0f);
                    ++strum;
                }
                process(*ring, 12000, &energy);
            }
            seconds.push_back(energy);
        }
        double rise = 0.0, runningMax = std::max({ seconds[0], seconds[1], seconds[2] });
        for (std::size_t k = 3; k < seconds.size(); ++k)
        {
            rise = std::max(rise, seconds[k] / runningMax);
            runningMax = std::max(runningMax, seconds[k]);
        }
        const double late = sustained ? (seconds[16] + seconds[17] + seconds[18] + seconds[19])
                                          / (seconds[4] + seconds[5] + seconds[6] + seconds[7])
                                      : seconds[19] / runningMax;
        std::cout << "steel blend " << (sustained ? "sustained strums" : "free ring")
                  << ": worst window over the running maximum " << rise
                  << (sustained ? ", seconds 16-20 over 4-8 " : ", last second over loudest ")
                  << late << '\n';
        if (sustained)
            expect(late < 1.25, "the blend's level crept up under sustained strumming");
        else
            expect(rise <= 1.0 && late < 1e-2, "the blend's free ring grew or did not decay");
    }
}

// The joint-pole body's retained modes (E, below its band) under the audit's
// construction-coherence rule (Docs/decisions.md, 2026-09-29): each of them
// that carries a bridge residue must drain the strings on its own radiation
// resonance under every Shape and Wood, at 44.1, 48 and 96 kHz, and a live
// Shape or Wood change must reach the bridge as a fresh engine does. The
// bridge section is prewarped bilinear, the body mode impulse-invariant, so
// each is read back to its analog frequency and Q and compared. Every
// blended section's residue matrix stays positive semidefinite under every
// Shape and Wood (testSteelBlend checks the default), and the strings' port
// stays passive (testSteelBlend, testPhaseAndPassivity).
void testSteelBlendJointBridgeRingsOnItsRadiation()
{
    namespace d = acustra::detail;
    if (!(d::steelBlendJointBodyWeight > 0.0f))
        return;
    const std::size_t own = d::measuredSteelBridgeModes.size();
    // (body slot, bridge slot) for each retained joint mode with a bridge
    // residue: the body's kept modes follow g21's bank, the bridge's follow
    // B's.
    std::vector<std::array<int, 2>> pairs;
    {
        int body = int(d::measuredSteelBodyModes.size()), bridge = int(own);
        for (const auto& mode : d::measuredSteelJointBodyModes)
        {
            if (!(mode.frequency < d::steelBlendJointBandHz)) continue;
            if (mode.heave > 0.0f || mode.rock > 0.0f) pairs.push_back({ body, bridge++ });
            ++body;
        }
    }
    expect(pairs.size() == 8, "the retained joint body no longer drains through 8 bridge modes");
    double worstCents = 0.0, worstQ = 0.0, worstDefinite = 0.0;
    int liveMismatches = 0;
    for (int rate : { 44100, 48000, 96000 })
        for (int shape = 0; shape < 4; ++shape)
            for (int wood = 0; wood < 3; ++wood)
            {
                acustra::EngineParameters p;
                p.shape = static_cast<acustra::BodyShape>(shape);
                p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
                auto e = std::make_unique<Engine>();
                e->setParameters(p); e->prepare(rate, 64);
                const auto bridge = Access::bridge(*e);
                for (const auto& [bodySlot, bridgeSlot] : pairs)
                {
                    const auto pole = Access::bodyPole(*e, bodySlot);
                    const double radius = std::hypot(pole[0], pole[1]);
                    const double bodyHz = std::atan2(pole[1], pole[0]) * rate / (2.0 * pi);
                    const double bodyQ = -pi * bodyHz / (rate * std::log(radius));
                    const auto& mode = bridge.heaveModes[std::size_t(bridgeSlot)];
                    const double bridgeHz = sectionFrequency(mode, rate);
                    const double tangent = std::tan(pi * bridgeHz / rate);
                    const double bridgeQ = tangent * (1.0 + mode.denominator1 + mode.denominator2)
                        / (2.0 * tangent * tangent * (1.0 - mode.denominator2));
                    worstCents = std::max(worstCents, std::abs(1200.0 * std::log2(bridgeHz / bodyHz)));
                    worstQ = std::max(worstQ, std::abs(bridgeQ / bodyQ - 1.0));
                }
                for (std::size_t i = 0; i < bridge.heaveModes.size(); ++i)
                {
                    const double h = bridge.residueHeave[i], c = bridge.residueCross[i],
                                 r = bridge.residueRock[i];
                    expect(h >= 0.0 && r >= 0.0, "a blended bridge section has a negative residue");
                    if (h > 0.0 && r > 0.0)
                        worstDefinite = std::min(worstDefinite, (h * r - c * c) / (h * r));
                }
                if (rate != 48000) continue;
                // Live: from the default Dreadnought/Spruce to this Shape and Wood.
                auto live = std::make_unique<Engine>();
                live->setParameters(acustra::EngineParameters {});
                live->prepare(rate, 64);
                live->setParameters(p); process(*live, 64);
                const auto liveBridge = Access::bridge(*live);
                for (const auto& pair : pairs)
                {
                    const auto slot = std::size_t(pair[1]);
                    liveMismatches += liveBridge.heaveModes[slot].denominator1 != bridge.heaveModes[slot].denominator1
                        || liveBridge.heaveModes[slot].denominator2 != bridge.heaveModes[slot].denominator2
                        || liveBridge.residueHeave[slot] != bridge.residueHeave[slot];
                    const auto a = Access::bodyPole(*live, pair[0]), b = Access::bodyPole(*e, pair[0]);
                    liveMismatches += a[0] != b[0] || a[1] != b[1];
                }
            }
    std::cout << "steel blend joint bridge vs its radiation, every Shape, Wood and rate: worst "
              << worstCents << " cents, worst Q ratio error " << worstQ
              << ", least relative residue determinant " << worstDefinite
              << ", live mismatches " << liveMismatches << '\n';
    expect(worstCents < 0.01, "a retained joint bridge mode left its radiation resonance");
    expect(worstQ < 5e-3, "a retained joint bridge mode's Q left its radiation's");
    expect(worstDefinite >= -1e-6, "a Shape or Wood made a blended bridge section indefinite");
    expect(liveMismatches == 0, "a live Shape or Wood change did not reach the joint body");
}

void process(Engine& e, int frames, double* energy, float* peak)
{
    std::array<float,64> left {}, right {};
    while (frames > 0)
    {
        const int count = std::min(frames,64);
        e.process(left.data(),right.data(),count);
        for (int i = 0; i < count; ++i)
        {
            expect(std::isfinite(left[i]) && std::isfinite(right[i]),
                   "shape transition produced nonfinite audio");
            if (energy) *energy += double(left[i])*left[i]+double(right[i])*right[i];
            if (peak) *peak = std::max(*peak,std::max(std::abs(left[i]),std::abs(right[i])));
        }
        frames -= count;
    }
}

void testRetuneAndTailOwnership()
{
    double largestRetune = 0;
    {
        acustra::EngineParameters p;
        auto e = std::make_unique<Engine>();
        e->setParameters(p); e->prepare(48000,64); e->setStringPerChannelMode(true);
        e->noteOn(43,.8f,1); e->noteOn(60,.8f,4);
        process(*e,2400);
        e->noteOn(45,.8f,1); e->noteOn(62,.8f,4);
        process(*e,64);
        const auto before = Access::voices(*e);
        std::array<double,6> oldPhase {}, frequency {};
        int tailCount = 0;
        for (int i = 0; i < 6; ++i)
        {
            tailCount += before[i].tailActive;
            frequency[i] = 440*std::exp2((before[i].midiNote-69
                +.01*before[i].attackPitchCents)/12.0);
            oldPhase[i] = Access::phase(*e,static_cast<float>(frequency[i]),i);
        }
        expect(tailCount == 2,"retune fixture did not retain both repluck tails");
        const float forceBefore = e->getLastBridgeReactionForce();
        p.shape = acustra::BodyShape::Parlor;
        e->setParameters(p);
        const auto& after = Access::voices(*e);
        for (int i = 0; i < 6; ++i)
        {
            expect(after[i].played == before[i].played
                && after[i].midiNote == before[i].midiNote
                && after[i].ownerCount == before[i].ownerCount,
                "shape changed note ownership");
            expect(after[i].loops[0].delay == before[i].loops[0].delay
                && after[i].loops[0].currentDelay == before[i].loops[0].currentDelay,
                "shape reset or jumped the stored string wave");
            expect(after[i].tailActive == before[i].tailActive
                && after[i].tailLoop.delay == before[i].tailLoop.delay
                && after[i].tailDamping == before[i].tailDamping
                && after[i].tailCharacteristicImpedance == before[i].tailCharacteristicImpedance,
                "shape discarded or changed a retained tail's own state");
            // Both loops also carry the coupled pair's detune, which follows
            // the new bridge too; the phase is taken on the bare length.
            const double expected = (before[i].loops[0].targetDelay
                    / (1.0+before[i].polarisationDetune)+oldPhase[i]
                -Access::phase(*e,static_cast<float>(frequency[i]),i))
                * (1.0+after[i].polarisationDetune);
            expect(std::abs(after[i].loops[0].targetDelay-expected)<.002,
                "shape did not retune an active or idle string to its new bridge");
            largestRetune = std::max(largestRetune,
                std::abs(double(after[i].loops[0].targetDelay-before[i].loops[0].targetDelay)));
        }
        expect(Access::rebasePending(*e),"shape forgot the bridge derivative transition");
        process(*e,1);
        expect(!Access::rebasePending(*e),"bridge derivative transition did not finish");
        // A step at a newly established load is not instantaneous bridge
        // motion: an omitted rebase emits the entire step as a force. Nor
        // did the bridge stop: the rebase carries the force it had on, where
        // it once reported none, a one-sample hole heard as a tick (F14).
        expect(std::abs(e->getLastBridgeReactionForce())
                   <= 2.0f*std::abs(forceBefore)+1e-6f
               && (forceBefore == 0.0f || e->getLastBridgeReactionForce() != 0.0f),
               "shape emitted the load change as a force impulse or dropped the force");
        process(*e,9600);
        for (const auto& voice : Access::voices(*e))
            expect(std::abs(voice.loops[0].targetDelay-voice.loops[0].currentDelay)<.02f,
                   "string delay did not settle after a shape retune");
        e->noteOn(67,.75f,6);
        expect(Access::voices(*e)[5].midiNote == 67,
               "new note did not use the selected shape instrument");
        auto fresh = std::make_unique<Engine>();
        fresh->setParameters(p); fresh->prepare(48000,64);
        fresh->setStringPerChannelMode(true); fresh->noteOn(67,.75f,6);
        expect(Access::voices(*e)[5].loops[0].targetDelay
                   == Access::voices(*fresh)[5].loops[0].targetDelay,
               "new note retained the previous shape's tuning compensation");
    }
    std::cout << "largest immediate shape delay-target change samples=" << largestRetune << '\n';
    expect(largestRetune>.001,"shape changed no actual string delay targets");
}

// A construction switched under a ringing chord must not tick: above 5 kHz,
// the largest 2 ms RMS in the 100 ms after the switch stays within 6 dB of
// the louder of the two steady settings, or under -95 dBFS where both are
// quieter than that. Rebuilding the bridge used to zero every mode and to
// report no bridge motion for a sample, 20-48 dB over both (audit F14).
struct SwitchCase
{
    const char* name;
    acustra::EngineParameters from;
    acustra::EngineParameters to;
    double allowedDb;
};

std::vector<float> renderSwitch(const acustra::EngineParameters& from,
                                const acustra::EngineParameters* to,
                                int rate, double switchAt, double seconds)
{
    auto e = std::make_unique<Engine>();
    e->setParameters(from); e->prepare(rate, 256);
    const int n = static_cast<int>(seconds * rate);
    std::vector<float> mono(static_cast<std::size_t>(n));
    std::array<float, 256> left {}, right {};
    const int switchSample = static_cast<int>(switchAt * rate);
    e->beginStrum();
    const std::array<int, 6> chord { 40, 47, 52, 56, 59, 64 };
    for (std::size_t k = 0; k < chord.size(); ++k)
        e->noteOn(chord[k], 0.8f, 1, static_cast<int>(k) * 300, true);
    for (int pos = 0; pos < n; pos += 256)
    {
        const int count = std::min(256, n - pos);
        if (to != nullptr && switchSample >= pos && switchSample < pos + count)
            e->setParameters(*to);
        e->process(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
            mono[static_cast<std::size_t>(pos + i)] = left[static_cast<std::size_t>(i)]
                + right[static_cast<std::size_t>(i)];
    }
    return mono;
}

double highBandPeakDb(const std::vector<float>& x, int rate, double from, double to)
{
    // Fourth-order Butterworth high-pass at 5 kHz, then the largest 2 ms RMS.
    std::array<std::array<double, 5>, 2> section {};
    const std::array<double, 2> qs { 0.54119610, 1.30656296 };
    for (std::size_t k = 0; k < 2; ++k)
    {
        const double w = 2*pi*5000/rate, c = std::cos(w), a = std::sin(w)/(2*qs[k]), a0 = 1+a;
        section[k] = { (1+c)/2/a0, -(1+c)/a0, (1+c)/2/a0, -2*c/a0, (1-a)/a0 };
    }
    std::array<double, 4> z {};
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i];
        for (std::size_t k = 0; k < 2; ++k)
        {
            const auto& b = section[k];
            const double out = b[0]*v + z[2*k];
            z[2*k] = b[1]*v - b[3]*out + z[2*k+1];
            z[2*k+1] = b[2]*v - b[4]*out;
            v = out;
        }
        y[i] = v*v;
    }
    const int window = static_cast<int>(0.002*rate);
    double best = 1e-30;
    for (int start = static_cast<int>(from*rate); start + window <= static_cast<int>(to*rate); start += window/2)
    {
        double sum = 0;
        for (int i = start; i < start + window; ++i) sum += y[static_cast<std::size_t>(i)];
        best = std::max(best, sum/window);
    }
    return 10*std::log10(best);
}

void testConstructionSwitchesDoNotTick()
{
    using acustra::EngineParameters;
    using acustra::GuitarModel;
    using acustra::BodyShape;
    const auto make = [] (GuitarModel g, BodyShape s)
    {
        EngineParameters p; p.guitarModel = g; p.shape = s;
        p.outputGain = 0.2f;
        return p;
    };
    const auto steel = make(GuitarModel::Original, BodyShape::Dreadnought);
    const auto bellido = make(GuitarModel::Bellido1978, BodyShape::Auditorium);
    const auto with = [] (EngineParameters p, auto change) { change(p); return p; };
    const std::vector<SwitchCase> cases {
        { "steel Shape", steel, with(steel, [] (auto& p) { p.shape = BodyShape::Parlor; }), 6.0 },
        { "steel Wood", steel, with(steel, [] (auto& p) { p.bodyMaterial = acustra::BodyMaterial::Maple; }), 6.0 },
        { "steel Model", steel, with(steel, [] (auto& p) { p.guitarModel = GuitarModel::Bellido1978; }), 6.0 },
        { "Bellido Model", bellido, with(bellido, [] (auto& p) { p.guitarModel = GuitarModel::Original; }), 6.0 },
        { "Bellido Shape", bellido, with(bellido, [] (auto& p) { p.shape = BodyShape::Parlor; }), 6.0 },
        { "steel Tuning", steel, with(steel, [] (auto& p) { p.tuning = acustra::Tuning::Dadgad; }), 6.0 },
    };
    double worst = -1e9;
    const char* worstName = "";
    for (int rate : { 44100, 48000 })
        for (const auto& c : cases)
        {
            const double at = std::ceil(0.5*rate/256)*256/rate;
            const auto a = renderSwitch(c.from, nullptr, rate, at, at + 0.11);
            const auto b = renderSwitch(c.to, nullptr, rate, at, at + 0.11);
            const auto switched = renderSwitch(c.from, &c.to, rate, at, at + 0.11);
            const double steady = std::max({ highBandPeakDb(a, rate, at, at + 0.1),
                                             highBandPeakDb(b, rate, at, at + 0.1), -95.0 });
            const double excess = highBandPeakDb(switched, rate, at, at + 0.1) - steady;
            if (excess - c.allowedDb > worst) { worst = excess - c.allowedDb; worstName = c.name; }
            expect(excess <= c.allowedDb,
                   (std::string(c.name) + " switched under a chord ticked "
                    + std::to_string(excess) + " dB over its steady sound above 5 kHz").c_str());
        }
    std::cout << "construction switch tick, worst margin over its bound " << worst
              << " dB (" << worstName << ")\n";
}

// A note near a strong, lossy low bridge or body mode is pulled a few cents
// from its request (README, Known gaps; audit F15): the loop is tuned to
// the bridge's reflection phase at the note, not to the damped pole. Keep
// that within 5 cents on the notes that pull most, measured as sustained
// pitch (1-2 s) against the same note with the bridge decoupled.
double sustainedPitchHz(const acustra::EngineParameters& p, int note, bool coupled)
{
    auto e = std::make_unique<Engine>();
    e->setParameters(p); e->prepare(48000, 256);
    e->setBridgeCouplingEnabled(coupled);
    e->noteOn(note, 0.8f);
    const int n = static_cast<int>(2.0 * 48000);
    std::vector<double> x(static_cast<std::size_t>(n));
    std::array<float, 256> left {}, right {};
    for (int pos = 0; pos < n; pos += 256)
    {
        e->process(left.data(), right.data(), 256);
        for (int i = 0; i < 256 && pos + i < n; ++i)
            x[static_cast<std::size_t>(pos + i)] = double(left[static_cast<std::size_t>(i)])
                + right[static_cast<std::size_t>(i)];
    }
    const int a = 48000, b = n;
    const auto power = [&] (double f)
    {
        Complex sum {};
        for (int i = a; i < b; ++i)
        {
            const double w = 0.5 - 0.5*std::cos(2*pi*(i-a)/(b-a));
            sum += w * x[static_cast<std::size_t>(i)] * std::polar(1.0, -2*pi*f*i/48000.0);
        }
        return std::norm(sum);
    };
    const double nominal = 440*std::exp2((note-69)/12.0);
    double best = nominal, bestPower = -1;
    for (int cents = -20; cents <= 20; ++cents)
    {
        const double f = nominal*std::exp2(cents/1200.0);
        const double pw = power(f);
        if (pw > bestPower) { bestPower = pw; best = f; }
    }
    double low = best*std::exp2(-1/1200.0), high = best*std::exp2(1/1200.0);
    for (int step = 0; step < 30; ++step)
    {
        const double m1 = low + 0.382*(high-low), m2 = low + 0.618*(high-low);
        if (power(m1) > power(m2)) high = m2; else low = m1;
    }
    return 0.5*(low+high);
}

void testCoupledPitchPullIsBounded()
{
    struct Case { const char* name; acustra::BodyShape shape; acustra::BodyMaterial wood; int note; };
    const Case cases[] {
        { "steel Jumbo G3", acustra::BodyShape::Jumbo, acustra::BodyMaterial::Spruce, 55 },
    };
    double worst = 0;
    for (const auto& c : cases)
    {
        acustra::EngineParameters p;
        p.shape = c.shape;
        p.bodyMaterial = c.wood;
        const double cents = 1200*std::log2(sustainedPitchHz(p, c.note, true)
                                            / sustainedPitchHz(p, c.note, false));
        std::cout << "coupled pitch pull, " << c.name << ": " << cents << " cents\n";
        worst = std::max(worst, std::abs(cents));
    }
    expect(worst < 5.0, "the bridge pulled a note 5 cents or more from its request");
}

// Note to note, the radiated level of single notes (1 s RMS, E2-C6) is
// rougher than the open recordings': RMS deviation from a seven-note
// neighbourhood 3.0-3.7 dB finger-played against 1.8 (Eastman E1D fingered)
// to 2.4 (Iowa classical mf), the deepest one-note hole 7.8-11.2 dB against
// 4.4-9.3 (audit F40; README Known gaps). It is the measured bodies'
// single-point near-field microphone responses (same-sign modal pairs such
// as g21's 515 and 589 Hz leave an antiresonance under C#5 at the bridge
// microphone), not an engine fault, and it is left as measured; this keeps
// it from getting worse unnoticed. The capture voicing (2026-10-01,
// CaptureVoicingData.h) brought the steel Dreadnought from 3.28 to 2.71 dB
// and its deepest hole from 9.13 to 5.45 dB, and the bounds follow it, so
// a change that undoes that gain fails here.
void testNoteToNoteLevelSpreadIsBounded()
{
    struct Case { const char* name; acustra::BodyShape shape; acustra::BodyMaterial wood; float velocity; };
    const Case cases[] {
        { "steel Dreadnought", acustra::BodyShape::Dreadnought, acustra::BodyMaterial::Spruce, 91.0f/127.0f },
    };
    for (const auto& c : cases)
    {
        acustra::EngineParameters p;
        p.shape = c.shape; p.bodyMaterial = c.wood;
        std::array<double, 45> level {};
        for (int note = 40; note <= 84; ++note)
        {
            auto e = std::make_unique<Engine>();
            e->setParameters(p); e->prepare(48000, 256);
            e->noteOn(note, c.velocity);
            double energy = 0;
            process(*e, 48000, &energy);
            level[static_cast<std::size_t>(note-40)] = 10*std::log10(energy/48000);
        }
        double squares = 0; int count = 0; double hole = 0;
        for (int i = 0; i < 45; ++i)
        {
            double sum = level[static_cast<std::size_t>(i)]; int n = 1;
            for (int j = std::max(0, i-3); j <= std::min(44, i+3); ++j)
                if (j != i) { sum += level[static_cast<std::size_t>(j)]; ++n; }
            if (n >= 4) { const double d = level[static_cast<std::size_t>(i)] - sum/n; squares += d*d; ++count; }
            if (i > 0 && i < 44)
                hole = std::max(hole, 0.5*(level[static_cast<std::size_t>(i-1)]
                    + level[static_cast<std::size_t>(i+1)]) - level[static_cast<std::size_t>(i)]);
        }
        const double rough = std::sqrt(squares/count);
        std::cout << "note-to-note level, " << c.name << ": rough " << rough
                  << " dB, deepest hole " << hole << " dB\n";
        expect(rough < 3.2, "note-to-note level grew rougher than 3.2 dB");
        expect(hole < 8.0, "a single note fell 8 dB or more under its neighbours");
    }
}

void testStaticWorkAndRapidChanges()
{
    double minimumWork = 0;
    float maximumPeak = 0;
    for (int rate : { 8000, 48000, 96000 })
        for (int capture = 0; capture < 2; ++capture)
        {
            acustra::EngineParameters p;
            p.capture = capture == 0 ? acustra::CaptureType::StereoMic
                                    : acustra::CaptureType::Piezo;
            for (int shape = 0; shape < 4; ++shape)
            {
                p.shape = static_cast<acustra::BodyShape>(shape);
                auto e = std::make_unique<Engine>(); e->setParameters(p); e->prepare(rate,64);
                for (int note : {40,47,52,55,59,64}) e->noteOn(note,.82f);
                double work = 0;
                for (int n = 0; n < rate/8; ++n)
                {
                    float left,right; e->process(&left,&right,1);
                    work += e->getLastBridgeBodyPower()/rate;
                    minimumWork = std::min(minimumWork,work);
                    expect(std::isfinite(left) && std::isfinite(right),"shaped chord was nonfinite");
                    maximumPeak = std::max(maximumPeak,std::max(std::abs(left),std::abs(right)));
                }
                expect(work>=-1e-12,"static shape body generated net energy");
                for (int next : { 3,0,2,1,3,0 })
                {
                    p.shape = static_cast<acustra::BodyShape>(next);
                    e->setParameters(p); process(*e,64,nullptr,&maximumPeak);
                }
                process(*e,rate/8,nullptr,&maximumPeak);
                expect(Access::bodySettled(*e,acustra::BodyShape::Parlor),
                       "rapid shape changes lost the final body selection");
                e->allSoundOff(); double remaining = 0; process(*e,512,&remaining);
                expect(remaining == 0,"shape changes left sound after panic");
            }
        }
    std::cout << "shape static minimum body work=" << minimumWork
              << " transition/capture peak=" << maximumPeak << '\n';
    expect(minimumWork>=-1e-12,"static shape body work went negative");
    expect(maximumPeak<.89f,"shape changes relied on the output limiter");
}
}

// In-place radix-2 FFT, forward.
void fft(std::vector<Complex>& data)
{
    const std::size_t size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const auto root = std::polar(1.0, -2.0 * pi / double(length));
        for (std::size_t start = 0; start < size; start += length)
        {
            Complex twiddle { 1.0, 0.0 };
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * twiddle;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                twiddle *= root;
            }
        }
    }
}

// The measured banks stop where their fit stopped (8.1 kHz on the steel
// Jumbo, 11.7 on the Parlor), and above their last mode the radiation used
// to fall 12-25 dB in one third of an octave, its 16 kHz band 23-39 dB under
// its 2-4 kHz level. configureBody continues it statistically at the bank's
// own top-octave density, falling 6 dB per octave (Docs/decisions.md,
// 2026-09-30). For every model, Shape, Wood and three rates, the body's
// radiation to both microphones, in third octaves from 1 kHz to 16 kHz (or
// the highest whole band under 0.45 fs), falls at most 8 dB from one band to
// the next, and the top band stays within 18 dB of the 2-4 kHz bands (4.9
// and 12.3 dB at worst as shipped). The continuation is built as 48 kHz
// modes and converted to the rate as the measured ones are, so its 8-16 kHz
// bands at 44.1 and 96 kHz stay within 0.3 dB of 48 kHz's (0.16 dB at
// worst; the band edge's measured modes alone moved 0.3-1.1 dB before).
void testRadiationContinuesAboveTheFittedBand()
{
    double worstStep = 0.0, worstTop = 0.0, worstRate = 0.0;
    std::vector<std::vector<double>> at48;
    for (const double rate : { 48000.0, 44100.0, 96000.0 })
        for (const auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
            for (int shape = 0; shape < 4; ++shape)
                for (int wood = 0; wood < 3; ++wood)
                {
                    acustra::EngineParameters p;
                    p.guitarModel = model;
                    p.shape = static_cast<acustra::BodyShape>(shape);
                    p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
                    auto e = std::make_unique<Engine>();
                    e->setParameters(p);
                    e->prepare(rate, 64);
                    const std::size_t size = rate > 50000.0 ? 32768 : 16384;
                    std::vector<Complex> left(size), right(size);
                    for (std::size_t n = 0; n < size; ++n)
                    {
                        const auto out = Access::body(*e, n == 0 ? 1.0f : 0.0f, 0.0f);
                        left[n] = out.left;
                        right[n] = out.right;
                    }
                    fft(left);
                    fft(right);
                    std::vector<double> levels;
                    const double edge = std::exp2(1.0 / 6.0);
                    for (int band = 0;; ++band)
                    {
                        const double centre = 1000.0 * std::exp2(band / 3.0);
                        if (centre > 16500.0 || centre * edge > 0.45 * rate)
                            break;
                        double power = 0.0;
                        int bins = 0;
                        for (auto bin = static_cast<std::size_t>(std::ceil(centre / edge * size / rate));
                             double(bin) * rate / size < centre * edge; ++bin, ++bins)
                            power += std::norm(left[bin]) + std::norm(right[bin]);
                        levels.push_back(10.0 * std::log10(power / std::max(bins, 1) + 1e-30));
                    }
                    for (std::size_t band = 1; band < levels.size(); ++band)
                        worstStep = std::max(worstStep, levels[band - 1] - levels[band]);
                    const std::size_t construction = std::size_t((int(model) * 4 + shape) * 3 + wood);
                    if (rate == 48000.0)
                        at48.push_back(levels);
                    else
                        // Bands 9-12 are 8-16 kHz.
                        for (std::size_t band = 9; band < std::min(levels.size(), at48[construction].size()); ++band)
                            worstRate = std::max(worstRate,
                                std::abs(levels[band] - at48[construction][band]));
                    // Bands 3-6 are 2-4 kHz.
                    const double reference = (levels[3] + levels[4] + levels[5] + levels[6]) / 4.0;
                    worstTop = std::max(worstTop, reference - levels.back());
                }
    std::cout << "radiation above the fitted band: worst third-octave fall " << worstStep
              << " dB, top band under 2-4 kHz by at most " << worstTop
              << " dB, 8-16 kHz at 44.1/96 kHz within " << worstRate << " dB of 48 kHz\n";
    expect(worstRate < 0.3,
           "the body's radiation above its fitted band moved with the sample rate");
    expect(worstStep < 8.0,
           "the body's radiation fell off a cliff above its fitted band");
    expect(worstTop < 18.0,
           "the body's radiation at 16 kHz is not continued from its fitted band");
}

// The plate conductance floor is the dense high-band overlap of a plate's
// driving-point response, which a modal fit loses between its overlapping
// modes; the Bellido's fit loses it as g21's does and takes the same floor
// (Docs/decisions.md, 2026-09-30). Without it its strings kept their 5-10
// kHz partials, and it played 11-15 dB over the recordings there. For every
// Shape and Wood at 48 and 96 kHz, the floor adds to the Bellido's string
// port conductance over 5-10 kHz at least 90% of what it adds to the
// Original's, and at least doubles the Bellido's own.
void testTheBellidoKeepsTheHighBandConductance()
{
    const auto meanConductance = [] (const acustra::EngineParameters& p, double rate, bool floor)
    {
        auto calibration = acustra::fittedPhysicalCalibration;
        if (!floor)
            calibration.bridgeConductanceFloor = 0.0f;
        auto e = std::make_unique<Engine>();
        e->setPhysicalCalibration(calibration);
        e->setParameters(p);
        e->prepare(rate, 64);
        double sum = 0.0;
        int count = 0;
        for (double f = 5000.0; f < 10000.0; f *= std::exp2(1.0 / 48.0), ++count)
            for (int string = 0; string < 6; ++string)
                sum += Access::port(*e, float(f), string).real() / 6.0;
        return sum / count;
    };
    double worstShare = 1e9, worstGain = 1e9;
    for (const double rate : { 48000.0, 96000.0 })
        for (int shape = 0; shape < 4; ++shape)
            for (int wood = 0; wood < 3; ++wood)
            {
                acustra::EngineParameters p;
                p.shape = static_cast<acustra::BodyShape>(shape);
                p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
                const double originalAdded = meanConductance(p, rate, true)
                    - meanConductance(p, rate, false);
                p.guitarModel = acustra::GuitarModel::Bellido1978;
                const double without = meanConductance(p, rate, false);
                const double with = meanConductance(p, rate, true);
                worstShare = std::min(worstShare, (with - without) / originalAdded);
                worstGain = std::min(worstGain, with / without);
            }
    std::cout << "Bellido 5-10 kHz port conductance: floor adds at least " << worstShare
              << " of the Original's, at least " << worstGain << "x its own\n";
    expect(worstShare > 0.9 && worstGain > 2.0,
           "the Bellido lost the plate's high-band conductance floor");
}

int main()
{
    testRadiationContinuesAboveTheFittedBand();
    testTheBellidoKeepsTheHighBandConductance();
    testSteelOwnBridgeSharesTheRadiationPoles();
    testBridgeModesMoveWithTheirRadiationNeighbours();
    testBellidoBridgeKeepsItsDrainsOnItsRadiation();
    testSteelBlend();
    testSteelBlendJointBridgeRingsOnItsRadiation();
    testPhaseAndPassivity();
    testRetuneAndTailOwnership();
    testStaticWorkAndRapidChanges();
    testConstructionSwitchesDoNotTick();
    testCoupledPitchPullIsBounded();
    testNoteToNoteLevelSpreadIsBounded();
    std::cout << "Body shape failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
