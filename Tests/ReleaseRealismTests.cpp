// Explicit release gestures change hand loss without refretting, re-plucking,
// or altering the nominal/missing-velocity performance.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static float releaseSeconds(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].releaseSeconds; }
    static float appliedGain(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].loops[0].appliedReleaseGain; }
    static double lineEnergy(const AcustraEngine& e, int string)
    {
        double result = 0.0;
        for (const auto& loop : e.voices_[static_cast<std::size_t>(string)].loops)
            for (float x : loop.delay) result += static_cast<double>(x) * x;
        return result;
    }
    static int note(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].midiNote; }
    static bool contact(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].loops[0].gestureContact.active; }
    static bool tailContact(const AcustraEngine& e, int string)
    {
        const auto& v = e.voices_[static_cast<std::size_t>(string)];
        return v.tailActive && v.tailLoop.gestureContact.active;
    }
    static float contactMemory(const AcustraEngine& e, bool tail = false)
    { return tail ? e.voices_[0].tailLoop.gestureContact.memory
                  : e.voices_[0].loops[0].gestureContact.memory; }
    static int returnSamples(const AcustraEngine& e)
    { return e.voices_[0].returnSamples; }
    static float withdrawalScale(const AcustraEngine& e)
    { return e.voices_[0].loops[0].gestureContact.withdrawalScale; }
    static int withdrawalSamples(const AcustraEngine& e)
    { return e.voices_[0].loops[0].gestureContact.withdrawalSamples; }
    static std::array<float, 5> contactState(const AcustraEngine& e, bool tail = false)
    {
        const auto& c = tail ? e.voices_[0].tailLoop.gestureContact
                             : e.voices_[0].loops[0].gestureContact;
        return { c.coupling, c.memoryGain, c.offsetScale, c.memory, c.withdrawalScale };
    }
    static std::array<double, 3> contactGeometry(const AcustraEngine& e,
                                                int string, int plane)
    {
        const auto& v = e.voices_[static_cast<std::size_t>(string)];
        const auto& contact = v.loops[static_cast<std::size_t>(plane)].gestureContact;
        return { v.speakingLengthMetres, static_cast<double>(contact.firstAge),
                 static_cast<double>(contact.firstAge + contact.secondAge) };
    }
    static void retainQuietContact(AcustraEngine& e)
    {
        auto& v = e.voices_[0];
        v.level = 0.0f;
        v.contactTravel.active = v.legatoContactTravel.active = false;
        v.legatoContactSamples = 0;
        v.repluckArrivals.clear();
    }
    static bool passiveVelocityContact()
    {
        AcustraEngine::StringLoop::GestureContact contact;
        contact.configure(0.22f, 288);
        std::array<float, 2> previousIn {}, previousOut {};
        for (int i = 0; i < 4096; ++i)
        {
            float a = 0.001f * static_cast<float>(std::sin(0.053 * i)
                                                   + 0.31 * std::cos(0.213 * i));
            float b = 0.001f * static_cast<float>(std::sin(0.031 * i + 0.7)
                                                   - 0.17 * std::cos(0.187 * i));
            const std::array<float, 2> incoming { a, b };
            const double oldMemory = contact.memory;
            contact.scatter(a, b);
            if (i == 0 && (a != incoming[0] || b != incoming[1]))
                return false; // No displacement step at contact establishment.
            if (i > 0)
            {
                // Temporal differences of each directional displacement
                // wave are its velocity; Z*Fs^2 cancels in this comparison.
                // Raw displacement-cell L2 would not establish passivity.
                const double ia = incoming[0] - previousIn[0];
                const double ib = incoming[1] - previousIn[1];
                const double oa = a - previousOut[0];
                const double ob = b - previousOut[1];
                const double inputPower = ia * ia + ib * ib + 0.5 * oldMemory * oldMemory;
                const double outputPower = oa * oa + ob * ob
                    + 0.5 * contact.memory * contact.memory;
                if (outputPower > inputPower * (1.0 + 2.0e-5) + 1.0e-18)
                    return false;
            }
            previousIn = incoming;
            previousOut = { a, b };
        }
        // With a stationary string the stored deformation must relax;
        // otherwise lifting the hand launches a spurious late pluck.
        float outA = 0.0f, outB = 0.0f;
        for (int i = 0; i < 8192; ++i)
        {
            outA = previousIn[0]; outB = previousIn[1];
            contact.scatter(outA, outB);
        }
        return std::abs(outA - previousIn[0]) < 1.0e-9f
            && std::abs(outB - previousIn[1]) < 1.0e-9f;
    }
    static double modalVelocityEnergy(int harmonic, bool damping, double phase)
    {
        // An ideal, lossless, rigidly reflected folded string isolates the
        // contact from the engine's bridge/intrinsic-loss filters. Center
        // contact is an antinode of odd partials and a node of even ones.
        auto loop = std::make_unique<AcustraEngine::StringLoop>();
        constexpr int period = 256;
        loop->currentDelay = loop->targetDelay = static_cast<float>(period);
        for (int age = 0; age < period; ++age)
            loop->delay[static_cast<std::size_t>((-1 - age) & (AcustraEngine::maximumDelaySamples - 1))]
                = 0.001f * static_cast<float>(std::cos(
                    6.283185307179586 * harmonic * age / period + phase));
        if (damping) loop->beginGestureContact(0.5f, 0.10f, period);
        double energy = 0.0;
        float previous = loop->delay[static_cast<std::size_t>((-period) & (AcustraEngine::maximumDelaySamples - 1))];
        for (int i = 0; i < 32 * period; ++i)
        {
            const float incident = loop->delay[static_cast<std::size_t>(
                (loop->writeIndex - period) & (AcustraEngine::maximumDelaySamples - 1))];
            loop->write(incident);
            if (damping) loop->applyGestureContact();
            const double velocity = incident - previous;
            previous = incident;
            if (i >= 28 * period) energy += velocity * velocity;
        }
        return energy;
    }
    static bool withdrawalWorkIsBounded()
    {
        // Observe actual outgoing displacement increments. Subtract only
        // the independently derived work of moving K; the remaining waves
        // and contact storage must obey the fixed-contact energy bound.
        for (int rate : { 8000, 44100, 48000, 96000, 192000, 384000 })
            for (float strength : { 0.005f, 0.10f, 0.22f })
                for (bool stillInput : { false, true })
                {
                    AcustraEngine::StringLoop::GestureContact contact;
                    const int count = static_cast<int>(std::ceil(0.006 * rate));
                    contact.configure(strength, count);
                    std::array<float, 2> previousIn {}, previousOut {};
                    double previousK = contact.offsetScale;
                    const double maximumKStep = 1.5 * contact.offsetScale / count;
                    for (int i = 0; i <= 3 * count; ++i)
                    {
                        const double t = static_cast<double>(i) / rate;
                        float a = 0.001f * static_cast<float>(std::sin(1130.0 * t)
                            + 0.31 * std::cos(7170.0 * t));
                        float b = 0.001f * static_cast<float>(std::sin(790.0 * t + 0.7)
                            - 0.17 * std::cos(6310.0 * t));
                        if (i >= 2 * count && stillInput)
                        { a = previousIn[0]; b = previousIn[1]; }
                        const std::array<float, 2> incoming { a, b };
                        const double oldMemory = contact.memory;
                        if (i >= 2 * count)
                            contact.withdraw(3 * count - i + 1);
                        const double k = static_cast<float>(
                            contact.offsetScale * contact.withdrawalScale);
                        const double deltaK = k - previousK;
                        contact.scatter(a, b);
                        if (i > 0)
                        {
                            const double ia = static_cast<double>(incoming[0]) - previousIn[0];
                            const double ib = static_cast<double>(incoming[1]) - previousIn[1];
                            const double oa = static_cast<double>(a) - previousOut[0];
                            const double ob = static_cast<double>(b) - previousOut[1];
                            const double hand = -deltaK * oldMemory;
                            const double passiveA = oa - hand;
                            const double passiveB = ob + hand;
                            const double input = ia * ia + ib * ib + 0.5 * oldMemory * oldMemory;
                            const double storage = 0.5 * contact.memory * contact.memory;
                            const double passive = passiveA * passiveA + passiveB * passiveB + storage;
                            const double actual = oa * oa + ob * ob + storage;
                            const double workBound = 2.0 * std::abs(hand)
                                * (std::abs(passiveA - passiveB) + std::abs(hand));
                            const double tolerance = input * 1.0e-4 + 1.0e-18;
                            if (passive > input + tolerance
                                || actual > input + workBound + tolerance
                                || std::abs(deltaK) > maximumKStep * 1.001 + 1.0e-7)
                                return false;
                        }
                        if (i == 3 * count
                            && (contact.withdrawalScale != 0.0f
                                || a != incoming[0] || b != incoming[1]))
                            return false; // Detaching leaves no displacement offset.
                        previousIn = incoming;
                        previousOut = { a, b };
                        previousK = k;
                    }
                }
        return true;
    }
};
}

