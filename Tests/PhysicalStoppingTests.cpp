// The hand responds to the physical stopping point, including fractional
// conventional/manager slides, rather than the original key's fret number.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Stop
    {
        float time, length, pluck, coupling, memoryGain;
        double position, noisePosition;
        float noiseCorner, noiseTouch, noiseBrush;
        std::uint32_t randomState;
        bool unchangedWave;
    };

    static Stop release(AcustraEngine& engine, int string)
    {
        auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        // Compare the full directional lines, not just a bridge sample.
        auto before = std::make_unique<decltype(voice.loops)>(voice.loops);
        engine.beginRelease(voice, string);
        const auto& contact = voice.loops[0].gestureContact;
        const double first = contact.firstAge + contact.firstFraction;
        const double second = contact.secondAge + contact.secondFraction;
        const auto& tap = voice.releaseNoiseTravel.taps[0];
        const double fractional = tap.order == 2
            ? 2.0 * (1.0 - tap.a2) / (1.0 + tap.a1 + tap.a2)
            : tap.order == 1 ? (1.0 - tap.a1) / (1.0 + tap.a1) : 0.0;
        return { voice.releaseSeconds, voice.speakingLengthMetres, voice.pluckPoint,
            contact.coupling, contact.memoryGain,
            2.0 * first / (first + second),
            2.0 * (tap.whole + fractional) * engine.loopFundamental(voice) / engine.sampleRate_,
            voice.releaseNoiseCoefficient, voice.releaseNoiseTouch, voice.releaseNoiseBrush,
            voice.releaseNoiseState,
            (*before)[0].delay == voice.loops[0].delay
                && (*before)[1].delay == voice.loops[1].delay };
    }

    static Stop isolated(float length, int keyFret, PickingTechnique technique,
                         int harmonic = 1, double rate = 48000.0)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->sampleRate_ = rate;
        engine->parameters_.picking = technique;
        engine->targetParameters_.releaseNoise = 0.5f;
        auto& voice = engine->voices_[0];
        voice.played = true;
        voice.fret = keyFret;
        voice.midiNote = voice.openMidi + keyFret;
        voice.harmonic = harmonic;
        voice.speakingLengthMetres = length;
        voice.characteristicImpedance = 0.7f;
        voice.appliedBendImpedanceScale = 1.0f;
        voice.pluckPoint = 0.22f;
        voice.level = 0.2f;
        voice.peakLevel = 0.7f;
        voice.releaseNoiseState = 0x13579bdu;
        for (auto& loop : voice.loops)
        {
            loop.currentDelay = loop.targetDelay = 240.375f;
            loop.delay[21] = 0.003f;
            loop.delay[39] = -0.002f;
        }
        return release(*engine, 0);
    }

    static Stop routed(double rate, int keyFret, float slide, float memberBend,
                       PickingTechnique technique)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 127);
        EngineParameters parameters;
        parameters.picking = technique;
        parameters.releaseNoise = 0.5f;
        engine->setParameters(parameters);
        engine->setLowerZoneMemberCount(15);
        engine->setPitchBend(slide, 1);
        engine->setPitchBend(memberBend, 2);
        engine->noteOn(40 + keyFret, 0.7f, 2);
        for (int string = 0; string < AcustraEngine::stringCount; ++string)
            if (engine->voices_[static_cast<std::size_t>(string)].played)
                return release(*engine, string);
        return {};
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
using Picking = acustra::PickingTechnique;
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

double expectedWeight(double length)
{
    // Physical reference uses metres, independently of the engine's helper.
    const double distance = std::clamp((double(0.648f) - length) / double(0.018f), 0.0, 1.0);
    return distance * distance * (3.0 - 2.0 * distance);
}

