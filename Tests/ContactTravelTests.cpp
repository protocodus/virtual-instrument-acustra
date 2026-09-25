// Native lifecycle and physical-wave checks for the finite contact transport.
// The source is a displacement-wave burst, not a measured contact force.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

namespace acustra
{
struct AcustraEngineTestAccess
{
    using Travel = AcustraEngine::ContactTravel;
    static auto& voice(AcustraEngine& engine) { return engine.voices_[0]; }
    static void advance(AcustraEngine& engine, int samples)
    {
        std::array<float, 128> left {}, right {};
        while (samples > 0)
        {
            const int count = std::min(samples, 128);
            engine.process(left.data(), right.data(), count);
            samples -= count;
        }
    }
    static double pending(Travel travel)
    {
        double energy = 0.0;
        for (int n = 0; n < 16384; ++n)
        {
            const auto paths = travel.process(0.0f);
            const double value = (paths[0] - paths[1]) * std::sqrt(0.5);
            energy += value * value;
        }
        return energy;
    }
    static void queueOnly(AcustraEngine& engine, bool tail = false)
    {
        auto& v = voice(engine);
        v.excitationEnvelope = 0.0f;
        for (auto& loop : v.loops) loop.reset();
        if (tail) v.tailLoop.reset();
        engine.resetSoundState();
    }
    static void fixedDamping(AcustraEngine& engine, float vertical, float horizontal)
    {
        auto& v = voice(engine);
        v.keyDown = false;
        v.pedalHeld = false;
        v.releaseDamping = vertical;
        v.returnSamples = 480000;
        for (auto& loop : v.loops)
            loop.appliedReleaseGain = loop.requestedReleaseGain = vertical;
        // Only the first sample is compared for independent polarization loss;
        // no artificial mismatch with the public shared damping persists.
        v.loops[1].appliedReleaseGain = horizontal;
    }
    static float lastWritten(const AcustraEngine::StringLoop& loop)
    { return loop.delay[static_cast<std::size_t>((loop.writeIndex + 8191) % 8192)]; }
    static void disableDrained(AcustraEngine& engine)
    {
        for (auto& v : engine.voices_)
            if (!v.contactTravel.active && v.excitationEnvelope <= 1.0e-8f)
                v.contactTravelEnabled = false;
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
auto fresh(double rate = 48000.0)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    return engine;
}

void testAllpassWaveEnergyAndArrival()
{
    double worstEnergy = 0.0, worstDelay = 0.0;
    for (float delay : { 0.0f, 0.10416667f, 0.55f, 1.1f, 1.999f,
                         2.1f, 10.25f, 453.72f, 8189.0f })
    {
        Access::Travel travel;
        travel.reset(delay, delay);
        for (const auto& tap : travel.taps)
            expect(std::abs(tap.a1) + std::abs(tap.a2) < 1.0,
                   "retirement proof's absolute feedback bound is invalid");
        double energy = 0.0, derivativeEnergy = 0.0, dc = 0.0, firstMoment = 0.0;
        double previous = 0.0;
        for (int n = 0; n < 16384; ++n)
        {
            const auto paths = travel.process(n == 0 ? 1.0f : 0.0f);
            expect(paths[0] == paths[1], "equal paths disagree");
            const double sample = paths[0];
            energy += sample * sample;
            derivativeEnergy += (sample - previous) * (sample - previous);
            previous = sample;
            dc += sample;
            firstMoment += n * sample;
        }
        worstEnergy = std::max({ worstEnergy, std::abs(energy - 1.0),
                                std::abs(derivativeEnergy / 2.0 - 1.0) });
        worstDelay = std::max(worstDelay, std::abs(firstMoment - delay));
        expect(std::abs(energy - 1.0) < 2e-6 && std::abs(derivativeEnergy - 2.0) < 4e-6,
               "fixed allpass changed emitted wave or derivative energy");
        expect(std::abs(dc - 1.0) < 1e-6 && std::abs(firstMoment - delay) < 1e-3,
               "allpass DC gain or low-frequency delay is wrong");
        expect(!travel.active, "zero-input transport failed to retire");
    }
    Access::Travel travel;
    travel.reset(3.0f, 9.0f);
    for (int n = 0; n < 24; ++n)
    {
        const auto paths = travel.process(n == 0 ? 1.0f : 0.0f);
        const float combined = paths[0] - paths[1];
        expect(combined == (n == 3 ? 1.0f : n == 9 ? -1.0f : 0.0f),
               "direct/nut arrival or fixed-nut displacement sign is wrong");
        if (n >= 3 && n < 9)
            expect(travel.active, "zero gap retired a still-pending reflected packet");
    }
    std::cout << "Contact allpass worst relative energy=" << worstEnergy
              << " DC delay error=" << worstDelay << " samples\n";
}

void testOrdinaryAndPedalRelease()
{
    for (double rate : { 8000.0, 48000.0, 96000.0, 384000.0 })
    {
        auto engine = fresh(rate);
        engine->noteOn(43, 1.0f, 1);
        Access::advance(*engine, std::max(1, static_cast<int>(0.001 * rate)));
        auto& voice = Access::voice(*engine);
        expect(voice.contactTravel.active && voice.excitationEnvelope > 1.0e-8f,
               "early release probe did not contain an active source and travelling waves");
        engine->noteOff(43, 1, 1.0f);
        expect(voice.excitationEnvelope == 0.0f && voice.contactTravel.active,
               "ordinary release continued emission or discarded already emitted waves");

        engine = fresh(rate);
        engine->setSustainPedal(true);
        engine->noteOn(43, 1.0f, 1);
        Access::advance(*engine, std::max(1, static_cast<int>(0.001 * rate)));
        engine->noteOff(43, 1, 1.0f);
        expect(Access::voice(*engine).excitationEnvelope > 1.0e-8f,
               "pedal-held key-up prematurely changed the contact");
        engine->setSustainPedal(false);
        expect(Access::voice(*engine).excitationEnvelope == 0.0f,
               "pedal-up did not end ordinary contact emission");
    }
    auto engine = fresh();
    engine->setLegato(true);
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 48);
    const float envelope = Access::voice(*engine).excitationEnvelope;
    engine->noteOff(43, 1, 1.0f);
    expect(Access::voice(*engine).excitationEnvelope == envelope,
           "ordinary-release change altered an explicitly requested legato lift");

