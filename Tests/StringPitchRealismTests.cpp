#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct State
    {
        double inharmonicity;
        double tension;
        double axialFrequency;
        double attackCents;
        double fundamentalDecay;
        double retainedIntrinsicDecay;
    };

    static State stringState(int string, int fret, double rate,
                             float slide, float member)
    {
        auto e = std::make_unique<AcustraEngine>();
        e->prepare(rate, 64);
        e->setLowerZoneMemberCount(15);
        auto& v = e->voices_[static_cast<std::size_t>(string)];
        v.played = v.keyDown = true;
        v.mpeMember = true;
        v.midiChannel = 2;
        v.midiNote = v.openMidi + fret;
        v.fret = fret;
        v.attackPitchCents = 0.0f;
        e->setPitchBend(slide, 1);
        e->setPitchBend(member, 2);
        e->configureVoice(v, string, v.midiNote, false);
        // Controlled wave slope gives the same geometric extension, without
        // a pluck's random shape or a bridge mode obscuring the comparison.
        v.attackSlopeEnergy = 0.001f;
        e->updateAttackPitch(v, string);
        const double radius = std::sqrt(-v.longitudinalA2[0]);
        const double angle = std::acos(std::clamp(
            v.longitudinalA1[0] / (2.0 * radius), -1.0, 1.0));
        const double fundamental = 440.0 * std::exp2(
            (v.midiNote + slide + member - 69.0) / 12.0);
        const double omega = 2.0 * std::acos(-1.0) * fundamental / rate;
        const double lossOmega = rate == 48000.0 ? omega
            : 2.0 * std::atan((rate / 48000.0) * std::tan(0.5 * omega));
        const auto& loop = v.loops[0];
        const auto shelfMagnitude = [lossOmega] (double coefficient, double mix)
        {
            const std::complex<double> low = (1.0 - coefficient)
                / (1.0 - coefficient * std::polar(1.0, -lossOmega));
            return std::abs((1.0 - mix) + mix * low);
        };
        const auto z = std::polar(1.0, -omega);
        const double bendingMagnitude = loop.bendingLossGain / std::abs(
            1.0 + static_cast<double>(loop.bendingLossA1) * z
                + static_cast<double>(loop.bendingLossA2) * z * z);
        const double gainPerTrip = loop.loopGain
            * shelfMagnitude(loop.broadLossCoefficient, loop.broadLossMix)
            * shelfMagnitude(loop.lowpassCoefficient, loop.highLossMix)
            * bendingMagnitude;
        const double decay = -3.0 / (fundamental * std::log10(gainPerTrip));
        v.level = 0.001f;
        e->captureTail(v);
        return { v.dispersionDesignInharmonicity, v.tensionNewtons,
                 angle * rate / (2.0 * std::acos(-1.0)), v.attackPitchCents,
                 decay, v.tailHandIntrinsicT60 };
    }
};
}

namespace
{
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testSlideAndFretHaveTheSamePhysicalString()
{
    // Jarvelainen & Karjalainen (Acta Acustica 92, 2006): the same string's
    // B grows as 1/L^2. Moving the fret up an octave halves L, quadruples B,
    // and doubles the fixed-fixed axial frequency. A manager pitch wheel is
    // documented as that same slide, so these observables must agree with
    // stopping the string twelve frets further up, at unchanged tension.
    double worstB = 0.0;
    double worstAxial = 0.0;
    double worstDecay = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int string : { 0, 1, 4, 5 })
            for (const int fret : { 0, 5 })
            {
                using Access = acustra::AcustraEngineTestAccess;
                const auto held = Access::stringState(string, fret, rate, 0.0f, 0.0f);
                const auto slid = Access::stringState(string, fret, rate, 12.0f, 0.0f);
                const auto stopped = Access::stringState(string, fret + 12, rate, 0.0f, 0.0f);
                worstB = std::max(worstB,
                    std::abs(slid.inharmonicity / stopped.inharmonicity - 1.0));
                expect(std::abs(slid.inharmonicity / held.inharmonicity - 4.0) < 0.01,
                       "octave slide did not quadruple the string's inharmonicity");
                expect(slid.tension == held.tension,
                       "a slide changed the string's tension");
                // Above the host's modelled band the axial mode is clamped;
                // equivalence with the physical fret remains the invariant.
                worstAxial = std::max(worstAxial,
                    std::abs(slid.axialFrequency / stopped.axialFrequency - 1.0));
                expect(std::abs(slid.attackCents / stopped.attackCents - 1.0) < 0.01,
                       "attack settling retained the pre-slide speaking length");
                // Read the actual normal-loop transfer at the fundamental,
                // so this compares realised round-trip decay as well as the
                // geometry with the independently configured physical fret.
                worstDecay = std::max(worstDecay,
                    std::abs(slid.fundamentalDecay / stopped.fundamentalDecay - 1.0));
                expect(std::abs(slid.retainedIntrinsicDecay
                        / stopped.retainedIntrinsicDecay - 1.0) < 0.002,
                       "retained tail forgot the slide's intrinsic fret decay");
            }
    std::cout << "Slide vs physical fret: B error " << 100.0 * worstB
              << "%, axial mode error " << 100.0 * worstAxial << "%\n";
    expect(worstB < 0.002, "slide and fret disagreed on stiff-string dispersion");
    expect(worstAxial < 0.002, "slide and fret disagreed on axial resonances");
    std::cout << "Slide vs physical fret: fundamental decay error "
              << 100.0 * worstDecay << "%\n";
    expect(worstDecay < 0.002, "slide and fret disagreed on fundamental decay");
}

void testBendingRaisesTensionWithoutShorteningTheFret()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const int string : { 0, 4, 5 })
    {
        const auto held = Access::stringState(string, 5, 48000.0, 0.0f, 0.0f);
        const auto bent = Access::stringState(string, 5, 48000.0, 0.0f, 2.0f);
        expect(bent.tension > held.tension,
               "the member's lateral bend did not raise tension");
        expect(std::abs(bent.axialFrequency / held.axialFrequency - 1.0) < 0.002,
               "a tension bend moved the physical fret");
        // The same transverse extension makes a smaller relative tension
        // increment on an already bent string (Kirchhoff-Carrier law).
        expect(bent.attackCents < 0.90 * held.attackCents,
               "attack settling ignored the member bend's higher tension");
    }
}
}

int main()
{
    testSlideAndFretHaveTheSamePhysicalString();
    testBendingRaisesTensionWithoutShorteningTheFret();
    if (failures == 0)
        std::cout << "All string pitch realism tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
