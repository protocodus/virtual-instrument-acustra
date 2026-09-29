#include "DSP/AcustraEngine.h"
#include "DSP/GuitarModelData.h"
#include "DSP/MeasuredBridgeData.h"
#include "DSP/MeasuredSteelBridgeData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <numbers>
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
    { return e.bridgeDerivativesCrossRelease_; }
    static bool bodySettled(const AcustraEngine& e, BodyShape shape)
    { return e.configuredBodyShape_ == shape && e.bodyModelFade_ == 1.0f; }
    static float saddle(const AcustraEngine& e) { return e.saddleHeightRatio(); }
    static const PhysicalCalibration& calibration(const AcustraEngine& e)
    { return e.physicalCalibration_; }
    static std::array<float, 2> radiationPole(const EngineParameters& p,
                                              const PhysicalCalibration& c, int index)
    { return AcustraEngine::radiationModePole(p, c, index); }
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
    std::array<int,7> worstCase {};
    for (int model = 0; model < 2; ++model)
        for (int material = 0; material < 2; ++material)
            for (int rate : { 8000, 24000, 48000, 96000 })
            {
                acustra::EngineParameters p;
                p.guitarModel = static_cast<acustra::GuitarModel>(model);
                p.stringMaterial = static_cast<acustra::StringMaterial>(material);
                p.shape = nativeShape(p.guitarModel);
                auto e = std::make_unique<Engine>();
                e->setParameters(p); e->prepare(rate, 64);
                const auto reference = Access::bridge(*e);
                for (int shape = 0; shape < 4; ++shape)
                {
                    p.shape = static_cast<acustra::BodyShape>(shape);
                    e->setParameters(p);
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
                                worstCase = {model,material,rate,shape,string,fret,
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
// and the plate-Q rule move it with the radiation. The Fylde, another
// guitar's bridge, is left as measured and does not follow Wood.
void testSteelOwnBridgeSharesTheRadiationPoles()
{
    const auto& twins = acustra::detail::steelBridgeRadiationTwins;
    int paired = 0;
    for (const auto twin : twins) paired += twin >= 0;
    expect(paired == 18, "steel's own bridge does not twin its 18 resolved modes");
    double worst = 0;
    std::array<double, 2> fyldeReference {};
    for (int shape = 0; shape < 4; ++shape)
        for (int wood = 0; wood < 4; ++wood)
        {
            acustra::EngineParameters p;
            p.stringMaterial = acustra::StringMaterial::Steel;
            p.bridgeModel = acustra::BridgeModel::Original;
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

            p.bridgeModel = acustra::BridgeModel::FyldeSteel;
            auto fylde = std::make_unique<Engine>();
            fylde->setParameters(p); fylde->prepare(48000, 64);
            const auto fyldeMode = Access::bridge(*fylde).heaveModes[5];
            if (wood == 0) fyldeReference = { fyldeMode.denominator1, fyldeMode.denominator2 };
            expect(fyldeMode.denominator1 == fyldeReference[0]
                       && fyldeMode.denominator2 == fyldeReference[1],
                   "Wood moved the Fylde bridge, another guitar's measurement");
        }
    std::cout << "steel own bridge twin pole relative error=" << worst << '\n';
    // A one-ulp change of a float frequency or Q moves these by ~1e-7.
    expect(worst < 1e-12, "a twinned bridge mode is not its radiation mode's pole");
}

// Every bridge that belongs to its radiation's guitar keeps each mode that is
// the same resonance as a radiation mode on it, under every Shape and Wood:
// steel's own bridge takes its twin's pole (above); nylon's g34 and the
// Bellido, with either string, keep the offset from it they have at their
// anchor (the Bellido's is the identity, so its measured offset). Before,
// their bridges ignored Wood: a Maple Bellido's drains sat 60-85 cents under
// their radiation peaks, and nylon's moved 30-50 cents against theirs from
// wood to wood (audit F10). The twins are the generator's test
// (GenerateMeasuredBridge.py), recomputed here from the banks.
void testEveryBodysBridgeKeepsItsDrainsOnItsRadiation()
{
    struct Bank { acustra::StringMaterial material; acustra::GuitarModel model; const char* name; };
    const Bank banks[] {
        { acustra::StringMaterial::Nylon, acustra::GuitarModel::Original, "nylon" },
        { acustra::StringMaterial::Nylon, acustra::GuitarModel::Bellido1978, "Bellido nylon" },
        { acustra::StringMaterial::Steel, acustra::GuitarModel::Bellido1978, "Bellido steel" },
    };
    double worst = 0;
    for (const auto& b : banks)
    {
        using namespace acustra::detail;
        const bool bellido = b.model == acustra::GuitarModel::Bellido1978;
        std::vector<MeasuredBridgeMode> bridgeModes;
        std::vector<MeasuredBodyMode> radiation;
        if (bellido)
        {
            bridgeModes.assign(bellidoBridgeModes.begin(), bellidoBridgeModes.end());
            radiation.assign(bellidoBodyModes.begin(), bellidoBodyModes.end());
        }
        else
        {
            bridgeModes.assign(measuredNylonBridgeModes.begin(), measuredNylonBridgeModes.end());
            radiation.assign(measuredNylonBodyModes.begin(), measuredNylonBodyModes.end());
        }
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
        expect(paired >= 9, "a body's bridge twins too few of its modes");
        // The offsets at the anchor: the classical box, at the body's wood.
        std::vector<double> anchorOffset(twins.size(), 0.0);
        {
            acustra::EngineParameters p;
            p.stringMaterial = b.material; p.guitarModel = b.model;
            p.shape = acustra::BodyShape::Auditorium;
            p.bodyMaterial = acustra::AcustraEngine::measuredBankWood(b.material, b.model);
            auto e = std::make_unique<Engine>();
            e->setParameters(p); e->prepare(48000, 64);
            const auto bridge = Access::bridge(*e);
            for (std::size_t i = 0; i < twins.size(); ++i)
                if (twins[i] >= 0)
                {
                    anchorOffset[i] = 1200*std::log2(sectionFrequency(bridge.heaveModes[i], 48000)
                        / Access::radiationPole(p, Access::calibration(*e), twins[i])[0]);
                    if (bellido)
                        expect(std::abs(anchorOffset[i] - 1200*std::log2(double(bridgeModes[i].frequency)
                                   / radiation[std::size_t(twins[i])].frequency)) < 0.01,
                               "the Bellido's bridge is not its measurement at its anchor");
                }
        }
        for (int shape = 0; shape < 4; ++shape)
            for (int wood = 0; wood < 4; ++wood)
            {
                acustra::EngineParameters p;
                p.stringMaterial = b.material; p.guitarModel = b.model;
                p.shape = static_cast<acustra::BodyShape>(shape);
                p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
                auto e = std::make_unique<Engine>();
                e->setParameters(p); e->prepare(48000, 64);
                const auto bridge = Access::bridge(*e);
                double sum = 0;
                for (std::size_t i = 0; i < twins.size(); ++i)
                {
                    if (twins[i] < 0) continue;
                    const double measured = anchorOffset[i];
                    const double engine = 1200*std::log2(sectionFrequency(bridge.heaveModes[i], 48000)
                        / Access::radiationPole(p, Access::calibration(*e), twins[i])[0]);
                    worst = std::max(worst, std::abs(engine-measured));
                    sum += std::abs(engine);
                }
                if (shape == 1 && (wood == 1 || wood == 3))
                    std::cout << b.name << (wood == 1 ? " Auditorium/Cedar" : " Auditorium/Maple")
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
                expect(same, "a live Wood change did not move a body's bridge");
            }
    }
    std::cout << "body bridges' twin offsets vs their anchor's, worst cents=" << worst << '\n';
    expect(worst < 0.01, "a bridge drain left its radiation twin under Shape, Wood or the anchor");
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
    struct Bank { acustra::StringMaterial material; acustra::GuitarModel model;
                  acustra::BridgeModel bridge; const char* name; };
    const Bank banks[] {
        { acustra::StringMaterial::Steel, acustra::GuitarModel::Original, acustra::BridgeModel::Original, "steel own" },
        { acustra::StringMaterial::Steel, acustra::GuitarModel::Original, acustra::BridgeModel::FyldeSteel, "Fylde" },
        { acustra::StringMaterial::Nylon, acustra::GuitarModel::Original, acustra::BridgeModel::Original, "nylon" },
        { acustra::StringMaterial::Nylon, acustra::GuitarModel::Bellido1978, acustra::BridgeModel::Original, "Bellido nylon" },
        { acustra::StringMaterial::Steel, acustra::GuitarModel::Bellido1978, acustra::BridgeModel::Original, "Bellido steel" },
    };
    const auto bridgeBank = [] (const Bank& b)
    {
        using namespace acustra::detail;
        if (b.model == acustra::GuitarModel::Bellido1978)
            return std::vector<MeasuredBridgeMode>(bellidoBridgeModes.begin(), bellidoBridgeModes.end());
        if (b.material == acustra::StringMaterial::Nylon)
            return std::vector<MeasuredBridgeMode>(measuredNylonBridgeModes.begin(), measuredNylonBridgeModes.end());
        if (b.bridge == acustra::BridgeModel::FyldeSteel)
            return std::vector<MeasuredBridgeMode>(measuredFyldeBridgeModes.begin(), measuredFyldeBridgeModes.end());
        return std::vector<MeasuredBridgeMode>(measuredSteelBridgeModes.begin(), measuredSteelBridgeModes.end());
    };
    const auto radiationBank = [] (const Bank& b)
    {
        using namespace acustra::detail;
        std::vector<float> f;
        if (b.model == acustra::GuitarModel::Bellido1978)
            for (const auto& m : bellidoBodyModes) f.push_back(m.frequency);
        else if (b.material == acustra::StringMaterial::Nylon)
            for (const auto& m : measuredNylonBodyModes) f.push_back(m.frequency);
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
        p.stringMaterial = b.material; p.guitarModel = b.model; p.bridgeModel = b.bridge;
        p.shape = b.material == acustra::StringMaterial::Steel
                && b.model == acustra::GuitarModel::Original
            ? acustra::BodyShape::Dreadnought : acustra::BodyShape::Auditorium;
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
    for (int material = 0; material < 2; ++material)
    {
        acustra::EngineParameters p;
        p.stringMaterial = static_cast<acustra::StringMaterial>(material);
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
        // At 48 kHz a step at a newly established load is not instantaneous
        // bridge motion. An omitted rebase emits the entire step as a force.
        expect(e->getLastBridgeReactionForce() == 0.0f,
               "shape emitted the instantaneous load change as a force impulse");
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

void testStaticWorkAndRapidChanges()
{
    double minimumWork = 0;
    float maximumPeak = 0;
    for (int material = 0; material < 2; ++material)
        for (int rate : { 8000, 48000, 96000 })
            for (int capture = 0; capture < 2; ++capture)
            {
                acustra::EngineParameters p;
                p.stringMaterial = static_cast<acustra::StringMaterial>(material);
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

int main()
{
    testSteelOwnBridgeSharesTheRadiationPoles();
    testBridgeModesMoveWithTheirRadiationNeighbours();
    testEveryBodysBridgeKeepsItsDrainsOnItsRadiation();
    testPhaseAndPassivity();
    testRetuneAndTailOwnership();
    testStaticWorkAndRapidChanges();
    std::cout << "Body shape failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
