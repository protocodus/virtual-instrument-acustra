#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Material
    {
        double length, tension, inharmonicity;
        bool played;
    };
    static Material inspect(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return { voice.speakingLengthMetres, voice.tensionNewtons,
                 voice.dispersionDesignInharmonicity, voice.played };
    }
    static double controlledAttackPitch(AcustraEngine& engine, int string,
                                         float slopeEnergy)
    {
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        voice.attackSlopeEnergy = slopeEnergy;
        engine.updateAttackPitch(voice, string);
        return voice.attackPitchCents;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double youngsModulus = 2.0e11;
constexpr std::array<int, 6> openNotes { 40, 45, 50, 55, 59, 64 };
// The declared EJ16 plain gauges and existing wound effective cores. These
// independent dimensions, not production B metadata, predict the target.
constexpr std::array<double, 6> diameters {
    0.477159e-3, 0.437895e-3, 0.412021e-3, 0.38e-3, 0.406e-3, 0.305e-3
};
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

auto fresh(double rate, float woundScale)
{
    auto engine = std::make_unique<Engine>();
    auto calibration = acustra::fittedPhysicalCalibration;
    calibration.steel.stiffnessScale = woundScale;
    engine->setPhysicalCalibration(calibration);
    engine->prepare(rate, 127);
    engine->setStringPerChannelMode(true);
    engine->setLowerZoneMemberCount(15);
    return engine;
}

double geometricB(int string, const Access::Material& material)
{
    // Woodhouse et al., Acta Acustica 98 (2012), Eqs. 1 and 3:
    // I=pi*d^4/64 and B=pi²*E*I/(T*L²). A plain monofilament requires no
    // effective-core fit. Wound strings keep the separately tested fit.
    const double secondMoment = pi * std::pow(diameters[static_cast<std::size_t>(string)], 4.0)
        / 64.0;
    return pi * pi * youngsModulus * secondMoment
        / (material.tension * material.length * material.length);
}

void testPlainSteelUsesGaugeAcrossFretsAndTension()
{
    double largestError = 0.0, largestAttackError = 0.0;
    int cases = 0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const float fittedScale : { 0.25f, 0.749355465f, 4.0f })
            for (const int string : { 4, 5 })
                for (const int fret : { 0, 5, 12, 20 })
                    for (const float bend : { 0.0f, 2.0f })
                    {
                        auto engine = fresh(rate, fittedScale);
                        engine->setPitchBend(bend, string + 1);
                        engine->noteOn(openNotes[static_cast<std::size_t>(string)] + fret,
                                       0.5f, string + 1);
                        const auto material = Access::inspect(*engine, string);
                        expect(material.played, "plain-stiffness fixture missed its physical string");
                        const double error = std::abs(material.inharmonicity
                            / geometricB(string, material) - 1.0);
                        expect(error < 3.0e-6,
                               "plain-string dispersion does not follow geometric steel EI");
                        largestError = std::max(largestError, error);

                        // Supply a known slope integral to isolate the
                        // denominator from the release waveform. Its tension
                        // increase would give 10 cents to a flexible string;
                        // the same geometric EI must resist that stretch.
                        const double diameter = diameters[static_cast<std::size_t>(string)];
                        const double area = 0.25 * pi * diameter * diameter;
                        const double displacement
                            = acustra::fittedPhysicalCalibration.steelDisplacementScaleMetres;
                        const float slope = static_cast<float>(2.0 * material.length * material.length
                            * material.tension * (std::exp2(10.0 / 600.0) - 1.0)
                            / (youngsModulus * area * displacement * displacement));
                        const double addedTension = youngsModulus * area * displacement * displacement
                            * slope / (2.0 * material.length * material.length);
                        const double expected = 600.0 * std::log2(1.0 + addedTension
                            / (material.tension * (1.0 + geometricB(string, material))));
                        const double observed = Access::controlledAttackPitch(*engine, string, slope);
                        const double attackError = std::abs(observed - expected);
                        expect(attackError < 2.0e-4,
                               "attack pitch and dispersion use different plain-string stiffness");
                        largestAttackError = std::max(largestAttackError, attackError);
                        ++cases;
                    }
    std::cout << "Plain geometric EI: " << cases << " cases, largest B relative error="
              << largestError << ", attack-pitch error=" << largestAttackError << " cents\n";
}

void testWoundCoresRetainTheirCalibration()
{
    double largestError = 0.0;
    for (const float fittedScale : { 0.25f, 0.749355465f, 4.0f })
        for (int string = 0; string < 4; ++string)
            for (const int fret : { 0, 7 })
            {
                auto engine = fresh(48000.0, fittedScale);
                engine->noteOn(openNotes[static_cast<std::size_t>(string)] + fret,
                               0.5f, string + 1);
                const auto material = Access::inspect(*engine, string);
                expect(material.played, "wound-stiffness fixture missed its physical string");
                const double error = std::abs(material.inharmonicity
                    / (geometricB(string, material) * fittedScale) - 1.0);
                expect(error < 3.0e-6, "plain-string correction changed wound effective-core stiffness");
                largestError = std::max(largestError, error);
            }
    std::cout << "Wound effective cores: largest B relative error=" << largestError << '\n';
}

void testPlainAudioIgnoresWoundFit()
{
    for (const int string : { 4, 5 })
        for (const int fret : { 0, 12 })
        {
            auto low = fresh(48000.0, 0.25f);
            auto high = fresh(48000.0, 4.0f);
            // Isolate the plain string from sympathetic wound strings,
            // whose physical stiffness legitimately still changes.
            low->setBridgeCouplingEnabled(false);
            high->setBridgeCouplingEnabled(false);
            const int note = openNotes[static_cast<std::size_t>(string)] + fret;
            low->noteOn(note, 0.5f, string + 1);
            high->noteOn(note, 0.5f, string + 1);
            std::array<float, 127> a {}, b {}, ar {}, br {};
            double energy = 0.0;
            bool identical = true;
            for (int offset = 0; offset < 12000; offset += 127)
            {
                const int count = std::min(127, 12000 - offset);
                low->process(a.data(), ar.data(), count);
                high->process(b.data(), br.data(), count);
                for (int sample = 0; sample < count; ++sample)
                {
                    identical &= a[static_cast<std::size_t>(sample)] == b[static_cast<std::size_t>(sample)]
                        && ar[static_cast<std::size_t>(sample)] == br[static_cast<std::size_t>(sample)];
                    const double value = a[static_cast<std::size_t>(sample)];
                    expect(std::isfinite(value) && std::abs(value) <= 1.0,
                           "plain-stiffness render was nonfinite or unbounded");
                    energy += value * value;
                }
            }
            expect(energy > 1.0e-10, "plain-stiffness audio witness was silent");
            expect(identical, "wound effective-core fit leaked into isolated plain-string audio");
        }
}
}

int main()
{
    testPlainSteelUsesGaugeAcrossFretsAndTension();
    testWoundCoresRetainTheirCalibration();
    testPlainAudioIgnoresWoundFit();
    return failures == 0 ? 0 : 1;
}