    engine = fresh();
    engine->noteOn(43, 1.0f, 1, 200);
    engine->noteOff(43);
    Access::advance(*engine, 256);
    expect(!Access::voice(*engine).contactTravel.active
           && Access::voice(*engine).excitationEnvelope == 0.0f,
           "cancelled scheduled pluck emitted a contact burst");
}

double queuedArrivalEnergy(float gain)
{
    auto engine = fresh();
    engine->setSympatheticStringsEnabled(false);
    engine->setBridgeCouplingEnabled(false);
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 48);
    Access::queueOnly(*engine);
    Access::fixedDamping(*engine, gain, gain);
    std::array<float, 1> left {}, right {};
    double energy = 0.0;
    // Less than one complete period: no new source or returned wave can
    // confound whether the already-emitted first arrival receives damping.
    for (int n = 0; n < 128; ++n)
    {
        engine->process(left.data(), right.data(), 1);
        const double sample = Access::lastWritten(Access::voice(*engine).loops[0]);
        energy += sample * sample;
    }
    return energy;
}

void testArrivalDampingAndTailRetention()
{
    const double open = queuedArrivalEnergy(1.0f);
    const double half = queuedArrivalEnergy(0.5f);
    const double stopped = queuedArrivalEnergy(0.0f);
    expect(open > 1.0e-8 && stopped == 0.0,
           "queued arrival bypassed complete hand damping");
    expect(std::abs(half / open - 0.25) < 2e-6,
           "queued first-arrival amplitude does not receive the applied hand gain");

    {
        auto planes = fresh();
        planes->setSympatheticStringsEnabled(false);
        planes->setBridgeCouplingEnabled(false);
        planes->noteOn(43, 1.0f);
        // Advance to the direct arrival, half the pluck point's share of a
        // round trip (about 67 samples at the default Finger distance), so
        // the next transport sample is the first arrival itself.
        const auto& plucked = Access::voice(*planes);
        Access::advance(*planes, static_cast<int>(std::ceil(
            0.5f * plucked.pluckPoint * plucked.contactPeriodSamples)));
        Access::queueOnly(*planes);
        Access::fixedDamping(*planes, 0.25f, 0.75f);
        auto expected = Access::voice(*planes).contactTravel;
        const auto paths = expected.process(0.0f);
        const float local = 0.7071067811865475f * (paths[0] - paths[1]);
        expect(std::abs(local) > 1e-9f, "independent-polarization probe has no arrival");
        std::array<float, 1> left {}, right {};
        planes->process(left.data(), right.data(), 1);
        expect(std::abs(Access::lastWritten(Access::voice(*planes).loops[0])
                        - 0.76f * (local * 0.25f)) < 1e-10f,
               "vertical arrival used another polarization's damping");
        expect(std::abs(Access::lastWritten(Access::voice(*planes).loops[1])
                        - 0.51f * (local * 0.75f)) < 1e-10f,
               "horizontal arrival used another polarization's damping");
    }

    auto engine = fresh();
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 48);
    for (int target : { 43, 50 })
    {
        auto expected = Access::voice(*engine).contactTravel;
        expect(Access::pending(expected) > 1e-10,
               "retrigger probe has no pending contact to retain");
        engine->noteOn(target, 1.0f);
        auto& voice = Access::voice(*engine);
        expect(voice.tailActive && voice.tailContactTravel.active,
               "retrigger lost the preceding contact transport");
        expect(!voice.contactTravel.active,
               "new contact inherited stale pending samples before its first source sample");
        auto captured = voice.tailContactTravel;
        for (int n = 0; n < 2048; ++n)
            expect(captured.process(0.0f) == expected.process(0.0f),
                   "retained contact differs from the preceding zero-input transport");
        Access::advance(*engine, 48);
    }

    // A delayed repick must preserve the old transport until the actual
    // event, then retain exactly its state from that sample in the tail.
    engine = fresh();
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 48);
    engine->noteOn(43, 1.0f, 1, 100);
    Access::advance(*engine, 100);
    auto expected = Access::voice(*engine).contactTravel;
    expected.process(0.0f);
    Access::advance(*engine, 1);
    auto captured = Access::voice(*engine).tailContactTravel;
    for (int n = 0; n < 1024; ++n)
        expect(captured.process(0.0f) == expected.process(0.0f),
               "scheduled repick captured the transport at the wrong sample");

    // Independently isolate the retained branch with an exactly delayed
    // packet. Its first arrival must see its own captured hand damping.
    engine = fresh();
    engine->setSympatheticStringsEnabled(false);
    engine->setBridgeCouplingEnabled(false);
    engine->noteOn(43, 1.0f);
    auto& voice = Access::voice(*engine);
    voice.contactTravelEnabled = false;
    Access::queueOnly(*engine, true);
    voice.tailActive = true;
    voice.tailCharacteristicImpedance = voice.characteristicImpedance;
    voice.tailDamping = 0.0f;
    voice.tailLoop.appliedReleaseGain = voice.tailLoop.requestedReleaseGain = 0.0f;
    voice.tailContactTravel.reset(3.0f, 9.0f);
    voice.tailContactTravel.process(1.0f);
    std::array<float, 1> left {}, right {};
    for (int n = 0; n < 32; ++n)
    {
        engine->process(left.data(), right.data(), 1);
        expect(Access::lastWritten(voice.tailLoop) == 0.0f,
               "retained contact bypassed tail hand damping");
    }
    std::cout << "Queued first arrival open/half/stopped energy="
              << open << '/' << half << '/' << stopped << '\n';
}

