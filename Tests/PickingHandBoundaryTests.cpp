#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Contact
    {
        std::array<float, 3> gesture;
        float direction, excitationDirection, point, normalShare;
        double slipPole, referencePole;
        std::array<float, 2> referencePickDelay;
        std::uint32_t drawState;
        int delay;
        bool fired;
    };
    static Contact contact(const AcustraEngine& engine, int string)
    {
        const auto& v = engine.voices_[static_cast<std::size_t>(string)];
        return { { v.pluckGesture.position, v.pluckGesture.pressure,
                   v.pluckGesture.active ? 1.0f : 0.0f },
                 v.pluckParallelSign, v.excitationParallelGain,
                 v.pluckPoint, v.polarisationMix,
                 v.releaseSlipPole, v.releaseReferencePole,
                 v.referencePickDelay, v.randomState, v.pluckDelay,
                 v.attackFired };
    }
    static int noteString(const AcustraEngine& engine, int note, int channel = 1)
    {
        for (int s = 0; s < AcustraEngine::stringCount; ++s)
        {
            const auto& v = engine.voices_[static_cast<std::size_t>(s)];
            if (v.played && v.midiNote == note && v.midiChannel == channel)
                return s;
        }
        return -1;
    }
    static std::array<float, 3> hand(const AcustraEngine& engine)
    {
        const auto& g = engine.pickingGesture_;
        return { g.position, g.pressure, g.active ? 1.0f : 0.0f };
    }
    static bool handSeen(const AcustraEngine& engine)
    { return engine.pickingGestureSeen_; }
};
}