namespace
{
int failures = 0;
void expect(bool ok, const std::string& message)
{ if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

int graceSamples(double rate)
{ return static_cast<int>(std::ceil(rate * 7.5 / 120.0)); }

void advance(acustra::AcustraEngine& engine, int samples)
{
    std::array<float, 127> left {}, right {};
    while (samples > 0)
    {
        const int count = std::min(samples, 127);
        engine.process(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
            expect(std::isfinite(left[i]) && std::isfinite(right[i]),
                   "release grace became non-finite");
        samples -= count;
    }
}

std::vector<float> render(double rate, int note, float velocity)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, 127);
    engine->setStringPerChannelMode(true);
    engine->setSympatheticStringsEnabled(false);
    engine->noteOn(note, 0.8f, 1);
    const int before = static_cast<int>(0.35 * rate);
    const int after = static_cast<int>(0.8 * rate);
    std::vector<float> left(static_cast<std::size_t>(before + after));
    std::vector<float> right(left.size());
    engine->process(left.data(), right.data(), before);
    if (velocity == -2.0f) engine->noteOff(note);
    else engine->noteOffWithVelocity(note, 1, velocity);
    engine->process(left.data() + before, right.data() + before, after);
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void nominalAndMonotonic()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 40, 43, 55 })
        {
            const auto nominal = render(rate, note, -2.0f);
            expect(nominal == render(rate, note, -1.0f), "missing velocity altered nominal output");
            expect(nominal == render(rate, note, 64.0f / 127.0f), "MIDI 64 altered nominal output");
            expect(nominal == render(rate, note, std::numeric_limits<float>::quiet_NaN()),
                   "non-finite velocity did not use nominal output");
            float previousTime = std::numeric_limits<float>::infinity();
            double previousEnergy = std::numeric_limits<double>::infinity();
            for (float velocity : { 0.0f, 0.25f, 64.0f / 127.0f, 0.75f, 1.0f })
            {
                auto e = std::make_unique<acustra::AcustraEngine>();
                e->prepare(rate, 127);
                e->setStringPerChannelMode(true);
                e->setSympatheticStringsEnabled(false);
                e->noteOn(note, 0.8f);
                std::array<float, 127> left {}, right {};
                for (int i = 0; i < 100; ++i) e->process(left.data(), right.data(), 127);
                const double before = Access::lineEnergy(*e, 0);
                const float oldGain = Access::appliedGain(*e, 0);
                e->noteOffWithVelocity(note, 1, velocity);
                expect(Access::lineEnergy(*e, 0) == before, "key-up wrote a new wave into the delay lines");
                expect(Access::appliedGain(*e, 0) == oldGain, "key-up stepped applied hand gain");
                expect(Access::note(*e, 0) == note, "key-up refretted the vibrating string");
                expect(e->heldString(note) < 0, "key-up retained held ownership");
                advance(*e, graceSamples(rate) + 1);
                expect(Access::releaseSeconds(*e, 0) == 0.0f
                           && Access::appliedGain(*e, 0) == oldGain,
                       "release velocity damped the string inside its ordinary join window");
                const double beforeDamping = Access::lineEnergy(*e, 0);
                advance(*e, 1);
                const float time = Access::releaseSeconds(*e, 0);
                expect(time < previousTime, "higher release velocity did not shorten hand T60");
                previousTime = time;
                // Keep the original 25 ms physical damping window, now
                // measured from actual hand contact after the deadline.
                const int trips = static_cast<int>(0.025 * rate / 127.0);
                for (int i = 0; i < trips; ++i)
                {
                    e->process(left.data(), right.data(), 127);
                    for (float x : left) expect(std::isfinite(x), "release became non-finite");
                    const float gain = Access::appliedGain(*e, 0);
                    expect(gain > 0.0f && gain <= 1.0f, "hand contact became an active gain or gate");
                }
                const double after = Access::lineEnergy(*e, 0);
                expect(after < before, "released string did not lose wave energy");
                expect(after < beforeDamping, "hand contact did not remove wave energy after its deadline");
                expect(after < previousEnergy, "firmer damping did not remove more wave energy");
                previousEnergy = after;
            }
        }
}

