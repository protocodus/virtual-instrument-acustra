#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <numbers>

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
        || model == acustra::GuitarModel::MartinD18V2007
        ? acustra::BodyShape::Dreadnought
        : model == acustra::GuitarModel::Washburn1897
        ? acustra::BodyShape::Parlor : acustra::BodyShape::Auditorium;
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
    for (int model = 0; model < 5; ++model)
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

void process(Engine& e, int frames, double* energy = nullptr, float* peak = nullptr)
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
    testPhaseAndPassivity();
    testRetuneAndTailOwnership();
    testStaticWorkAndRapidChanges();
    std::cout << "Body shape failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