void testLossAndGeometryContinuum()
{
    double maximumRateError = 0.0, maximumPositionError = 0.0;
    for (auto technique : { Picking::Finger, Picking::Pick, Picking::Thumb })
        for (int keyFret : { 0, 8 })
        {
            Access::Stop previous {};
            for (int probe = 0; probe <= 160; ++probe)
            {
                const float length = 0.652f - 0.027f * static_cast<float>(probe) / 160.0f;
                const auto stop = Access::isolated(length, keyFret, technique);
                const double weight = expectedWeight(length);
                const double rate = (1.0 - weight) / double(1.25f) + weight / double(0.32f);
                const double openPosition = technique == Picking::Pick ? 0.028 / length : 0.22;
                const double position = (1.0 - weight) * openPosition
                    + weight * (1.0 - 0.018 / length);
                const double rateError = std::abs(1.0 / stop.time - rate);
                const double positionError = std::abs(stop.position - position);
                maximumRateError = std::max(maximumRateError, rateError);
                maximumPositionError = std::max(maximumPositionError, positionError);
                expect(rateError < 2.0e-6, "physical stopping did not mix dissipative rates");
                expect(positionError < 3.0e-7, "physical stopping did not follow the effective pad centroid");
                expect(stop.unchangedWave, "establishing the stopping contact injected a new wave");
                if (weight == 0.0) expect(stop.time == 1.25f, "open endpoint changed its established release time");
                if (weight == 1.0) expect(stop.time == 0.32f, "fretted endpoint changed its established release time");
                if (probe > 0)
                {
                    expect(stop.time <= previous.time, "shortening the string weakened the damping contact");
                    expect(stop.randomState == previous.randomState,
                           "crossing the physical nut changed the key's release RNG schedule");
                    expect(std::abs(stop.noiseCorner - previous.noiseCorner) < 0.004
                               && std::abs(stop.noiseTouch - previous.noiseTouch) < 0.08
                               && std::abs(stop.noiseBrush - previous.noiseBrush) < 0.0006
                               && std::abs(stop.noisePosition - previous.noisePosition) < 0.025,
                           "a continuous stop sweep stepped release sound or position");
                }
                previous = stop;
            }
        }
    std::cout << "Physical stop max loss-rate error: " << maximumRateError
              << "; pad position error: " << maximumPositionError << '\n';
}

void testRealSlideAndMemberTensionRouting()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (auto technique : { Picking::Finger, Picking::Pick, Picking::Thumb })
        {
            const auto open = Access::routed(rate, 0, 0.0f, 0.0f, technique);
            const auto up = Access::routed(rate, 0, 12.0f, 0.0f, technique);
            const auto fretted = Access::routed(rate, 3, 0.0f, 0.0f, technique);
            const auto down = Access::routed(rate, 3, -3.0f, 0.0f, technique);
            expect(open.time == 1.25f && up.time == 0.32f
                       && fretted.time == 0.32f && down.time == 1.25f,
                   "manager slide kept the original key's release category");
            expect(std::abs(up.length / open.length - 0.5) < 2.0e-6
                       && std::abs(down.length / open.length - 1.0) < 2.0e-6,
                   "routed stopping fixture did not reach its requested speaking length");
            for (int keyFret : { 0, 3 })
            {
                const auto plain = Access::routed(rate, keyFret, 0.0f, 0.0f, technique);
                const auto tension = Access::routed(rate, keyFret, 0.0f, 7.0f, technique);
                expect(plain.length == tension.length && plain.time == tension.time,
                       "MPE tension bend moved the physical stopping regime");
                // A changed pluck position is legitimate: the hand follows
                // the bent string's configured attack. Its stop weight is
                // the quantity required to remain independent of tension.
            }
        }
    for (float length : { 0.8f, 0.648f, 0.639f, 0.3f })
    {
        const auto harmonic = Access::isolated(length, 12, Picking::Finger, 3);
        expect(harmonic.time == 1.25f && std::abs(harmonic.position - 0.22) < 2.0e-7,
               "natural harmonic acquired a fictitious fretting contact");
    }
}
}

int main()
{
    testLossAndGeometryContinuum();
    testRealSlideAndMemberTensionRouting();
    if (failures != 0) return 1;
    std::cout << "Physical stopping checks passed.\n";
    return 0;
}
