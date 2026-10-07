#include "DSP/AcustraEngine.h"
#include "DSP/GuitarModelData.h"
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
    static const auto& bridge(const AcustraEngine& e) { return e.bridgeLoad_; }
    static const auto& mobility(const AcustraEngine& e) { return e.bridgeMobilityTable(); }
    static auto pole(const EngineParameters& p, int index, bool loaded)
    { return AcustraEngine::radiationModePole(p, fittedPhysicalCalibration, index, loaded); }
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
int failures = 0;
void expect(bool pass, const char* message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

double independentlyLoadedQ(double f, double q, double magnitude)
{
    // Independent inverse-loss formulation; no production helper call.
    const double participation = f*f / (f*f + 180.0*180.0)
        * (900.0*900.0 / (f*f + 900.0*900.0));
    return 1.0 / (1.0/q + magnitude*participation);
}

acustra::EngineParameters parameters(acustra::GuitarModel model)
{
    acustra::EngineParameters p;
    p.guitarModel = model;
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
        for (float q : { 1.f, 4.f, 25.f, 60.f, 150.f })
        {
            expect(acustra::detail::playerLoadedBodyQ(float(f), q, false) == q,
                   "disabled contact changed Q");
            for (float amount : { .003f, .006f })
            {
                const double result = acustra::detail::playerLoadedBodyQ(float(f), q,
                                                                         true, amount);
                expect(result > 0 && result <= q,
                       "player loss left the positive-damping interval");
                expect(std::abs(result / independentlyLoadedQ(f, q, amount) - 1.) < 2e-7,
                       "player loss differs from the independent inverse-Q law");
            }
        }
    std::cout << "Authored loss brackets (free / .003 / .006 T60 seconds, peak dB):\n";
    for (const int i : { 0, 1, 2, 3, 5, 10, 27 })
    {
        const auto& m = acustra::detail::bellidoBodyModes[std::size_t(i)];
        std::cout << m.frequency << " Hz " << std::log(1000.)*m.q/(pi*m.frequency);
        for (double amount : { .003, .006 })
        {
            const auto q = independentlyLoadedQ(m.frequency, m.q, amount);
            std::cout << " / " << std::log(1000.)*q/(pi*m.frequency)
                      << " (" << 20*std::log10(q/m.q) << " dB)";
        }
        std::cout << '\n';
    }
}

void testActualPolesAndResidues()
{
    for (const auto model : { acustra::GuitarModel::Original,
                              acustra::GuitarModel::Bellido1978 })
        for (double rate : { 44100., 48000., 96000. })
        {
            const auto p = parameters(model);
            const auto free = makeEngine(rate, p, false);
            const auto held = makeEngine(rate, p, true);
            const bool applies = model == acustra::GuitarModel::Bellido1978;
            expect(Access::bodyCount(*free) == Access::bodyCount(*held),
                   "player loading changed mode count/order");
            for (int i = 0; i < Access::bodyCount(*free); ++i)
            {
                const auto& a = Access::body(*free)[std::size_t(i)];
                const auto& b = Access::body(*held)[std::size_t(i)];
                const Complex oldPole(a.poleReal, a.poleImaginary);
                const Complex newPole(b.poleReal, b.poleImaginary);
                const auto runtime = Access::runtimePole(*held, i);
                expect(runtime[0] == b.poleReal && runtime[1] == b.poleImaginary,
                       "configured and runtime radiation loss disagree");
                if (a.poleImaginary == 0.f) continue;
                const auto base = Access::pole(p, i, false);
                const double q = applies ? independentlyLoadedQ(base[0], base[1], .006f)
                                         : base[1];
                const auto expectedPole = std::polar(std::exp(-pi*base[0]/(q*rate)),
                                                      2*pi*base[0]/rate);
                expect(std::abs(newPole-expectedPole) < 2e-7,
                       "actual radiation pole does not carry the authored Q");
                expect(std::abs(std::arg(oldPole)-std::arg(newPole)) < 1e-7,
                       "player loading shifted a radiation center frequency");
                expect(std::abs(newPole) <= std::abs(oldPole)+1e-7,
                       "player contact reduced radiation loss");
                const auto expectedScale = (newPole-Complex(1)) / (oldPole-Complex(1));
                const std::array<Complex, 4> oldResidues {
                    Complex(a.leftReal,a.leftImaginary), Complex(a.rightReal,a.rightImaginary),
                    Complex(a.leftMomentReal,a.leftMomentImaginary),
                    Complex(a.rightMomentReal,a.rightMomentImaginary) };
                const std::array<Complex, 4> newResidues {
                    Complex(b.leftReal,b.leftImaginary), Complex(b.rightReal,b.rightImaginary),
                    Complex(b.leftMomentReal,b.leftMomentImaginary),
                    Complex(b.rightMomentReal,b.rightMomentImaginary) };
                for (std::size_t part = 0; part < oldResidues.size(); ++part)
                    expect(std::abs(newResidues[part]-oldResidues[part]*expectedScale)
                               < 3e-7*std::abs(oldResidues[part])+1e-10,
                           "player loss changed physical radiation coupling beyond ZOH conversion");
            }
            const auto& a = Access::mobility(*free);
            const auto& b = Access::mobility(*held);
            expect(a.count == b.count && a.ordered == b.ordered,
                   "contact changed analytic bridge mode count");
            for (int i = 0; i < a.count; ++i)
            {
                const auto& x = a.modes[std::size_t(i)];
                const auto& y = b.modes[std::size_t(i)];
                expect(x.omega == y.omega && x.heave == y.heave
                    && x.cross == y.cross && x.rock == y.rock,
                    "contact changed bridge stiffness/residue matrix");
                const double f = rate/pi*std::atan(x.omega/(2*rate));
                const double oldQ = x.omega/(2*x.damping);
                const double expectedQ = applies
                    ? independentlyLoadedQ(f, oldQ, .006f) : oldQ;
                expect(std::abs(y.damping/(y.omega/(2*expectedQ))-1.) < 3e-7,
                       "analytic bridge loss differs from radiation loss law");
            }
            expect(a.plateOmega == b.plateOmega && a.plateDamping == b.plateDamping
                && a.plateWeight == b.plateWeight,
                "player contact double-counted the high-band conductance floor");
        }
}

void testActualBridgePassivity()
{
    for (double rate : { 44100., 48000., 96000. })
        for (const auto shape : { acustra::BodyShape::Parlor, acustra::BodyShape::Auditorium,
                                 acustra::BodyShape::Dreadnought, acustra::BodyShape::Jumbo })
        {
            auto p = parameters(acustra::GuitarModel::Bellido1978);
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
                    analytic[0] += double(table.plateWeight)*Complex(0,omega)
                        /Complex(table.plateOmega*table.plateOmega-omega*omega,
                                 2*table.plateDamping*omega);
                for (std::size_t part = 0; part < response.size(); ++part)
                    expect(std::abs(response[part]-analytic[part])
                        < 2e-4*std::abs(analytic[part])+2e-8,
                        "cached mobility differs from actual loaded digital sections");
            }
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
            p.guitarModel = (tick%3 == 0) ? acustra::GuitarModel::Original
                                        : acustra::GuitarModel::Bellido1978;
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
        for (const auto model : { acustra::GuitarModel::Original,
                                  acustra::GuitarModel::Bellido1978 })
        {
            const auto p = parameters(model);
            auto loaded = makeEngine(rate, p, true);
            auto free = makeEngine(rate, p, false);
            const auto a = passage(*loaded, rate, p, 128, false);
            const auto b = passage(*free, rate, p, 128, false);
            expect(model != acustra::GuitarModel::Original || a == b,
                   "contact double-counted Original's recording-fitted load");
            expect(model == acustra::GuitarModel::Original || a != b,
                   "Bellido contact had no audible-path effect");
            if (model == acustra::GuitarModel::Bellido1978)
            {
                double newEnergy = 0., oldEnergy = 0., difference = 0.;
                for (std::size_t n = 0; n < a.size(); ++n)
                    if (n%3 != 2)
                    {
                        newEnergy += double(a[n])*a[n];
                        oldEnergy += double(b[n])*b[n];
                        difference += double(a[n]-b[n])*(a[n]-b[n]);
                    }
                std::cout << rate << " Hz native held/released chord Main level "
                          << 10*std::log10(newEnergy/oldEnergy) << " dB; paired waveform "
                          << 100*std::sqrt(difference/oldEnergy) << "% RMS difference\n";
            }
            for (float v : a)
                expect(std::isfinite(v) && std::abs(v) <= 1.f,
                       "held chord/release became unstable");
            loaded->prepare(rate, 128);
            const auto reset = passage(*loaded, rate, p, 31, false);
            expect(reset == a, "reprepare/block boundaries changed loaded audio");
            auto options = loaded->performanceRealism();
            options.playerBodyLoading = false;
            loaded->setPerformanceRealism(options);
            expect(Access::quiet(*loaded), "setup ablation did not clear body/string state");
            expect(passage(*loaded, rate, p, 128, false) == b,
                   "setup ablation reused a loaded bank or stale analytic cache");
        }
        auto p = parameters(acustra::GuitarModel::Bellido1978);
        auto a = makeEngine(rate, p, true);
        auto b = makeEngine(rate, p, true);
        const auto large = passage(*a, rate, p, 128, true);
        const auto small = passage(*b, rate, p, 17, true);
        expect(large == small, "body/capture fades depend on host block boundaries");
        expect(Access::settled(*a), "interrupted model/shape fade did not settle");
        for (float v : large)
            expect(std::isfinite(v) && std::abs(v) <= 1.f,
                   "body/contact transition produced unstable audio");

        p.capture = acustra::CaptureType::StereoMic;
        auto stereo = makeEngine(rate, p, true);
        p.capture = acustra::CaptureType::MonoMic;
        auto mono = makeEngine(rate, p, true);
        p.capture = acustra::CaptureType::Piezo;
        auto piezo = makeEngine(rate, p, true);
        const auto s = passage(*stereo, rate, parameters(p.guitarModel), 128, false);
        const auto m = passage(*mono, rate, p, 128, false);
        const auto z = passage(*piezo, rate, p, 128, false);
        for (std::size_t n = 2; n < s.size(); n += 3)
            expect(s[n] == m[n] && m[n] == z[n],
                   "microphone capture re-applied or bypassed the mechanical body load");
    }
}

double fundamentalCents(const std::vector<float>& audio, double rate, int note)
{
    const double nominal = 440*std::exp2((note-69)/12.);
    const int first = int(.35*rate), last = int(.8*rate);
    double bestPower = -1., best = 0.;
    for (int step = -60; step <= 60; ++step)
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
            const auto p = parameters(acustra::GuitarModel::Bellido1978);
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
            expect(std::abs(cents[1]) < 5., "held pitch left its existing tuning bracket");
        }
}
}

int main()
{
    testLawAndBracket();
    testActualPolesAndResidues();
    testActualBridgePassivity();
    testPlayedState();
    testHeldTuning();
    if (failures) std::cerr << failures << " player-body loading checks failed\n";
    return failures ? 1 : 0;
}