void ownershipAndPedal()
{
    using Access = acustra::AcustraEngineTestAccess;
    auto e = std::make_unique<acustra::AcustraEngine>();
    e->prepare(48000.0, 64);
    e->setStringPerChannelMode(true);
    e->noteOn(43, 0.7f); e->noteOn(43, 0.7f);
    e->noteOffWithVelocity(43, 1, 1.0f);
    expect(e->heldString(43) == 0, "first duplicate off released the second owner");
    e->noteOffWithVelocity(43, 1, 0.0f);
    advance(*e, graceSamples(48000.0) + 2);
    expect(Access::releaseSeconds(*e, 0) > 0.16f, "final owner's gentle release was lost");
    e->reset();
    e->setSustainPedal(true);
    e->noteOn(43, 0.7f);
    e->noteOffWithVelocity(43, 1, 1.0f);
    e->setSustainPedal(false);
    expect(Access::releaseSeconds(*e, 0) == 0.16f,
           "pedal-up reused a stale fast key release instead of nominal contact");
    e->allSoundOff();
    expect(e->getActiveVoiceCount() == 0, "all sound off retained a release voice");
    e->noteOn(43, 0.7f);
    e->noteOff(43);
    advance(*e, graceSamples(48000.0) + 2);
    expect(Access::releaseSeconds(*e, 0) == 0.16f, "panic left a stale release velocity");
}

