#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
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
    // The played string's coupled detune once the palm has moved from where
    // the note was struck to where it is held, and settled there.
    static float settledDetune(int note, float struck, float held)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->setPalmMutePressure(struck);
        engine->prepare(48000.0, 256);
        engine->noteOn(note, 0.6f);
        std::array<float, 256> left {}, right {};
        engine->process(left.data(), right.data(), 256);
        engine->setPalmMutePressure(held);
        for (int block = 0; block < 60; ++block)
            engine->process(left.data(), right.data(), 256);
        if (engine->palmMute_ != held)
            return std::numeric_limits<float>::quiet_NaN();
        for (const auto& voice : engine->voices_)
            if (voice.played && voice.midiNote == note)
                return voice.polarisationDetune;
        return std::numeric_limits<float>::quiet_NaN();
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

void testAPalmThatMovesLeavesTheStringTunedForWhereItRests()
{
    // The coupled pair is tuned on the palm's target and solves once per
    // change (configureVoice). Once the palm settles, a note must be tuned
    // as one struck there: tuning on the palm at contact left MIDI 42 9.3
    // cents out once a full palm lifted, and nothing else plays that.
    using Access = acustra::AcustraEngineTestAccess;
    double worstCents = 0.0;
    for (int note : { 40, 42, 47, 52, 57, 64, 71, 76 })
        for (const auto& [struck, held] : { std::pair { 1.0f, 0.0f },
                                            std::pair { 0.0f, 0.6f },
                                            std::pair { 1.0f, 0.3f } })
        {
            const float moved = Access::settledDetune(note, struck, held);
            const float rested = Access::settledDetune(note, held, held);
            expect(std::isfinite(moved) && std::isfinite(rested),
                   "the palm did not settle on a sounding note");
            const double cents = 1200.0 * std::log2((1.0 + double(moved)) / (1.0 + double(rested)));
            worstCents = std::max(worstCents, std::abs(cents));
            expect(moved == rested,
                   "a palm that moved left the string tuned for where it was struck");
        }
    std::cout << "Palm moved after contact: worst tuning difference from a note struck there "
              << worstCents << " cents\n";
}

// The pair now reads the palm's target as soon as CC2 sets it, between
// control periods, so a palm moved mid-note (including in the same sample
// as a note-on) must render the same bits at every host block size.
std::vector<float> renderMovingPalm(int block)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters parameters;
    parameters.picking = acustra::PickingTechnique::Pick;
    engine->setParameters(parameters);
    engine->prepare(48000.0, 512);
    struct Event { int at; bool note; float value; };
    constexpr std::array<Event, 8> events { {
        { 0, true, 52.0f }, { 0, true, 59.0f }, { 3001, false, 1.0f }, { 3001, true, 64.0f },
        { 9000, false, 0.3f }, { 9137, true, 47.0f }, { 15000, false, 0.0f }, { 15011, false, 0.8f } } };
    constexpr int frames = 30000;
    std::vector<float> left(frames), right(frames);
    std::size_t next = 0;
    for (int frame = 0; frame < frames;)
    {
        for (; next < events.size() && events[next].at == frame; ++next)
        {
            if (events[next].note)
                engine->noteOn(static_cast<int>(events[next].value), 0.7f);
            else
                engine->setPalmMutePressure(events[next].value);
        }
        int count = std::min(block, frames - frame);
        if (next < events.size())
            count = std::min(count, events[next].at - frame);
        engine->process(left.data() + frame, right.data() + frame, count);
        frame += count;
    }
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void testAMovingPalmIsBlockPartitionInvariant()
{
    const auto reference = renderMovingPalm(256);
    for (int block : { 1, 17, 64, 333, 512 })
        expect(renderMovingPalm(block) == reference,
               "a moving palm's audio depended on the host block size");
}
}

int main()
{
    testSlideAndPhysicalFretHaveTheSameHandLoss();
    testHandLossKeepsItsTimeScaleOnBendsAndRetainedWaves();
    testAPalmThatMovesLeavesTheStringTunedForWhereItRests();
    testAMovingPalmIsBlockPartitionInvariant();
    if (failures == 0) std::cout << "Palm-mute pitch tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
