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
    struct Snapshot
    {
        double length, frequency, tension;
        bool played;
    };
    static Snapshot peak(AcustraEngine& engine, int string, float wheel = 1.0f)
    {
        engine.setVibrato(wheel);
        engine.vibratoOnset_ = 1.0f;
        engine.vibratoPhase_ = 3.14159265358979323846f;
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        voice.attackPitchCents = 0.0f;
        engine.configureVoice(voice, string, voice.midiNote, false);
        return { voice.speakingLengthMetres, voice.pitchGeometry.value.frequency,
                 voice.tensionNewtons, voice.played };
    }
    static void harmonic(AcustraEngine& engine, int string)
    {
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        voice.harmonic = 2;
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> openNotes { 40, 45, 50, 55, 59, 64 };
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

auto fresh(double rate, int string, int fret, bool member)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(rate, 64);
    engine->setStringPerChannelMode(true);
    if (member) engine->setLowerZoneMemberCount(15);
    engine->noteOn(openNotes[static_cast<std::size_t>(string)] + fret,
                   0.5f, string + 1);
    return engine;
}

void slide(Engine& engine, int string, bool member, float semitones)
{
    engine.setPitchBend(semitones, member ? 1 : string + 1);
}

double peakDepth(Engine& engine, int string)
{
    const auto rest = Access::peak(engine, string, 0.0f);
    const auto moving = Access::peak(engine, string);
    expect(rest.played && moving.played, "stopping fixture missed its physical string");
    expect(rest.length == moving.length, "vibrato moved the stopping position");
    return 1200.0 * std::log2(moving.frequency / rest.frequency);
}

void testCurrentStopControlsVibrato()
{
    double worst = 0.0;
    int cases = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (bool member : { false, true })
            for (int string : { 1, 4, 5 })
                for (int originalFret : { 0, 5 })
                {
                    auto engine = fresh(rate, string, originalFret, member);
                    // Move both toward and away from the nut in one history.
                    // A stale prior speaking length must not decide the new
                    // vibrato interval or enter its configuration cache key.
                    for (float actualFret : { 5.0f, 0.1f, 0.0f, 0.25f, 1.0f, -1.0f })
                    {
                        slide(*engine, string, member, actualFret - originalFret);
                        const double actualDepth = peakDepth(*engine, string);
                        const double length = 0.648 * std::exp2(-actualFret / 12.0);
                        // The 18 mm unloading distance is an authored finite
                        // contact extent shared with release, not measured
                        // physiology. Every true fret is beyond this region.
                        const double x = std::clamp((0.648 - length) / 0.018, 0.0, 1.0);
                        const double expectedDepth = 20.0 * x * x * (3.0 - 2.0 * x);
                        const double error = std::abs(actualDepth - expectedDepth);
                        worst = std::max(worst, error);
                        expect(error < 0.001,
                               "vibrato follows the original note instead of the current physical stop");
                        ++cases;
                    }
                }
    std::cout << "Current-stop vibrato: " << cases << " cases, largest depth error="
              << worst << " cents\n";
}

void testMemberTensionPressureAndHarmonicGuards()
{
    for (int string : { 1, 4, 5 })
        for (int fret : { 0, 5 })
        {
            auto engine = fresh(48000.0, string, fret, true);
            for (float memberBend : { -2.0f, 0.0f, 2.0f })
            {
                engine->setPitchBend(memberBend, string + 1);
                const double depth = peakDepth(*engine, string);
                expect(std::abs(depth - (fret == 0 ? 0.0 : 20.0)) < 0.001,
                       "a member tension bend changed stopping-hand eligibility");
            }
            if (fret > 0)
            {
                for (float pressure : { 0.0f, 0.5f, 1.0f })
                {
                    engine->setMpePressure(pressure, string + 1);
                    expect(std::abs(peakDepth(*engine, string)
                        - 20.0 * (0.5 + 0.5 * pressure)) < 0.001,
                        "physical-stop eligibility changed the existing pressure depth law");
                }
            }
            Access::harmonic(*engine, string);
            slide(*engine, string, true, 5.0f);
            expect(peakDepth(*engine, string) == 0.0,
                   "a harmonic's light touch became a stopping-finger vibrato");
        }
}

void testNutAndContactEdgeContinuity()
{
    auto engine = fresh(48000.0, 4, 5, false);
    const double contactEdge = -12.0 * std::log2((0.648 - 0.018) / 0.648);
    double largestFineGap = 0.0;
    for (double centre : { 0.0, contactEdge })
    {
        std::array<double, 2> differences {};
        int index = 0;
        for (double epsilon : { 0.01, 0.0001 })
        {
            slide(*engine, 4, false, static_cast<float>(centre - epsilon - 5.0));
            const double below = peakDepth(*engine, 4);
            slide(*engine, 4, false, static_cast<float>(centre + epsilon - 5.0));
            const double above = peakDepth(*engine, 4);
            differences[static_cast<std::size_t>(index++)] = std::abs(above - below);
        }
        expect(differences[1] < 0.001 && differences[1] < 0.02 * differences[0],
               "vibrato eligibility jumps at the nut or finite contact edge");
        largestFineGap = std::max(largestFineGap, differences[1]);
    }
    std::cout << "Vibrato stopping boundary: largest fine gap=" << largestFineGap << " cents\n";
}
}

int main()
{
    testCurrentStopControlsVibrato();
    testMemberTensionPressureAndHarmonicGuards();
    testNutAndContactEdgeContinuity();
    return failures == 0 ? 0 : 1;
}