void testDrainPanicAndExtremeOwnership()
{
    auto engine = fresh(8000);
    engine->noteOn(43, 1.0f);
    Access::queueOnly(*engine, true);
    auto& voice = Access::voice(*engine);
    voice.contactTravelEnabled = false;
    voice.tailActive = true;
    voice.tailCharacteristicImpedance = voice.characteristicImpedance;
    voice.tailContactTravel.reset(8180.0f, 8189.0f);
    voice.tailContactTravel.process(1.0f);
    voice.tailLevel = 0.0f;
    voice.tailQuietSamples = 1000;
    Access::advance(*engine, 128);
    expect(voice.tailActive && voice.tailContactTravel.active,
           "quiet-tail retirement discarded an in-flight long-delay packet");

    engine = fresh(8000);
    engine->setPitchBend(-96.0f);
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 128);
    Access::voice(*engine).level = 0.0f;
    expect(Access::voice(*engine).contactTravel.active,
           "quiet capture probe has no in-flight packet");
    engine->noteOn(43, 1.0f);
    expect(Access::voice(*engine).tailActive
           && Access::voice(*engine).tailContactTravel.active,
           "quiet level threshold discarded pending contact during capture");

    engine = fresh();
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 48000);
    expect(Access::voice(*engine).contactTravelEnabled
           && !Access::voice(*engine).contactTravel.active,
           "held contact failed to retire or changed its routing identity");
    auto control = fresh();
    control->noteOn(43, 1.0f);
    Access::advance(*control, 48000);
    Access::disableDrained(*control);
    std::array<float, 128> left {}, right {}, controlLeft {}, controlRight {};
    engine->process(left.data(), right.data(), 128);
    control->process(controlLeft.data(), controlRight.data(), 128);
    expect(left == controlLeft && right == controlRight,
           "drained contact changed output or duplicated the former boundary source");

    // Frozen path geometry is an explicit approximation during a fast bend.
    // Confirm legal extremes remain finite and MPE ownership reset cannot
    // replay queued samples into the next owner.
    engine = fresh();
    engine->setStringPerChannelMode(false);
    engine->setLowerZoneMemberCount(2);
    engine->noteOn(43, 1.0f, 2);
    Access::advance(*engine, 48);
    engine->setPitchBend(96.0f, 2);
    engine->setMpeTimbre(1.0f, 2);
    engine->setMpePressure(1.0f, 2);
    for (int block = 0; block < 20; ++block)
    {
        engine->process(left.data(), right.data(), 128);
        for (int n = 0; n < 128; ++n)
            expect(std::isfinite(left[n]) && std::isfinite(right[n]),
                   "extreme MPE contact trajectory is nonfinite");
    }
    engine->setLowerZoneMemberCount(0);
    engine->process(left.data(), right.data(), 128);
    expect(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0f; })
           && std::all_of(right.begin(), right.end(), [](float x) { return x == 0.0f; }),
           "MPE zone reset replayed an old transport");
    engine->noteOn(43, 1.0f);
    Access::advance(*engine, 48);
    engine->allSoundOff();
    engine->process(left.data(), right.data(), 128);
    expect(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0f; })
           && std::all_of(right.begin(), right.end(), [](float x) { return x == 0.0f; }),
           "panic left audible contact or body state");
}
}

int main()
{
    testAllpassWaveEnergyAndArrival();
    testOrdinaryAndPedalRelease();
    testArrivalDampingAndTailRetention();
    testDrainPanicAndExtremeOwnership();
    std::cout << "Contact transport failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