void spatialContactPhysics()
{
    using Access = acustra::AcustraEngineTestAccess;
    expect(Access::passiveVelocityContact(),
           "local contact created directional wave-velocity energy or a displacement step");
    for (double phase : { 0.0, 0.8, 1.7 })
        for (int harmonic : { 1, 2, 3, 4 })
        {
            const double free = Access::modalVelocityEnergy(harmonic, false, phase);
            const double caught = Access::modalVelocityEnergy(harmonic, true, phase);
            if ((harmonic & 1) == 0)
                expect(std::abs(caught / free - 1.0) < 2.0e-5,
                       "contact damped a partial with a node at its position");
            else
                expect(caught / free < 0.005,
                       "contact did not selectively remove an antinode's velocity energy");
        }
}

auto configured(bool gesture, double rate)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    auto options = e->performanceRealism();
    options.gestureDamping = gesture;
    e->setPerformanceRealism(options);
    e->prepare(rate, 127);
    e->setStringPerChannelMode(true);
    e->setSympatheticStringsEnabled(false);
    return e;
}

void releasePadFollowsPhysicalSlideLength()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int gesture : { 0, 1, 2 })
            for (const int fret : { 8, 20 })
            {
                // The normal +/-12-semitone wheel reaches virtual fret -4
                // or 32 here. The decay calibration clamps to frets 0..20;
                // that clamp must not move the physical 18 mm damping pad.
                // Conventional/manager wheels slide, a member wheel changes
                // tension at a fixed length and must keep that pad geometry.
                auto engine = configured(true, rate);
                if (gesture != 0)
                    engine->setLowerZoneMemberCount(15);
                const float bend = fret == 8 ? -12.0f : 12.0f;
                engine->setPitchBend(bend, gesture == 1 ? 1 : 2);
                engine->noteOn(45 + fret, 0.75f, 2);
                advance(*engine, static_cast<int>(0.1 * rate));
                engine->noteOff(45 + fret, 2);
                advance(*engine, graceSamples(rate) + 2);
                expect(Access::contact(*engine, 1),
                       "slide geometry fixture did not reach its release contact");
                const double expectedLength = 0.648 * std::exp2(
                    -(fret + (gesture == 2 ? 0.0 : bend)) / 12.0);
                for (int plane = 0; plane < 2; ++plane)
                {
                    const auto geometry = Access::contactGeometry(*engine, 1, plane);
                    expect(std::abs(geometry[0] / expectedLength - 1.0) < 1.0e-6,
                           "the release fixture did not retain its physical slide length");
                    const double relativePosition = 2.0 * geometry[1] / geometry[2];
                    const double padInset = expectedLength * (1.0 - relativePosition);
                    // The contact rounds its half-period tap to the nearest
                    // cell. One full-period cell in metres is the resulting
                    // maximum inset error; use it as the geometry tolerance.
                    const double gridError = expectedLength / geometry[2];
                    expect(std::abs(padInset - 0.018) <= gridError + 1.0e-7,
                           "a supported slide moved the 18 mm release pad with the decay clamp");
                }
            }
}

