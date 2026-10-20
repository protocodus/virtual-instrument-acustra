#include "DSP/AcustraEngine.h"
#include "DSP/MeasuredBodyData.h"
#include "DSP/MeasuredJointBodyData.h"
#include "DSP/SteelBodyBlend.h"
#include "DSP/PlayerBodyLoading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static const auto& body(const AcustraEngine& e) { return e.bodyModes_; }
    static int bodyCount(const AcustraEngine& e) { return e.bodyBank_.ordered; }
    static std::array<float, 2> runtimePole(const AcustraEngine& e, int index)
    { return { e.bodyBank_.poleReal[std::size_t(index)],
               e.bodyBank_.poleImaginary[std::size_t(index)] }; }
    static void isolateMode(AcustraEngine& e, int index)
    {
        auto modes = e.bodyModes_;
        const auto chosen = modes[std::size_t(index)];
        modes.fill({});
        modes[0] = chosen;
        e.bodyBank_.load(modes, 1, 1);
        e.bodyBank_.reset();
        e.bodyBank_.captureFilter.enabled = false;
    }
    static std::array<float, 2> renderMode(AcustraEngine& e, float force, float moment)
    {
        const auto value = e.bodyBank_.render(force, moment);
        return { value.left, value.right };
    }
    static void removeContinuousFactor(AcustraEngine& e, std::complex<double> inverse)
    {
        // Negative control: restore the old rate-only residue on the already
        // isolated loaded mode without changing its pole or state equation.
        for (auto arrays : { std::array<AcustraEngine::BodyBank::Lanes*, 2> {
                &e.bodyBank_.leftReal, &e.bodyBank_.leftImaginary },
                { &e.bodyBank_.rightReal, &e.bodyBank_.rightImaginary },
                { &e.bodyBank_.leftMomentReal, &e.bodyBank_.leftMomentImaginary },
                { &e.bodyBank_.rightMomentReal, &e.bodyBank_.rightMomentImaginary } })
        {
            const auto old = std::complex<double>((*arrays[0])[0], (*arrays[1])[0])*inverse;
            (*arrays[0])[0] = float(old.real());
            (*arrays[1])[0] = float(old.imag());
        }
    }
    static const auto& bridge(const AcustraEngine& e) { return e.bridgeLoad_; }
    static const auto& mobility(const AcustraEngine& e) { return e.bridgeMobilityTable(); }
    static auto pole(const EngineParameters& p, int index, bool loaded)
    { return AcustraEngine::radiationModePole(p, fittedPhysicalCalibration, index, loaded); }
    static double modeEnergy(const AcustraEngine& e)
    { return double(e.bodyBank_.real[0])*e.bodyBank_.real[0]
           + double(e.bodyBank_.imaginary[0])*e.bodyBank_.imaginary[0]; }
    static bool sameConfigurations(const AcustraEngine& a, const AcustraEngine& b)
    {
        for (std::size_t i = 0; i < a.bodyConfigurations_.size(); ++i)
        {
            const auto& x = a.bodyConfigurations_[i];
            const auto& y = b.bodyConfigurations_[i];
            if (x.count != y.count || x.ordered != y.ordered
                || x.airModeHz != y.airModeHz || x.midPeakHz != y.midPeakHz
                || x.midHoleHz != y.midHoleHz) return false;
            for (int j = 0; j < x.count; ++j)
            {
                const auto& u = x.modes[std::size_t(j)];
                const auto& v = y.modes[std::size_t(j)];
                if (u.poleReal != v.poleReal || u.poleImaginary != v.poleImaginary
                    || u.leftReal != v.leftReal || u.leftImaginary != v.leftImaginary
                    || u.rightReal != v.rightReal || u.rightImaginary != v.rightImaginary
                    || u.leftMomentReal != v.leftMomentReal
                    || u.leftMomentImaginary != v.leftMomentImaginary
                    || u.rightMomentReal != v.rightMomentReal
                    || u.rightMomentImaginary != v.rightMomentImaginary) return false;
            }
        }
        return true;
    }
    static bool settled(const AcustraEngine& e)
    { return e.bodyModelFade_ == 1.0f && e.bridgeLoadFade_ == 1.0f
        && !e.bodyUpdatePending_ && !e.bridgeUpdatePending_; }
    static bool quiet(const AcustraEngine& e)
    {
        for (const auto& voice : e.voices_)
            if (voice.played || voice.keyDown || voice.tailActive) return false;
        for (float value : e.bodyBank_.real) if (value != 0) return false;
        for (float value : e.bodyBank_.imaginary) if (value != 0) return false;
        return true;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Complex = std::complex<double>;
constexpr double pi = 3.14159265358979323846;
// The four original offline switches have positional aggregate callers.
// A newly inserted field must not reinterpret those callers or disable the
// production contact load through an unrelated hand comparison.
constexpr acustra::PerformanceRealism legacySwitches { false, true, false, false };
static_assert(!legacySwitches.contactRelease && legacySwitches.coherentHand
              && !legacySwitches.gestureDamping && !legacySwitches.retuneContinuity
              && legacySwitches.playerBodyLoading,
              "new realism switches must preserve existing aggregate positions");
int failures = 0;
void expect(bool pass, const char* message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

double independentlyLoadedQ(double f, double q)
{
    // Independent inverse-loss formulation; no production helper call.
    const double torso = .004 * f*f / (f*f + 180.*180.)
        * (700.*700. / (f*f + 700.*700.));
    const double forearm = .002 * f*f / (f*f + 300.*300.)
        * (1800.*1800. / (f*f + 1800.*1800.));
    return 1.0 / (1.0/q + torso + forearm);
}

// Numerically integrate the unit-input continuous state equation over one
// held-input interval. This does not reuse the engine's residue-ratio formula.
Complex heldInputIntegral(Complex pole, double duration)
{
    constexpr int steps = 256;
    const double step = duration/steps;
    Complex sum = 1.+std::exp(pole*duration);
    for (int i = 1; i < steps; ++i)
        sum += (i%2 == 0 ? 2. : 4.)*std::exp(pole*(i*step));
    return sum*(step/3.);
}

acustra::EngineParameters parameters()
{
    acustra::EngineParameters p;
    p.shape = acustra::BodyShape::Auditorium;
    p.bodyMaterial = acustra::BodyMaterial::Mahogany;
    p.picking = acustra::PickingTechnique::Thumb;
    p.bodyAmount = .82f;
    p.outputGain = .7f;
    p.room = 0.0f;
    return p;
}

std::unique_ptr<Engine> makeEngine(double rate, const acustra::EngineParameters& p,
                                  bool loaded)
{
    auto e = std::make_unique<Engine>();
    auto options = e->performanceRealism();
    options.playerBodyLoading = loaded;
    e->setPerformanceRealism(options);
    e->setParameters(p);
    e->prepare(rate, 128);
    return e;
}

void testLawAndBracket()
{
    for (double f : { 45., 82., 102., 212., 337., 411., 555., 900., 3000., 9000. })
        for (float q : { .05f, 1.f, 4.f, 25.f, 60.f, 150.f })
        {
            expect(acustra::detail::playerLoadedBodyQ(float(f), q, false) == q,
                   "disabled contact changed Q");
            const double result = acustra::detail::playerLoadedBodyQ(float(f), q, true);
            expect(result > 0 && result < q,
                   "player loss left the positive-damping interval");
            expect(std::abs(result / independentlyLoadedQ(f, q) - 1.) < 2e-7,
                   "player loss differs from the independent inverse-Q law");
        }
    std::cout << "Auditorium/Mahogany body mode free/held T60 (seconds):\n";
    const auto p = parameters();
    for (const int i : { 0, 1, 2, 5, 9, 11, 30 })
    {
        const auto base = Access::pole(p, i, false);
        const double q = independentlyLoadedQ(base[0], base[1]);
        std::cout << base[0] << " Hz " << std::log(1000.)*base[1]/(pi*base[0])
                  << " / " << std::log(1000.)*q/(pi*base[0])
                  << " (isolated mobility peak " << 20*std::log10(q/base[1]) << " dB)\n";
    }
}

void testActualPolesAndResidues()
{
    for (double rate : { 44100., 48000., 96000. })
        for (int shape = 0; shape < 4; ++shape)
        {
            auto p = parameters();
            p.shape = static_cast<acustra::BodyShape>(shape);
            p.bodyMaterial = static_cast<acustra::BodyMaterial>(shape % 3);
            const auto free = makeEngine(rate, p, false);
            const auto held = makeEngine(rate, p, true);
            expect(Access::bodyCount(*free) == Access::bodyCount(*held),
                   "player loading changed mode count/order");
            int measuredCount = int(acustra::detail::measuredSteelBodyModes.size());
            if (acustra::detail::steelBlendJointBodyWeight > 0)
                for (const auto& mode : acustra::detail::measuredSteelJointBodyModes)
                    if (mode.frequency < acustra::detail::steelBlendJointBandHz)
                        ++measuredCount;
            for (int i = 0; i < measuredCount; ++i)
            {
                const auto& a = Access::body(*free)[std::size_t(i)];
                const auto& b = Access::body(*held)[std::size_t(i)];
                const Complex oldPole(a.poleReal, a.poleImaginary);
                const Complex newPole(b.poleReal, b.poleImaginary);
                const auto runtime = Access::runtimePole(*held, i);
                expect(runtime[0] == b.poleReal && runtime[1] == b.poleImaginary,
                       "configured and runtime radiation loss disagree");
                if (a.poleImaginary == 0.f) continue;
                const bool own = i < int(acustra::detail::measuredSteelBodyModes.size());
                const double decodedFrequency = std::arg(oldPole)*rate/(2*pi);
                const double decodedQ = -std::arg(oldPole)/(2*std::log(std::abs(oldPole)));
                const auto base = own ? Access::pole(p, i, false)
                    : std::array<float, 2> { float(decodedFrequency), float(decodedQ) };
                const double q = independentlyLoadedQ(base[0], base[1]);
                const auto expectedPole = std::polar(std::exp(-pi*base[0]/(q*rate)),
                                                      2*pi*base[0]/rate);
                expect(std::abs(newPole-expectedPole) < 3e-7,
                       "actual radiation pole does not carry the authored Q");
                expect(std::abs(std::arg(oldPole)-std::arg(newPole)) < 1e-7,
                       "player loading shifted a radiation center frequency");
                expect(std::abs(newPole) <= std::abs(oldPole)+1e-7,
                       "player contact reduced radiation loss");
                const Complex oldContinuousPole(-pi*base[0]/base[1], 2*pi*base[0]);
                const Complex newContinuousPole(-pi*base[0]/q, 2*pi*base[0]);
                const auto freeInputIntegral = heldInputIntegral(oldContinuousPole, 1./rate);
                const auto heldInput = heldInputIntegral(newContinuousPole, 1./rate);
                const std::array<Complex, 4> oldResidues {
                    Complex(a.leftReal,a.leftImaginary), Complex(a.rightReal,a.rightImaginary),
                    Complex(a.leftMomentReal,a.leftMomentImaginary),
                    Complex(a.rightMomentReal,a.rightMomentImaginary) };
                const std::array<Complex, 4> newResidues {
                    Complex(b.leftReal,b.leftImaginary), Complex(b.rightReal,b.rightImaginary),
                    Complex(b.leftMomentReal,b.leftMomentImaginary),
                    Complex(b.rightMomentReal,b.rightMomentImaginary) };
                // The four projected low modes evaluate their Green function
                // at the loaded continuous pole. That observation legitimately
                // changes with damping. Its symmetric pressure cancels in L-R,
                // exposing the unchanged measured input coupling independently.
                const bool spatial = i < 2 || i == 133 || i == 134;
                for (std::size_t part = 0; part < oldResidues.size(); ++part)
                {
                    if (spatial && part%2 != 0) continue;
                    const auto oldCoupling = (spatial
                        ? oldResidues[part]-oldResidues[part+1] : oldResidues[part])
                        /freeInputIntegral;
                    const auto loadedCoupling = (spatial
                        ? newResidues[part]-newResidues[part+1] : newResidues[part])
                        /heldInput;
                    expect(std::abs(loadedCoupling-oldCoupling)
                               < 2e-5*std::abs(oldCoupling)+1e-8,
                           "player loss changed independently recovered continuous input coupling");
                }
            }
            const auto& a = Access::mobility(*free);
            const auto& b = Access::mobility(*held);
            expect(a.count == b.count && a.ordered == b.ordered,
                   "contact changed analytic bridge mode count");
            for (int i = 0; i < a.count; ++i)
            {
                const auto& x = a.modes[std::size_t(i)];
                const auto& y = b.modes[std::size_t(i)];
                expect(x.heave == y.heave && x.cross == y.cross && x.rock == y.rock,
                    "contact changed mechanical bridge residue matrix");
                // Shared-pole prototypes are inverse-bilinear images of the
                // damped radiation pole: omega changes slightly with Q.
                expect(y.damping >= x.damping,
                       "contact reduced analytic bridge damping");
            }
            expect(a.plateOmega == b.plateOmega && a.plateDamping == b.plateDamping
                && a.plateWeight == b.plateWeight && a.plateRock == b.plateRock,
                "player contact double-counted the high-band conductance floor");
        }
}

void testActualBridgePassivity()
{
    for (double rate : { 44100., 48000., 96000. })
        for (const auto shape : { acustra::BodyShape::Parlor, acustra::BodyShape::Auditorium,
                                 acustra::BodyShape::Dreadnought, acustra::BodyShape::Jumbo })
        {
            auto p = parameters();
            p.shape = shape;
            const auto e = makeEngine(rate, p, true);
            const auto& bridge = Access::bridge(*e);
            const auto& table = Access::mobility(*e);
            for (int n = 0; n < 160; ++n)
            {
                const double f = 30*std::pow(.44*rate/30, double(n)/159);
                const auto z = std::polar(1., -2*pi*f/rate);
                std::array<Complex, 3> response {};
                // Actual deployed digital coefficients, including the floor.
                for (int active = 0; active < bridge.activeModeCount; ++active)
                {
                    const auto i = bridge.activeModes[std::size_t(active)];
                    const auto& m = bridge.heaveModes[i];
                    const double immediate = -m.numerator2/(1+m.denominator2);
                    const auto mode = immediate
                        + (m.numerator1*z + m.numerator2*z*z)
                            /(1.+m.denominator1*z+m.denominator2*z*z);
                    response[0] += double(bridge.residueHeave[i])*mode;
                    response[1] += double(bridge.residueCross[i])*mode;
                    response[2] += double(bridge.residueRock[i])*mode;
                }
                const double tolerance = 1e-9;
                expect(response[0].real() >= -tolerance
                    && response[2].real() >= -tolerance
                    && response[0].real()*response[2].real()
                        - response[1].real()*response[1].real() >= -tolerance,
                    "loaded digital mobility is not positive real/PSD");
                // Independent analog mobility at bilinear frequency verifies
                // the cached observer sees precisely the deployed damping.
                const double omega = 2*rate*std::tan(pi*f/rate);
                std::array<Complex, 3> analytic {};
                for (int i = 0; i < table.count; ++i)
                {
                    const auto& m = table.modes[std::size_t(i)];
                    const auto mode = Complex(0,omega)
                        /Complex(m.omega*m.omega-omega*omega, 2*m.damping*omega);
                    analytic[0] += double(table.scale)*m.heave*mode;
                    analytic[1] += double(table.scale)*m.cross*mode;
                    analytic[2] += double(table.scale)*m.rock*mode;
                }
                if (table.plate)
                {
                    const auto floor = Complex(0,omega)
                        /Complex(table.plateOmega*table.plateOmega-omega*omega,
                                 2*table.plateDamping*omega);
                    analytic[0] += double(table.plateWeight)*floor;
                    analytic[2] += double(table.plateRock)*floor;
                }
                for (std::size_t part = 0; part < response.size(); ++part)
                    expect(std::abs(response[part]-analytic[part])
                        < 2e-4*std::abs(analytic[part])+2e-8,
                        "cached mobility differs from actual loaded digital sections");
            }
        }
}

void testContinuousHeldInputResponse()
{
    // Drive the deployed single-mode recurrence and an independently
    // integrated continuous state equation with the same held sample input.
    // Their complex Fourier responses must agree, including phase. The
    // continuous coupling is recovered from the unloaded mode by numerical
    // held-input quadrature; the loaded residue formula is never called.
    const auto p = parameters();
    for (double rate : { 44100., 48000., 96000. })
        for (int mode : { 0, 3, 10 })
            for (int axis : { 0, 1 })
            {
                const auto free = makeEngine(rate, p, false);
                auto held = makeEngine(rate, p, true);
                auto wrong = makeEngine(rate, p, true);
                const auto& original = Access::body(*free)[std::size_t(mode)];
                const auto base = Access::pole(p, mode, false);
                const double newQ = independentlyLoadedQ(base[0], base[1]);
                const Complex nominalOldPole(-pi*base[0]/base[1], 2*pi*base[0]);
                const Complex nominalNewPole(-pi*base[0]/newQ, 2*pi*base[0]);
                // The nominal pole/Q was independently verified above.
                // Propagate the precisely represented float-pole system
                // here: low modes' radius quantization otherwise dominates
                // a long response comparison against an ideal double pole.
                const auto freeDigital = Access::runtimePole(*free, mode);
                const auto heldDigital = Access::runtimePole(*held, mode);
                const Complex oldPole = rate*std::log(Complex(freeDigital[0], freeDigital[1]));
                const Complex newPole = rate*std::log(Complex(heldDigital[0], heldDigital[1]));
                const auto integral = heldInputIntegral(oldPole, 1./rate);
                std::array<Complex, 2> coupling = axis == 0
                    ? std::array<Complex, 2> {
                        Complex(original.leftReal, original.leftImaginary)/integral,
                        Complex(original.rightReal, original.rightImaginary)/integral }
                    : std::array<Complex, 2> {
                        Complex(original.leftMomentReal, original.leftMomentImaginary)/integral,
                        Complex(original.rightMomentReal, original.rightMomentImaginary)/integral };
                // Low-mode spatial observation is symmetric; use the
                // measured L-R component as the constant-coupling witness.
                if (mode < 2) coupling = { coupling[0]-coupling[1], Complex{} };
                Access::isolateMode(*held, mode);
                Access::isolateMode(*wrong, mode);
                Access::removeContinuousFactor(*wrong, nominalNewPole/nominalOldPole);
                constexpr int frames = 4096, subdivisions = 16;
                const double h = 1./(rate*subdivisions);
                Complex continuousState {};
                std::array<std::array<Complex, 3>, 2> actual {}, reference {}, oldFormula {};
                for (int n = 0; n < frames; ++n)
                {
                    const float input = float(.25*std::cos(2*pi*base[0]*.73*n/rate));
                    for (int part = 0; part < subdivisions; ++part)
                    {
                        const auto rhs = [newPole, input] (Complex x)
                        { return newPole*x+double(input); };
                        const Complex k1 = rhs(continuousState);
                        const Complex k2 = rhs(continuousState+h*.5*k1);
                        const Complex k3 = rhs(continuousState+h*.5*k2);
                        const Complex k4 = rhs(continuousState+h*k3);
                        continuousState += h/6.*(k1+2.*k2+2.*k3+k4);
                    }
                    auto pressure = Access::renderMode(*held,
                        axis == 0 ? input : 0.f, axis == 1 ? input : 0.f);
                    auto wrongPressure = Access::renderMode(*wrong,
                        axis == 0 ? input : 0.f, axis == 1 ? input : 0.f);
                    if (mode < 2)
                    {
                        pressure = { pressure[0]-pressure[1], 0.f };
                        wrongPressure = { wrongPressure[0]-wrongPressure[1], 0.f };
                    }
                    const double window = .5-.5*std::cos(2*pi*n/(frames-1));
                    for (int frequency = 0; frequency < 3; ++frequency)
                    {
                        const double evaluated = base[0]*(.73+.135*frequency);
                        const auto phase = std::polar(window, -2*pi*evaluated*n/rate);
                        for (std::size_t channel = 0; channel < 2; ++channel)
                        {
                            actual[channel][std::size_t(frequency)]
                                += double(pressure[channel])*phase;
                            oldFormula[channel][std::size_t(frequency)]
                                += double(wrongPressure[channel])*phase;
                            reference[channel][std::size_t(frequency)]
                                += 2.*(coupling[channel]*continuousState).real()*phase;
                        }
                    }
                }
                double error = 0., energy = 0., negativeError = 0.;
                for (std::size_t channel = 0; channel < 2; ++channel)
                    for (std::size_t frequency = 0; frequency < 3; ++frequency)
                    {
                        error += std::norm(actual[channel][frequency]-reference[channel][frequency]);
                        negativeError += std::norm(oldFormula[channel][frequency]-reference[channel][frequency]);
                        energy += std::norm(reference[channel][frequency]);
                    }
                std::cout << "Continuous response " << rate << " Hz mode " << mode
                          << " axis " << axis << " relative error " << std::sqrt(error/energy)
                          << "; old-formula negative control " << std::sqrt(negativeError/energy)
                          << '\n';
                expect(energy > 1e-12 && std::sqrt(error/energy) < 5e-5,
                       "loaded radiation response differs from independently integrated continuous H(s)");
                expect(std::sqrt(negativeError/energy) > 5e-5,
                       "continuous-response test failed to reject the old rate-only residue formula");
            }
}

std::vector<float> passage(Engine& e, double rate, acustra::EngineParameters p,
                           int block, bool transitions)
{
    const int hop = int(rate*.04);
    std::vector<float> audio;
    audio.reserve(std::size_t(hop*42*3));
    std::array<float, 128> l {}, r {}, pickup {};
    for (int tick = 0; tick < 42; ++tick)
    {
        if (tick == 0) e.noteOn(40, .65f);
        if (tick == 1) e.noteOn(52, .72f);
        if (tick == 2) e.noteOn(64, .76f);
        if (tick == 3) e.noteOn(67, .8f);
        if (tick == 7) e.noteOn(67, .85f);
        if (tick == 11) e.noteOff(67);
        if (tick == 15) e.allNotesOff();
        if (transitions && tick >= 4 && tick < 14)
        {
            p.bodyMaterial = static_cast<acustra::BodyMaterial>(tick%3);
            p.shape = (tick%2 == 0) ? acustra::BodyShape::Parlor
                                   : acustra::BodyShape::Auditorium;
            p.capture = (tick%2 == 0) ? acustra::CaptureType::MonoMic
                                     : acustra::CaptureType::StereoMic;
            e.setParameters(p);
        }
        for (int done = 0; done < hop;)
        {
            const int count = std::min(block, hop-done);
            e.process(l.data(), r.data(), { pickup.data() }, count);
            for (int n = 0; n < count; ++n)
            {
                audio.push_back(l[std::size_t(n)]);
                audio.push_back(r[std::size_t(n)]);
                audio.push_back(pickup[std::size_t(n)]);
            }
            done += count;
        }
    }
    return audio;
}

void testPlayedState()
{
    for (double rate : { 44100., 48000., 96000. })
    {
        const auto p = parameters();
        auto loaded = makeEngine(rate, p, true);
        auto free = makeEngine(rate, p, false);
        const auto a = passage(*loaded, rate, p, 128, false);
        const auto b = passage(*free, rate, p, 128, false);
        expect(a != b, "player contact had no audible-path effect");
        double newEnergy = 0., oldEnergy = 0., difference = 0.;
        for (std::size_t n = 0; n < a.size(); ++n)
        {
            expect(std::isfinite(a[n]) && std::abs(a[n]) <= 1.f,
                   "held chord/release became unstable");
            if (n%3 != 2)
            {
                newEnergy += double(a[n])*a[n];
                oldEnergy += double(b[n])*b[n];
                difference += double(a[n]-b[n])*(a[n]-b[n]);
            }
        }
        std::cout << rate << " Hz native held/released chord Main level "
                  << 10*std::log10(newEnergy/oldEnergy) << " dB; paired waveform "
                  << 100*std::sqrt(difference/oldEnergy) << "% RMS difference\n";
        loaded->prepare(rate, 128);
        expect(passage(*loaded, rate, p, 31, false) == a,
               "reprepare/block boundaries changed loaded audio");
        auto options = loaded->performanceRealism();
        options.playerBodyLoading = false;
        loaded->setPerformanceRealism(options);
        expect(Access::quiet(*loaded), "setup ablation did not clear body/string state");
        expect(passage(*loaded, rate, p, 128, false) == b,
               "setup ablation reused a loaded bank or stale analytic cache");
        options.playerBodyLoading = true;
        loaded->setPerformanceRealism(options);
        expect(passage(*loaded, rate, p, 17, false) == a,
               "reenabling contact reused an unloaded bank or stale analytic cache");
        auto first = makeEngine(rate, p, true);
        auto second = makeEngine(rate, p, true);
        const auto large = passage(*first, rate, p, 128, true);
        const auto small = passage(*second, rate, p, 17, true);
        expect(large == small, "body/capture fades depend on host block boundaries");
        expect(Access::settled(*first), "interrupted material/shape fade did not settle");
        for (float v : large)
            expect(std::isfinite(v) && std::abs(v) <= 1.f,
                   "body/contact transition produced unstable audio");
    }
}

void testImpulseDecay()
{
    for (double rate : { 44100., 48000., 96000. })
        for (int mode : { 0, 5, 11 })
        {
            auto free = makeEngine(rate, parameters(), false);
            auto held = makeEngine(rate, parameters(), true);
            const auto freePole = Access::runtimePole(*free, mode);
            const auto heldPole = Access::runtimePole(*held, mode);
            const double oldRadius = std::hypot(freePole[0], freePole[1]);
            const double newRadius = std::hypot(heldPole[0], heldPole[1]);
            std::array<double, 2> measured {};
            int version = 0;
            for (auto* e : { free.get(), held.get() })
            {
                Access::isolateMode(*e, mode);
                Access::renderMode(*e, 1.f, 0.f);
                const double before = Access::modeEnergy(*e);
                const int frames = int(.1*rate);
                for (int n = 0; n < frames; ++n) Access::renderMode(*e, 0.f, 0.f);
                const double after = Access::modeEnergy(*e);
                measured[std::size_t(version++)] = -2.*std::log(1000.)*frames/rate
                    /std::log(after/before);
            }
            const double nominalOld = -std::log(1000.)/(rate*std::log(oldRadius));
            const double nominalNew = -std::log(1000.)/(rate*std::log(newRadius));
            expect(std::abs(measured[0]/nominalOld-1.) < 5e-4
                && std::abs(measured[1]/nominalNew-1.) < 5e-4,
                "rendered modal impulse decay differs from deployed pole");
            expect(measured[1] < measured[0], "held body impulse rings longer");
            std::cout << rate << " Hz mode " << mode << " measured free/held T60 "
                      << measured[0] << '/' << measured[1] << " s\n";
        }
}

void testAllConstructionCaches()
{
    // Toggle after rendering and changing construction; every cached shape
    // and material must match the corresponding freshly prepared posture.
    auto changed = makeEngine(48000., parameters(), true);
    passage(*changed, 48000., parameters(), 64, true);
    for (bool loaded : { false, true })
    {
        auto options = changed->performanceRealism();
        options.playerBodyLoading = loaded;
        changed->setPerformanceRealism(options);
        const auto fresh = makeEngine(48000., parameters(), loaded);
        expect(Access::sameConfigurations(*changed, *fresh),
               "posture toggle left one of the twelve construction caches stale");
    }
}

double fundamentalCents(const std::vector<float>& audio, double rate, int note)
{
    const double nominal = 440*std::exp2((note-69)/12.);
    const int first = int(.35*rate), last = int(.8*rate);
    double bestPower = -1., best = 0.;
    for (int step = -150; step <= 150; ++step)
    {
        const double cents = .1*step;
        const double f = nominal*std::exp2(cents/1200.);
        const double recurrence = 2*std::cos(2*pi*f/rate);
        double one = 0., two = 0.;
        for (int n = first; n < last; ++n)
        {
            const double window = .5-.5*std::cos(2*pi*(n-first)/(last-first-1));
            const double next = audio[std::size_t(n)]*window + recurrence*one-two;
            two = one; one = next;
        }
        const double power = one*one+two*two-recurrence*one*two;
        if (power > bestPower) { bestPower = power; best = cents; }
    }
    return best;
}

void testHeldTuning()
{
    for (double rate : { 44100., 48000., 96000. })
        for (int note : { 40, 52, 67 })
        {
            const auto p = parameters();
            std::array<double, 2> cents {};
            for (int loaded = 0; loaded < 2; ++loaded)
            {
                auto e = makeEngine(rate, p, loaded != 0);
                e->noteOn(note, .75f);
                std::vector<float> l(std::size_t(rate*.85)), r(l.size());
                e->process(l.data(), r.data(), int(l.size()));
                for (std::size_t n = 0; n < l.size(); ++n) l[n] = .5f*(l[n]+r[n]);
                cents[std::size_t(loaded)] = fundamentalCents(l, rate, note);
            }
            std::cout << rate << " Hz MIDI " << note << " held cents free/loaded "
                      << cents[0] << '/' << cents[1] << '\n';
            expect(std::abs(cents[1]-cents[0]) <= .4,
                   "player body load shifted held pitch by more than .4 cents");
            expect(std::abs(cents[0]) < 14.9 && std::abs(cents[1]) < 14.9,
                   "pitch comparison reached the spectral search boundary");
        }
}
}

int main()
{
    testLawAndBracket();
    testActualPolesAndResidues();
    testActualBridgePassivity();
    testContinuousHeldInputResponse();
    testPlayedState();
    testImpulseDecay();
    testAllConstructionCaches();
    testHeldTuning();
    if (failures) std::cerr << failures << " player-body loading checks failed\n";
    return failures ? 1 : 0;
}