namespace
{
using acustra::AcustraEngine;
using acustra::PickingTechnique;
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

auto fresh(bool hand = true, double rate = 48000.0,
           PickingTechnique style = PickingTechnique::Finger)
{
    auto engine = std::make_unique<AcustraEngine>();
    engine->setPerformanceRealism({ false, hand, false, false });
    acustra::EngineParameters parameters;
    parameters.picking = style;
    engine->setParameters(parameters);
    engine->prepare(rate, 257);
    return engine;
}

std::vector<float> render(AcustraEngine& engine, int samples, int block = 257)
{
    std::vector<float> audio(static_cast<std::size_t>(samples * 2));
    for (int offset = 0; offset < samples; offset += block)
    {
        const int count = std::min(block, samples - offset);
        engine.process(audio.data() + offset, audio.data() + samples + offset, count);
    }
    const bool bounded = std::all_of(audio.begin(), audio.end(), [](float v)
    { return std::isfinite(v) && std::abs(v) <= 1.0f; });
    expect(bounded, "a boundary gesture produced a nonfinite or unbounded sample");
    return audio;
}

// A new stroke can begin while the allocator is still completing an older
// chord. Moving C4 must transfer its old contact, including when its countdown
// is one and startNote fires it inside the move. Checking the fired excitation
// as well as the stored snapshot detects a restore performed too late.
void movedStrokeKeepsItsContact()
{
    for (const int lead : { 3, 1 })
        for (const bool earlyKeyUp : { false, true })
        {
            auto engine = fresh();
            engine->beginStrum();
            render(*engine, 64);
            engine->beginStrum(0, 0, true);
            engine->noteOn(64, 0.8f, 1, 150, true);
            engine->noteOn(60, 0.8f, 1, 300, true);
            const int from = Access::noteString(*engine, 60);
            expect(from >= 0, "the pending C4 fixture was not allocated");
            if (from < 0)
                continue;
            if (earlyKeyUp)
                engine->noteOff(60);
            const auto original = Access::contact(*engine, from);
            expect(original.gesture[2] == 1.0f && original.direction == -1.0f,
                   "the queued old stroke was not an active upstroke");
            const int wait = original.delay - lead;
            expect(wait >= 0, "the queued contact precedes the fixture boundary");
            if (wait < 0)
                continue;
            render(*engine, wait);
            expect(Access::contact(*engine, from).delay == lead,
                   "the old contact did not reach its requested countdown");
            engine->beginStrum(0, 0, false);
            expect(Access::hand(*engine) != original.gesture,
                   "the later stroke failed to establish a distinct snapshot");
            engine->noteOn(67, 0.8f);
            const int to = Access::noteString(*engine, 60);
            expect(to >= 0 && to != from,
                   "the C4/E4/G4 fixture did not move the pending C4");
            if (to < 0)
                continue;
            const auto moved = Access::contact(*engine, to);
            expect(moved.gesture == original.gesture
                       && moved.direction == original.direction,
                   "moving a pending note replaced its original stroke snapshot/direction");
            expect(moved.fired == (lead == 1),
                   "moving a pending note changed its original contact deadline");
            if (lead > 1)
            {
                expect(moved.delay == lead, "moving a contact changed its remaining delay");
                render(*engine, lead);
            }
            const auto fired = Access::contact(*engine, to);
            expect(fired.fired && fired.gesture == original.gesture
                       && fired.direction == -1.0f
                       && fired.excitationDirection == -0.51f,
                   "the moved contact sounded with a later stroke's hand or direction");
        }
}

// The two engines have identical noise histories. Explicit member controls
// supplied after scheduling must own the eventual point, angle and compliance.
// An active hand is essential: first-contact parity alone misses this branch.
void lateMemberExpressionOwnsTheContact()
{
    for (const auto style : { PickingTechnique::Finger, PickingTechnique::Pick,
                              PickingTechnique::Thumb })
        for (const double rate : { 44100.0, 96000.0 })
        {
            auto hand = fresh(true, rate, style);
            auto reference = fresh(false, rate, style);
            for (auto* engine : { hand.get(), reference.get() })
            {
                engine->setStringPerChannelMode(true);
                engine->setLowerZoneMemberCount(15);
                engine->noteOn(45, 0.45f, 2);
            }
            const int interval = static_cast<int>(0.012 * rate);
            expect(render(*hand, interval) == render(*reference, interval),
                   "MPE fixtures differed before the hand acquired any history");
            for (auto* engine : { hand.get(), reference.get() })
            {
                engine->noteOn(47, 0.7f, 2, 17);
                engine->setMpeTimbre(0.82f, 2);
                engine->setMpePressure(0.63f, 2);
            }
            expect(render(*hand, 17) == render(*reference, 17),
                   "late MPE controls changed audio before the scheduled contact");
            expect(!Access::contact(*hand, 1).fired,
                   "the late-expression contact fired before its due sample");
            expect(render(*hand, 1) == render(*reference, 1),
                   "the expressed contact differs from the explicit-control reference");
            const auto actual = Access::contact(*hand, 1);
            const auto expected = Access::contact(*reference, 1);
            expect(actual.gesture[2] == 1.0f && actual.fired,
                   "late MPE fixture did not exercise an active hand");
            expect(actual.point == expected.point && actual.normalShare == expected.normalShare
                       && actual.slipPole == expected.slipPole
                       && actual.referencePole == expected.referencePole,
                   "automatic hand motion overrode late explicit member expression");
            expect(actual.drawState == expected.drawState,
                   "explicit member expression changed the independent contact draw count");
            expect(render(*hand, 257) == render(*reference, 257),
                   "explicit MPE contact left a different released wave or noise history");
        }
}

void extremeRatesKeepRepeatedPicksBounded()
{
    for (const double rate : { 8000.0, 192000.0, 384000.0 })
    {
        auto single = fresh(true, rate, PickingTechnique::Pick);
        auto blocks = fresh(true, rate, PickingTechnique::Pick);
        for (auto* engine : { single.get(), blocks.get() })
        {
            engine->setStringPerChannelMode(true);
            engine->noteOn(52, 0.42f, 1);
        }
        const int interval = static_cast<int>(0.008 * rate);
        expect(render(*single, interval, 1) == render(*blocks, interval, 257),
               "the first extreme-rate Pick depends on host block partition");
        for (auto* engine : { single.get(), blocks.get() })
        {
            engine->noteOn(52, 0.42f, 1, 3);
            engine->setPitchBend(1.5f, 1);
        }
        expect(render(*single, 4, 1) == render(*blocks, 4, 257),
               "a queued extreme-rate re-pick depends on host block partition");
        const auto actual = Access::contact(*single, 0);
        expect(actual.fired && actual.gesture[2] == 1.0f,
               "the extreme-rate re-pick never reached an active hand contact");
        expect(std::all_of(actual.referencePickDelay.begin(), actual.referencePickDelay.end(),
                   [](float delay) { return std::isfinite(delay) && delay > 0.0f; }),
               "the extreme-rate repeated Pick lost its physical reference geometry");
        const auto a = render(*single, interval, 1);
        const auto b = render(*blocks, interval, 257);
        expect(a == b, "the extreme-rate released wave depends on host block partition");
        expect(std::any_of(a.begin(), a.end(), [](float sample) { return sample != 0.0f; }),
               "the extreme-rate repeated Pick became silent");
    }
}

void panicKeepsOtherChannelsAndCancelsQueuedContacts()
{
    auto engine = fresh();
    engine->setStringPerChannelMode(true);
    engine->noteOn(52, 0.5f, 3);
    engine->noteOn(64, 0.5f, 6);
    render(*engine, 64);
    engine->beginStrum(0, 0, true);
    engine->noteOn(52, 0.5f, 3, 100, true);
    engine->noteOn(64, 0.5f, 6, 200, true);
    const auto survivor = Access::contact(*engine, 5);
    const auto posture = Access::hand(*engine);
    engine->allSoundOff(3);
    expect(Access::noteString(*engine, 52, 3) < 0
               && Access::contact(*engine, 2).delay == 0,
           "channel panic retained a canceled string's scheduled contact");
    const auto remaining = Access::contact(*engine, 5);
    expect(engine->heldString(64, 6) == 5
               && remaining.gesture == survivor.gesture
               && remaining.direction == survivor.direction
               && remaining.delay == survivor.delay
               && remaining.drawState == survivor.drawState
               && Access::hand(*engine) == posture,
           "channel panic altered another channel's queued stroke or shared hand");
    render(*engine, survivor.delay);
    expect(Access::contact(*engine, 5).excitationDirection == -0.51f,
           "the surviving channel lost its pending upstroke");
    engine->noteOn(64, 0.5f, 6, 200, true);
    engine->allSoundOff(6);
    expect(engine->getActiveVoiceCount() == 0 && !Access::handSeen(*engine),
           "global silence after panic retained an active hand history");
    for (int s = 0; s < AcustraEngine::stringCount; ++s)
        expect(Access::contact(*engine, s).delay == 0,
               "panic left a contact queued on a silent string");
    const auto silence = render(*engine, 512);
    expect(std::all_of(silence.begin(), silence.end(), [](float v) { return v == 0.0f; }),
           "a canceled stroke sounded after global panic");
    engine->noteOn(64, 0.5f, 6);
    expect(Access::contact(*engine, 5).gesture[2] == 0.0f,
           "the first contact after panic inherited stale coherent variation");
    render(*engine, 128);
}
}

int main()
{
    movedStrokeKeepsItsContact();
    lateMemberExpressionOwnsTheContact();
    extremeRatesKeepRepeatedPicksBounded();
    panicKeepsOtherChannelsAndCancelsQueuedContacts();
    if (failures == 0)
        std::cout << "Picking-hand ownership, late expression, rate and panic boundaries passed.\n";
    return failures == 0 ? 0 : 1;
}