void contactBoundariesAndContinuity()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto on = configured(true, rate), off = configured(false, rate);
        std::array<float, 127> onL {}, onR {}, offL {}, offR {};
        const auto paired = [&] (int count, bool identical)
        {
            while (count > 0)
            {
                const int n = std::min(count, 127);
                on->process(onL.data(), onR.data(), n);
                off->process(offL.data(), offR.data(), n);
                if (identical)
                    expect(std::equal(onL.begin(), onL.begin() + n, offL.begin())
                        && std::equal(onR.begin(), onR.begin() + n, offR.begin()),
                        "gesture damping changed held, sustained or join-window audio");
                count -= n;
            }
        };
        on->noteOn(43, 0.75f); off->noteOn(43, 0.75f);
        paired(static_cast<int>(rate * 0.6), true);
        expect(!Access::contact(*on, 0), "held long note acquired autonomous damping");
        on->noteOff(43); off->noteOff(43);
        paired(graceSamples(rate) + 1, true);
        expect(!Access::contact(*on, 0), "contact began inside the inclusive join window");
        // A stroke at the inclusive endpoint cancels contact without ever
        // affecting the old wave, force draw, or attack timing.
        on->noteOn(43, 0.9f); off->noteOn(43, 0.9f);
        paired(511, true);
        on->setSustainPedal(true); off->setSustainPedal(true);
        on->noteOffWithVelocity(43, 1, 1.0f);
        off->noteOffWithVelocity(43, 1, 1.0f);
        paired(static_cast<int>(rate * 0.4), true);
        expect(!Access::contact(*on, 0), "pedal-held string acquired a key-up contact");
        on->setSustainPedal(false); off->setSustainPedal(false);
        expect(Access::contact(*on, 0) && !Access::contact(*off, 0),
               "pedal-up did not establish the optional actual damping contact");
        paired(static_cast<int>(rate * 0.035), false);
        const float oldMemory = Access::contactMemory(*on);
        on->noteOn(45, 0.7f); off->noteOn(45, 0.7f);
        expect(!Access::contact(*on, 0) && Access::tailContact(*on, 0),
               "fresh note inherited old damping or retained tail lost its contact");
        expect(Access::contactMemory(*on, true) == oldMemory,
               "capturing the retained tail reset or altered contact storage");
        paired(1024, false);
        on->allSoundOff();
        expect(!Access::contact(*on, 0) && !Access::tailContact(*on, 0),
               "panic retained a local damping contact");
        on->noteOn(43, 0.75f);
        on->noteOff(43);
        advance(*on, graceSamples(rate) + 2);
        expect(Access::contact(*on, 0), "ordinary key-up failed to land after its deadline");
        advance(*on, static_cast<int>(rate * 0.25));
        expect(!Access::contact(*on, 0), "finished release kept its contact on the idle string");
        // Prior ownership/reset checks deliberately played only the enabled
        // engine. Fresh fixtures restore matched random-draw histories.
        on = configured(true, rate); off = configured(false, rate);
        on->noteOn(55, 0.75f); off->noteOn(55, 0.75f);
        paired(static_cast<int>(rate * 0.3), true);
        on->noteOff(55); off->noteOff(55);
        paired(graceSamples(rate) + static_cast<int>(rate * 0.015), false);
        expect(Access::contact(*on, 0), "late repeat fixture missed the release contact");
        on->noteOn(55, 0.9f); off->noteOn(55, 0.9f);
        expect(!Access::contact(*on, 0), "continuing same-pitch stroke retained the damping hand");
        double onPeak = 0.0, offPeak = 0.0;
        for (int i = 0; i < static_cast<int>(rate * 0.01); ++i)
        {
            on->process(onL.data(), onR.data(), 1);
            off->process(offL.data(), offR.data(), 1);
            onPeak = std::max(onPeak, static_cast<double>(std::abs(onL[0])));
            offPeak = std::max(offPeak, static_cast<double>(std::abs(offL[0])));
        }
        expect(onPeak < 1.10 * offPeak,
               "lifting an active contact made a late same-pitch reattack overshoot its headroom");
        on->noteOff(55);
        advance(*on, graceSamples(rate) + static_cast<int>(rate * 0.01));
        const float quietMemory = Access::contactMemory(*on);
        expect(std::abs(quietMemory) > 1.0e-12f, "quiet tail fixture has no contact storage");
        Access::retainQuietContact(*on);
        on->noteOn(57, 0.7f);
        expect(Access::tailContact(*on, 0)
            && Access::contactMemory(*on, true) == quietMemory,
               "a floor-level bridge observer discarded stored physical contact energy");
    }
}
}

