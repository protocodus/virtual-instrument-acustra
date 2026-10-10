#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Loss
    {
        double gain, broadPole, broadMix, highPole, highMix;
        double bendingGain, bendingA1, bendingA2;
    };
    struct State
    {
        Loss dry, held, tail;
        double frequency;
    };
    static State state(int string, int fret, double rate, float slide,
                       float member, float attackCents, float pressure,
                       float age = 0.15f)
    {
        auto engine = std::make_unique<AcustraEngine>();
        EngineParameters parameters;
        parameters.stringAge = age;
        engine->setParameters(parameters);
        engine->prepare(rate, 64);
        engine->setLowerZoneMemberCount(15);
        auto& voice = engine->voices_[static_cast<std::size_t>(string)];
        voice.played = voice.keyDown = voice.mpeMember = true;
        voice.midiChannel = 2;
        voice.midiNote = voice.openMidi + fret;
        voice.attackPitchCents = attackCents;
        engine->setPitchBend(slide, 1);
        engine->setPitchBend(member, 2);
        const auto loss = [] (const auto& loop)
        {
            return Loss { loop.loopGain, loop.broadLossCoefficient,
                loop.broadLossMix, loop.lowpassCoefficient, loop.highLossMix,
                loop.bendingLossGain, loop.bendingLossA1, loop.bendingLossA2 };
        };
        engine->configureVoice(voice, string, voice.midiNote, false);
        State result {};
        result.dry = loss(voice.loops[0]);
        voice.level = 0.01f;
        engine->captureTail(voice);
        // The retained string must accept the same physical hand contact
        // after capture, including when its original note was slid or bent
        // and the main voice has since moved to a different fret and pitch.
        const int capturedNote = voice.midiNote;
        engine->setPitchBend(slide + 5.0f, 1);
        engine->configureVoice(voice, string, capturedNote + 2, false);
        engine->palmMute_ = pressure;
        engine->updateTailHandLoss(voice);
        result.tail = loss(voice.tailLoop);
        engine->setPitchBend(slide, 1);
        engine->configureVoice(voice, string, capturedNote, false);
        result.held = loss(voice.loops[0]);
        result.frequency = rate / voice.contactPeriodSamples;
        return result;
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

double decay(const acustra::AcustraEngineTestAccess::Loss& loss,
             double fundamental, int partial, double rate)
{
    const double omega = 2.0 * std::acos(-1.0) * fundamental * partial / rate;
    const double lossOmega = rate == 48000.0 ? omega
        : 2.0 * std::atan(rate / 48000.0 * std::tan(0.5 * omega));
    const auto shelf = [lossOmega] (double pole, double mix)
    {
        const auto low = (1.0 - pole) / (1.0 - pole * std::polar(1.0, -lossOmega));
        return std::abs(1.0 - mix + mix * low);
    };
    const auto z = std::polar(1.0, -omega);
    const double magnitude = loss.gain * shelf(loss.broadPole, loss.broadMix)
        * shelf(loss.highPole, loss.highMix) * loss.bendingGain
        / std::abs(1.0 + loss.bendingA1 * z + loss.bendingA2 * z * z);
    return -20.0 * fundamental * std::log10(magnitude);
}

void testSlideAndPhysicalFretHaveTheSameHandLoss()
{
    // A manager wheel moves the stopping point at unchanged tension. The
    // same string, speaking length and hand must therefore have the same
    // decay, regardless of which MIDI note requested that stopping point.
    using Access = acustra::AcustraEngineTestAccess;
    double worstAddedDecayError = 0.0;
    int comparisons = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int string : { 0, 2, 5 })
            for (int slide : { -2, 2, 12 })
                for (float pressure : { 0.5f, 1.0f })
                    for (float age : { 0.15f, 0.75f })
                    {
                        const auto moved = Access::state(string, 5, rate,
                            static_cast<float>(slide), 0.0f, 0.0f, pressure, age);
                        const auto stopped = Access::state(string, 5 + slide,
                            rate, 0.0f, 0.0f, 0.0f, pressure, age);
                        for (int partial : { 1, 4, 12 })
                        {
                            const double movedRate = decay(moved.held, moved.frequency, partial, rate)
                                - decay(moved.dry, moved.frequency, partial, rate);
                            const double stoppedRate = decay(stopped.held, stopped.frequency, partial, rate)
                                - decay(stopped.dry, stopped.frequency, partial, rate);
                            worstAddedDecayError = std::max(worstAddedDecayError,
                                std::abs(movedRate - stoppedRate));
                            ++comparisons;
                        }
                    }
    std::cout << comparisons << " slide/fret hand-loss comparisons: worst error "
              << worstAddedDecayError << " dB/s\n";
    expect(worstAddedDecayError < 0.02,
           "moving the fret by pitch wheel changes the hand's decay rate");
}

void testHandLossKeepsItsTimeScaleOnBendsAndRetainedWaves()
{
    using Access = acustra::AcustraEngineTestAccess;
    double worstTimeError = 0.0, worstTailError = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int string : { 0, 2, 5 })
            for (float slide : { -12.0f, 0.0f, 12.0f })
                for (float member : { -2.0f, 0.0f, 2.0f })
                    for (float attack : { 0.0f, 20.0f })
                    {
                        const auto reference = Access::state(string, 5, rate,
                            0.0f, 0.0f, 0.0f, 0.7f);
                        const auto moved = Access::state(string, 5, rate,
                            slide, member, attack, 0.7f);
                        // The high shelf's asymptotic extra attenuation per
                        // second must be invariant when a loop runs faster.
                        const auto rateOfLoss = [] (const auto& state)
                        {
                            return -state.frequency * std::log(
                                (1.0 - state.held.highMix) / (1.0 - state.dry.highMix));
                        };
                        worstTimeError = std::max(worstTimeError,
                            std::abs(rateOfLoss(moved) / rateOfLoss(reference) - 1.0));
                        for (int partial : { 1, 4, 12 })
                            worstTailError = std::max(worstTailError, std::abs(
                                decay(moved.tail, moved.frequency, partial, rate)
                                - decay(moved.held, moved.frequency, partial, rate)));
                    }
    std::cout << "Hand time-scale error " << 100.0 * worstTimeError
              << "%; retained/held decay error " << worstTailError << " dB/s\n";
    expect(worstTimeError < 0.0001, "bending changed the hand's time scale");
    expect(worstTailError < 0.02, "the retained wave uses a different hand loss");
}
}

int main()
{
    testSlideAndPhysicalFretHaveTheSameHandLoss();
    testHandLossKeepsItsTimeScaleOnBendsAndRetainedWaves();
    if (failures == 0) std::cout << "Palm-mute pitch tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