void contactWithdrawalDeadline()
{
    using Access = acustra::AcustraEngineTestAccess;
    expect(Access::withdrawalWorkIsBounded(),
           "moving hand exceeded its derived velocity/work bound or left a detach offset");
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (float velocity : { 0.0f, 64.0f / 127.0f, 1.0f })
        {
            auto engine = std::make_unique<acustra::AcustraEngine>();
            engine->prepare(rate, 127);
            engine->setStringPerChannelMode(true);
            engine->noteOn(59, 0.9f, 5); // Keep driving the shared bridge.
            engine->noteOn(47, 0.8f, 1);
            advance(*engine, static_cast<int>(0.3 * rate));
            engine->noteOffWithVelocity(47, 1, velocity);
            advance(*engine, graceSamples(rate) + 2);
            const int totalRemaining = Access::returnSamples(*engine);
            const int liftSamples = Access::withdrawalSamples(*engine);
            expect(totalRemaining > liftSamples + 2,
                   "withdrawal fixture missed the existing release countdown");
            advance(*engine, totalRemaining - liftSamples - 1);
            expect(Access::withdrawalScale(*engine) == 1.0f,
                   "hand withdrew before its final relaxation interval");
            float previous = 1.0f;
            for (int remaining = liftSamples + 1; remaining > 1; --remaining)
            {
                expect(Access::returnSamples(*engine) == remaining,
                       "hand withdrawal changed the return-to-open deadline");
                advance(*engine, 1);
                const float scale = Access::withdrawalScale(*engine);
                expect(scale >= 0.0f && scale <= previous,
                       "hand withdrawal reversed or exceeded its physical endpoints");
                previous = scale;
            }
            expect(previous <= 3.01f / (liftSamples * liftSamples)
                       && Access::contact(*engine, 0),
                   "hand was not almost detached on the penultimate release sample");
            advance(*engine, 1);
            expect(Access::returnSamples(*engine) == 0 && !Access::contact(*engine, 0)
                       && Access::note(*engine, 0) == 40,
                   "hand contact persisted beyond the unchanged return deadline");
        }
}

void contactWithdrawalLifecycle()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int action = 0; action < 6; ++action)
        {
            auto engine = std::make_unique<acustra::AcustraEngine>();
            engine->prepare(rate, 127);
            engine->setStringPerChannelMode(true);
            engine->noteOn(47, 0.8f, 1);
            advance(*engine, static_cast<int>(0.3 * rate));
            engine->noteOff(47);
            advance(*engine, graceSamples(rate) + 2);
            advance(*engine, Access::returnSamples(*engine)
                - Access::withdrawalSamples(*engine) / 2);
            const int remaining = Access::returnSamples(*engine);
            const auto contact = Access::contactState(*engine);
            expect(contact[4] > 0.0f && contact[4] < 1.0f,
                   "lifecycle fixture did not reach a partially withdrawn contact");
            if (action < 3)
            {
                acustra::EngineParameters parameters;
                if (action == 0) parameters.bodyMaterial = acustra::BodyMaterial::Maple;
                if (action == 1) parameters.shape = acustra::BodyShape::Parlor;
                if (action == 2) parameters.tuning = acustra::Tuning::DropD;
                engine->setParameters(parameters);
                expect(Access::returnSamples(*engine) == remaining
                    && Access::contactState(*engine) == contact,
                    "live construction change restarted the withdrawal or its deadline");
                advance(*engine, remaining - 1);
                expect(Access::returnSamples(*engine) == 1 && Access::contact(*engine, 0),
                       "live construction change ended the release early");
                advance(*engine, 1);
                expect(Access::returnSamples(*engine) == 0 && !Access::contact(*engine, 0),
                       "live construction change delayed the release hand-back");
            }
            else if (action == 3)
            {
                engine->noteOn(57, 0.7f, 1);
                expect(!Access::contact(*engine, 0) && Access::tailContact(*engine, 0)
                    && Access::contactState(*engine, true) == contact,
                    "retaining old waves changed the partially withdrawn contact state");
            }
            else
            {
                if (action == 4) engine->allSoundOff();
                else engine->prepare(rate, 127);
                expect(!Access::contact(*engine, 0) && !Access::tailContact(*engine, 0)
                    && Access::returnSamples(*engine) == 0,
                    "panic or prepare retained a withdrawal or release deadline");
            }
        }
}

int main()
{
    spatialContactPhysics(); contactBoundariesAndContinuity();
    contactWithdrawalDeadline();
    contactWithdrawalLifecycle();
    releasePadFollowsPhysicalSlideLength();
    nominalAndMonotonic(); ownershipAndPedal();
    if (failures) return 1;
    std::cout << "Release damping, passive spatial contact, node selectivity, join/pedal parity and ownership passed\n";
    return 0;
}
