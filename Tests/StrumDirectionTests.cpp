// A return stroke reverses only its newly released sideways field. Existing
// waves and packets in flight retain their own direction and energy. How an
// upstroke's force leans across the strings is StrokeDepthTests'; the
// reversal is checked here with that depth switched off.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    using Loop = AcustraEngine::StringLoop;
    static constexpr int delayCapacity = AcustraEngine::maximumDelaySamples;
    struct Source
    {
        std::array<Loop, 2> loops;
        float sign, excitation, step, mix;
        std::uint32_t pickingRandom, excitationRandom;
    };
    static Source source(const AcustraEngine& e, int string = 0)
    {
        const auto& v = e.voices_[static_cast<std::size_t>(string)];
        return { v.loops, v.pluckParallelSign, v.excitationParallelGain,
                 v.releaseStepRise, v.polarisationMix,
                 v.randomState, v.excitationNoiseState };
    }
    static int delay(const AcustraEngine& e, int string = 0)
    { return e.voices_[static_cast<std::size_t>(string)].pluckDelay; }
    static float sign(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckParallelSign; }
    static float gain(const AcustraEngine& e)
    { return e.voices_[0].excitationParallelGain; }
    static bool tail(const AcustraEngine& e)
    { return e.voices_[0].tailActive; }
    static std::array<Loop, 2> tailLoops(const AcustraEngine& e)
    { return { e.voices_[0].tailLoop, e.voices_[0].tailParallelLoop }; }
    static float tailGain(const AcustraEngine& e)
    { return e.voices_[0].tailExcitationParallelGain; }
    static std::vector<std::array<float, 2>> futureArrivals(const AcustraEngine& e)
    {
        const auto& v = e.voices_[0];
        auto contact = v.contactTravel;
        std::vector<std::array<float, 2>> result;
        while (contact.active)
        {
            std::array<float, 2> packet {};
            const auto paths = contact.process(0.0f);
            const float local = 0.7071067811865475f * (paths[0] - paths[1]);
            packet[0] += 0.76f * local;
            packet[1] += v.excitationParallelGain * local;
            result.push_back(packet);
            if (result.size() >= AcustraEngine::RepluckArrivals::capacity) break;
        }
        return result;
    }
    static std::vector<std::array<float, 2>> retainedArrivals(const AcustraEngine& e,
                                                            std::size_t count)
    {
        auto arrivals = e.voices_[0].repluckArrivals;
        std::vector<std::array<float, 2>> result;
        for (std::size_t i = 0; i < count; ++i) result.push_back(arrivals.process());
        return result;
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
auto fresh(double rate = 48000.0,
           acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
           bool strokeDepth = true)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.strokeDepth = strokeDepth;
    e->setPerformanceRealism(realism);
    acustra::EngineParameters p;
    p.picking = picking;
    e->setParameters(p);
    e->prepare(rate, 127);
    e->setStringPerChannelMode(true);
    return e;
}
std::vector<float> advance(acustra::AcustraEngine& e, int samples)
{
    std::vector<float> result(static_cast<std::size_t>(samples * 2));
    std::array<float, 127> left {}, right {};
    for (int at = 0; at < samples;)
    {
        const int count = std::min(127, samples - at);
        e.process(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
        { result[static_cast<std::size_t>(2 * (at + i))] = left[static_cast<std::size_t>(i)];
          result[static_cast<std::size_t>(2 * (at + i) + 1)] = right[static_cast<std::size_t>(i)]; }
        at += count;
    }
    return result;
}
double slopeEnergy(const Access::Loop& loop)
{
    const int length = std::clamp(static_cast<int>(std::round(loop.currentDelay)),
                                  8, Access::delayCapacity - 3);
    const auto sample = [&](int age)
    {
        return loop.delay[static_cast<std::size_t>((loop.writeIndex - age
            + Access::delayCapacity) % Access::delayCapacity)];
    };
    double sum = 0.0;
    float previous = sample(length);
    for (int age = length - 1; age >= 0; --age)
    {
        const float current = sample(age);
        const double difference = static_cast<double>(current) - previous;
        sum += difference * difference;
        previous = current;
    }
    return sum;
}
void testFreshReleaseAndDefaultPaths()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto picking : { acustra::PickingTechnique::Finger,
                                   acustra::PickingTechnique::Pick,
                                   acustra::PickingTechnique::Thumb })
        {
            auto down = fresh(rate, picking, false);
            auto up = fresh(rate, picking, false);
            down->beginStrum(0, 0, false); up->beginStrum(0, 0, true);
            down->noteOn(47, 0.7f, 1, 0, true); up->noteOn(47, 0.7f, 1, 0, true);
            const auto a = Access::source(*down), b = Access::source(*up);
            expect(a.loops[0].delay == b.loops[0].delay,
                   "return stroke changed the fresh normal displacement field");
            bool opposite = true;
            for (std::size_t i = 0; i < a.loops[1].delay.size(); ++i)
                opposite &= a.loops[1].delay[i] == -b.loops[1].delay[i];
            expect(opposite && slopeEnergy(a.loops[1]) > 0.0,
                   "return stroke did not reverse a nonzero fresh sideways field");
            expect(slopeEnergy(a.loops[0]) == slopeEnergy(b.loops[0])
                       && slopeEnergy(a.loops[1]) == slopeEnergy(b.loops[1]),
                   "direction reversal changed fresh cyclic slope energy");
            expect(a.step == b.step && a.mix == b.mix,
                   "direction reversal changed static normal force or polarisation share");
            expect(a.excitation == 0.51f && b.excitation == -0.51f
                       && a.pickingRandom == b.pickingRandom
                       && a.excitationRandom == b.excitationRandom,
                   "stroke direction changed noise draws or burst magnitude");
        }
    auto implicit = fresh(), explicitDown = fresh();
    implicit->beginStrum(); explicitDown->beginStrum(0, 0, false);
    implicit->noteOn(47, 0.7f, 1, 0, true); explicitDown->noteOn(47, 0.7f, 1, 0, true);
    expect(advance(*implicit, 4096) == advance(*explicitDown, 4096),
           "default beginStrum no longer preserves explicit downstroke audio");
    auto single = fresh(), afterUpstroke = fresh();
    afterUpstroke->beginStrum(0, 0, true);
    single->noteOn(47, 0.7f, 1); afterUpstroke->noteOn(47, 0.7f, 1);
    expect(Access::source(*afterUpstroke).sign == 1.0f
               && advance(*single, 4096) == advance(*afterUpstroke, 4096),
           "ordinary single note inherited a preceding strum's direction");
}
void testScheduledDirectionsAndRetainedSources()
{
    auto e = fresh();
    e->beginStrum(0, 0, true);
    e->noteOn(47, 0.7f, 1, 200, true);
    const int wait = Access::delay(*e);
    e->beginStrum(0, 0, false);
    advance(*e, wait + 1);
    expect(Access::source(*e).sign == -1.0f && Access::gain(*e) == -0.51f,
           "later gesture changed an earlier scheduled string's release direction");

    auto continuing = fresh(48000.0, acustra::PickingTechnique::Pick);
    continuing->beginStrum(0, 0, true);
    continuing->noteOn(47, 0.7f, 1, 0, true);
    advance(*continuing, 11);
    const auto expected = Access::futureArrivals(*continuing);
    bool nonzero = false;
    for (const auto& packet : expected) nonzero |= packet[1] != 0.0f;
    expect(nonzero, "old-contact fixture contains no sideways packets in flight");
    continuing->beginStrum(0, 0, false);
    continuing->noteOn(47, 0.7f, 1, 0, true);
    expect(Access::retainedArrivals(*continuing, expected.size()) == expected,
           "opposite continuing stroke changed old packets' future arrivals");
    expect(Access::gain(*continuing) == 0.51f && !Access::tail(*continuing),
           "continuing stroke failed to adopt its new direction in the existing string");

    auto scheduled = fresh();
    scheduled->beginStrum(0, 0, true); scheduled->noteOn(47, 0.7f, 1, 0, true);
    advance(*scheduled, 11);
    scheduled->beginStrum(0, 0, false); scheduled->noteOn(47, 0.7f, 1, 97, true);
    expect(Access::source(*scheduled).sign == 1.0f && Access::gain(*scheduled) == -0.51f,
           "pending opposite stroke changed the old source before its physical release");
    const int scheduledWait = Access::delay(*scheduled);
    advance(*scheduled, std::max(0, scheduledWait - 1));
    expect(Access::gain(*scheduled) == -0.51f,
           "old source reversed one sample before the delayed pick arrived");
    advance(*scheduled, 1);
    expect(Access::gain(*scheduled) == 0.51f,
           "scheduled opposite stroke did not adopt direction at release");

    auto refret = fresh(48000.0, acustra::PickingTechnique::Pick);
    refret->beginStrum(0, 0, true); refret->noteOn(47, 0.7f, 1, 0, true);
    advance(*refret, 11);
    const auto old = Access::source(*refret);
    refret->beginStrum(0, 0, false); refret->noteOn(48, 0.7f, 1, 0, true);
    const auto tails = Access::tailLoops(*refret);
    expect(Access::tail(*refret) && tails[0].delay == old.loops[0].delay
               && tails[1].delay == old.loops[1].delay,
           "opposite refret stroke reversed or discarded its retained old wave");
    expect(Access::tailGain(*refret) == old.excitation
              ,
           "refret tail inherited the fresh stroke's source direction");
}
void chord(acustra::Performer& p)
{
    std::array<float, 1> left {}, right {};
    p.beginBlock(left.data(), right.data(), 1);
    for (const int note : open) p.noteOn(0, 1, note, 95);
    p.endBlock();
}
void advance(acustra::Performer& p, int samples)
{
    std::array<float, 127> left {}, right {};
    while (samples > 0)
    { const int count = std::min(samples, 127); p.process(left.data(), right.data(), count); samples -= count; }
}
void testPerformerAlternationRestAndReset()
{
    auto p = std::make_unique<acustra::Performer>();
    p->prepare(48000.0, 127); p->setGatherChords(false);
    const auto check = [&](float wanted, const char* message)
    {
        for (int string = 0; string < 6; ++string)
            expect(Access::sign(p->engine(), string) == wanted, message);
    };
    // Strums a quarter second apart, inside the dotted eighth at 120 BPM
    // (0.375 s), are one hand's down and return.
    chord(*p); check(1.0f, "first strum was not a downstroke"); advance(*p, 12000);
    chord(*p); check(-1.0f, "the return inside the window was not an upstroke"); advance(*p, 12000);
    chord(*p); check(1.0f, "alternation did not continue inside the window"); advance(*p, 12000);
    chord(*p); check(-1.0f, "alternation did not continue inside the window");
    p->reset(); chord(*p); check(1.0f, "reset did not restart on a downstroke"); advance(*p, 12000);
    chord(*p); check(-1.0f, "alternation did not resume after reset"); advance(*p, 96001);
    chord(*p); check(1.0f, "a two-second rest did not restart on a downstroke");
    // Quarter-note chords at 120 BPM (0.5 s apart) are all downstrokes...
    advance(*p, 24000); chord(*p); check(1.0f, "a quarter note later was not a downstroke");
    advance(*p, 24000); chord(*p); check(1.0f, "a quarter note later was not a downstroke");
    // ...and the same half second is an eighth at 60 BPM, so they alternate.
    p->setTempoBpm(60.0);
    advance(*p, 24000); chord(*p); check(-1.0f, "an eighth at 60 BPM did not alternate");
    advance(*p, 24000); chord(*p); check(1.0f, "an eighth at 60 BPM did not alternate");
    // Just inside and just outside the dotted eighth (0.75 s at 60 BPM).
    advance(*p, 35900); chord(*p); check(-1.0f, "a strum just inside the dotted eighth did not alternate");
    advance(*p, 36100); chord(*p); check(1.0f, "a strum just outside the dotted eighth was not a downstroke");
    // At 20 BPM the dotted eighth would be 2.25 s; the two-second rest caps it.
    p->setTempoBpm(20.0);
    advance(*p, 91200); chord(*p); check(-1.0f, "a 1.9 s gap at 20 BPM did not alternate");
    advance(*p, 100800); chord(*p); check(1.0f, "the two-second cap did not restart on a downstroke");
    // An invalid tempo reads as 120 BPM, as it does for the release window.
    p->setTempoBpm(0.0);
    advance(*p, 24000); chord(*p); check(1.0f, "an invalid tempo did not fall back to 120 BPM");
}

// Keyswitches below the guitar's range latch a direction until the next
// keyswitch or reset, never sound, and apply to a chord gathered after them.
void keyswitch(acustra::Performer& p, int note, bool on)
{
    std::array<float, 127> left {}, right {};
    p.beginBlock(left.data(), right.data(), 1);
    if (on) p.noteOn(0, 1, note, 100); else p.noteOff(0, 1, note, 64);
    p.endBlock();
}
void testKeyswitches()
{
    using Mode = acustra::Performer::StrokeMode;
    auto p = std::make_unique<acustra::Performer>();
    p->prepare(48000.0, 127); p->setGatherChords(false);
    const auto check = [&](float wanted, const char* message)
    {
        for (int string = 0; string < 6; ++string)
            expect(Access::sign(p->engine(), string) == wanted, message);
    };
    expect(p->strokeMode() == Mode::Automatic, "a fresh Performer is not automatic");
    // A keyswitch alone plays nothing and holds no key.
    keyswitch(*p, acustra::Performer::keyswitchUpstroke, true);
    advance(*p, 1000);
    expect(p->engine().getActiveVoiceCount() == 0, "a keyswitch sounded a string");
    expect(p->strokeMode() == Mode::Up, "C#0 did not latch upstrokes");
    keyswitch(*p, acustra::Performer::keyswitchUpstroke, false);
    expect(p->strokeMode() == Mode::Up, "a keyswitch's key-up changed the mode");
    // Every strum is an upstroke, whatever the interval: the first one too.
    chord(*p); check(-1.0f, "the first strum under C#0 was not an upstroke"); advance(*p, 12000);
    chord(*p); check(-1.0f, "a strum inside the window under C#0 was not an upstroke"); advance(*p, 96001);
    chord(*p); check(-1.0f, "a strum after a rest under C#0 was not an upstroke");
    keyswitch(*p, acustra::Performer::keyswitchDownstroke, true);
    advance(*p, 12000); chord(*p); check(1.0f, "a strum under C0 was not a downstroke");
    advance(*p, 12000); chord(*p); check(1.0f, "a strum under C0 was not a downstroke");
    // D0 returns to the automatic rule: the hand alternates from the last stroke.
    keyswitch(*p, acustra::Performer::keyswitchAutomatic, true);
    expect(p->strokeMode() == Mode::Automatic, "D0 did not return to automatic");
    advance(*p, 12000); chord(*p); check(-1.0f, "automatic did not alternate from the last downstroke");
    advance(*p, 96001); chord(*p); check(1.0f, "automatic did not restart after a rest");
    // Panic forgets the latch.
    keyswitch(*p, acustra::Performer::keyswitchUpstroke, true);
    p->reset();
    expect(p->strokeMode() == Mode::Automatic, "reset kept a latched keyswitch");
    chord(*p); check(1.0f, "the first strum after reset was not a downstroke");
    // Under Gather Chords a keyswitch sent inside the window before a chord
    // applies to it and is not gathered into it; one sent after the chord's
    // first key applies only after the chord.
    p->setGatherChords(true);
    advance(*p, 96001);
    const int window = acustra::Performer::gatherWindowSamples(48000.0);
    {
        std::array<float, 127> left {}, right {};
        p->beginBlock(left.data(), right.data(), 64);
        p->noteOn(0, 1, acustra::Performer::keyswitchUpstroke, 100);
        for (const int note : open) p->noteOn(10, 1, note, 100);
        p->noteOff(20, 1, acustra::Performer::keyswitchUpstroke, 64);
        p->endBlock();
    }
    advance(*p, window + 64);
    check(-1.0f, "a keyswitch inside the window before a gathered chord did not apply to it");
    expect(p->engine().getActiveVoiceCount() == 6, "a gathered chord lost strings to a keyswitch");
    for (const int note : open) keyswitch(*p, note, false);
    advance(*p, 96001);
    {
        std::array<float, 127> left {}, right {};
        p->beginBlock(left.data(), right.data(), 64);
        for (const int note : open) p->noteOn(0, 1, note, 100);
        p->noteOn(10, 1, acustra::Performer::keyswitchDownstroke, 100);
        p->endBlock();
    }
    advance(*p, window + 64);
    check(-1.0f, "a keyswitch after a gathered chord's first key changed that chord");
    expect(p->strokeMode() == Mode::Down, "a keyswitch after a gathered chord was lost");
}
std::vector<float> phrase(double rate, int block)
{
    auto p = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = acustra::PickingTechnique::Pick;
    parameters.room = 0.5f;
    p->setParameters(parameters); p->prepare(rate, block); p->setGatherChords(false);
    const int interval = static_cast<int>(std::lround(0.09 * rate));
    const int samples = 8 * interval + static_cast<int>(0.08 * rate);
    std::vector<float> left(static_cast<std::size_t>(samples)), right(left.size());
    for (int at = 0; at < samples; at += block)
    {
        const int count = std::min(block, samples - at);
        p->beginBlock(left.data() + at, right.data() + at, count);
        for (int stroke = 0; stroke < 8; ++stroke)
        {
            const int on = stroke * interval;
            const int off = on + interval - static_cast<int>(0.008 * rate);
            for (const int note : open)
            {
                if (on >= at && on < at + count) p->noteOn(on - at, 1, note, stroke % 3 == 0 ? 104 : 55);
                if (off >= at && off < at + count) p->noteOff(off - at, 1, note, 64);
            }
        }
        p->endBlock();
    }
    left.insert(left.end(), right.begin(), right.end());
    const bool finite = std::all_of(left.begin(), left.end(), [](float x)
        { return std::isfinite(x) && std::abs(x) <= 1.0f; });
    expect(finite, "repeated alternating strums produced nonfinite or unbounded audio");
    expect(std::any_of(left.begin(), left.end(), [](float x) { return x != 0.0f; }),
           "repeated alternating strums rendered silence");
    return left;
}
}
// The strum keys strum the keys held on their channel, F0 down and F#0 up,
// whatever the technique and however apart the keys arrived. In Keys mode
// the keys alone sound nothing; a re-strum keeps one owner per key.
void testStrumKeys()
{
    using P = acustra::Performer;
    auto p = std::make_unique<P>();
    p->prepare(48000.0, 127); p->setGatherChords(false);
    // Finger plucks a four-string chord together; the strum key sweeps it.
    constexpr std::array<int, 4> top { 50, 55, 59, 64 };
    const auto key = [&](int note, bool on) { keyswitch(*p, note, on); };
    // The pick enters at the stroke's first string; GuitarSet's scatter
    // (strumScatterSamples) may reorder the strings between.
    const auto swept = [&](bool upstroke, const char* message)
    {
        for (int string = 2; string < 6; ++string)
            expect(Access::sign(p->engine(), string) == (upstroke ? -1.0f : 1.0f), message);
        const int first = Access::delay(p->engine(), upstroke ? 5 : 2);
        const int last = Access::delay(p->engine(), upstroke ? 2 : 5);
        expect(first == 0 && last > 0, message);
    };
    key(P::keyswitchStrumDown, true);
    expect(p->strokeMode() == P::StrokeMode::Keys, "F0 did not latch Keys mode");
    expect(p->engine().getActiveVoiceCount() == 0, "a strum key with no keys held sounded");
    for (const int note : top) { key(note, true); advance(*p, 200); }
    advance(*p, 2000);
    expect(p->engine().getActiveVoiceCount() == 0, "a key sounded on its own in Keys mode");
    key(P::keyswitchStrumDown, true);
    swept(false, "F0 did not sweep the held keys low to high as a downstroke");
    advance(*p, 6000);
    std::array<int, 4> strings {};
    for (std::size_t i = 0; i < top.size(); ++i)
        strings[i] = p->engine().heldString(top[i], 1);
    key(P::keyswitchStrumUp, true);
    swept(true, "F#0 did not sweep the held keys high to low as an upstroke");
    for (std::size_t i = 0; i < top.size(); ++i)
        expect(p->engine().heldString(top[i], 1) == strings[i], "a re-strum moved a held key");
    advance(*p, 6000);
    // One key-up each releases every key: a re-strum took no extra owner.
    for (const int note : top) key(note, false);
    for (const int note : top)
        expect(p->engine().heldString(note, 1) < 0, "a re-strummed key stayed down after its key-up");
    // A strum key on its chord's own sample strums it once.
    {
        std::array<float, 127> left {}, right {};
        p->beginBlock(left.data(), right.data(), 1);
        for (const int note : top) p->noteOn(0, 1, note, 60);
        p->noteOn(0, 1, P::keyswitchStrumUp, 100);
        p->endBlock();
    }
    swept(true, "a strum key on its chord's sample did not strum it");
    for (const int note : top) key(note, false);
    // D0 leaves Keys mode: a chord sounds as it is played again.
    key(P::keyswitchAutomatic, true);
    advance(*p, 96001);
    for (const int note : top) key(note, true);
    expect(p->engine().getActiveVoiceCount() == 4, "keys stayed silent after D0");
    // A panic forgets Keys mode and every silent key.
    key(P::keyswitchStrumDown, true);
    key(40, true);
    p->reset();
    expect(p->strokeMode() == P::StrokeMode::Automatic, "reset kept Keys mode");
    key(40, false);
    key(40, true);
    expect(p->engine().heldString(40, 1) == 0, "a key silent before reset stayed silent after it");
}

// Explicit directions describe the player's gesture even when automatic
// alternate picking is ablated. Disable scatter only to isolate the sweep's
// endpoint timing; testStrumKeys above retains the production scatter path.
auto explicitStrokePerformer(acustra::PickingTechnique picking, bool alternatePicking)
{
    auto p = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    p->setParameters(parameters);
    acustra::PerformanceRealism realism;
    realism.alternatePicking = alternatePicking;
    realism.strumScatter = false;
    p->engine().setPerformanceRealism(realism);
    p->prepare(48000.0, 127);
    p->setGatherChords(false);
    return p;
}

void testExplicitSmallStrokes()
{
    using P = acustra::Performer;
    constexpr std::array<int, 4> top { 50, 55, 59, 64 };
    for (const auto picking : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Thumb,
                                acustra::PickingTechnique::Pick })
        for (const bool alternatePicking : { false, true })
            for (const int count : { 1, 2, 4 })
            {
                auto p = explicitStrokePerformer(picking, alternatePicking);
                const std::string context = " (technique "
                    + std::to_string(static_cast<int>(picking)) + ", alternatePicking "
                    + std::to_string(alternatePicking) + ", held notes "
                    + std::to_string(count) + ")";
                const auto check = [&](bool condition, const char* message)
                { expect(condition, (std::string(message) + context).c_str()); };
                keyswitch(*p, P::keyswitchStrumDown, true);
                // A live keyboard chord is fretted one key at a time, without
                // Gather Chords or any sound before the picking hand arrives.
                for (int index = 4 - count; index < 4; ++index)
                {
                    keyswitch(*p, top[static_cast<std::size_t>(index)], true);
                    advance(*p, 200);
                }
                check(p->engine().getActiveVoiceCount() == 0,
                      "a staggered fretting key sounded before its strum key");
                std::array<int, 4> strings {};
                strings.fill(-1);
                // Repeated directions must remain explicit instead of taking
                // the automatic hand's return; both keys also re-strum singles.
                for (const int key : { P::keyswitchStrumDown, P::keyswitchStrumDown,
                                       P::keyswitchStrumUp, P::keyswitchStrumUp })
                {
                    const bool upstroke = key == P::keyswitchStrumUp;
                    keyswitch(*p, key, true);
                    check(p->strokeMode() == P::StrokeMode::Keys,
                          "a strum key left Keys mode");
                    for (int index = 4 - count; index < 4; ++index)
                    {
                        const auto slot = static_cast<std::size_t>(index);
                        const int string = p->engine().heldString(top[slot], 1);
                        check(string >= 0, "a strum key lost a fretted key");
                        if (string < 0) continue;
                        check(Access::sign(p->engine(), string) == (upstroke ? -1.0f : 1.0f),
                              "F0/F#0 did not set the held key's explicit direction");
                        check(strings[slot] < 0 || strings[slot] == string,
                              "a strum key moved a key that remained held");
                        strings[slot] = string;
                    }
                    if (count > 1)
                    {
                        const int first = strings[static_cast<std::size_t>(upstroke ? 3 : 4 - count)];
                        const int last = strings[static_cast<std::size_t>(upstroke ? 4 - count : 3)];
                        if (first >= 0 && last >= 0)
                            check(Access::delay(p->engine(), first) == 0
                                      && Access::delay(p->engine(), last) > 0,
                                  "F0/F#0 did not sweep its held keys in the explicit direction");
                    }
                    advance(*p, 6000);
                }
                for (int index = 4 - count; index < 4; ++index)
                    keyswitch(*p, top[static_cast<std::size_t>(index)], false);
                for (int index = 4 - count; index < 4; ++index)
                    check(p->engine().heldString(top[static_cast<std::size_t>(index)], 1) < 0,
                          "a repeated explicit strum added an owner that survived key-up");
            }

    // Direction latches override independent finger/thumb pair plucks too:
    // a two-note chord is swept, just as a larger forced-direction chord is.
    for (const auto picking : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Thumb })
        for (const bool alternatePicking : { false, true })
            for (const int key : { P::keyswitchDownstroke, P::keyswitchUpstroke })
            {
                auto p = explicitStrokePerformer(picking, alternatePicking);
                const std::string context = " (technique "
                    + std::to_string(static_cast<int>(picking)) + ", direction key "
                    + std::to_string(key) + ", alternatePicking "
                    + std::to_string(alternatePicking) + ")";
                const auto check = [&](bool condition, const char* message)
                { expect(condition, (std::string(message) + context).c_str()); };
                keyswitch(*p, key, true);
                std::array<float, 1> left {}, right {};
                p->beginBlock(left.data(), right.data(), 1);
                p->noteOn(0, 1, 59, 100);
                p->noteOn(0, 1, 64, 100);
                p->endBlock();
                const bool upstroke = key == P::keyswitchUpstroke;
                const int low = p->engine().heldString(59, 1);
                const int high = p->engine().heldString(64, 1);
                check(low >= 0 && high >= 0, "C0/C#0 lost a two-note chord member");
                if (low >= 0 && high >= 0)
                {
                    check(Access::sign(p->engine(), low) == (upstroke ? -1.0f : 1.0f)
                              && Access::sign(p->engine(), high) == (upstroke ? -1.0f : 1.0f),
                          "C0/C#0 did not set a two-note chord's explicit direction");
                    check(Access::delay(p->engine(), upstroke ? high : low) == 0
                              && Access::delay(p->engine(), upstroke ? low : high) > 0,
                          "C0/C#0 did not sweep a two-note chord in its explicit direction");
                }
                keyswitch(*p, 59, false);
                keyswitch(*p, 64, false);
                check(p->engine().heldString(59, 1) < 0 && p->engine().heldString(64, 1) < 0,
                      "a forced two-note chord survived its keys' single key-ups");
            }

    // Disabling automatic alternation cannot disable an explicitly latched
    // picked direction. The latch still governs each individual pick stroke.
    for (const bool alternatePicking : { false, true })
        for (const int key : { P::keyswitchDownstroke, P::keyswitchUpstroke })
        {
            auto p = explicitStrokePerformer(acustra::PickingTechnique::Pick,
                                             alternatePicking);
            keyswitch(*p, key, true);
            keyswitch(*p, 64, true);
            const int string = p->engine().heldString(64, 1);
            expect(string >= 0, "C0/C#0 lost a picked single note");
            if (string >= 0)
                expect(Access::sign(p->engine(), string)
                           == (key == P::keyswitchUpstroke ? -1.0f : 1.0f),
                       "C0/C#0 lost a picked single's direction when automatic picking was ablated");
            keyswitch(*p, 64, false);
            expect(p->engine().heldString(64, 1) < 0,
                   "a latched-direction picked single stayed owned after key-up");
        }

    // The direction latches affect strums and picked notes. An ordinary
    // Finger/Thumb single still plucks toward the palm, unlike a strum key.
    for (const auto picking : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Thumb })
        for (const int key : { P::keyswitchAutomatic, P::keyswitchDownstroke,
                               P::keyswitchUpstroke })
        {
            auto p = explicitStrokePerformer(picking, true);
            keyswitch(*p, key, true);
            keyswitch(*p, 64, true);
            const int string = p->engine().heldString(64, 1);
            expect(string >= 0, "an ordinary Finger/Thumb single lost its key");
            if (string >= 0)
                expect(Access::sign(p->engine(), string) == 1.0f,
                       "a direction latch reversed an ordinary Finger/Thumb single");
            keyswitch(*p, 64, false);
            expect(p->engine().heldString(64, 1) < 0,
                   "an ordinary Finger/Thumb single stayed owned after key-up");
        }
}

int main()
{
    testFreshReleaseAndDefaultPaths();
    testScheduledDirectionsAndRetainedSources();
    testPerformerAlternationRestAndReset();
    testKeyswitches();
    testStrumKeys();
    testExplicitSmallStrokes();
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto reference = phrase(rate, 17);
        expect(phrase(rate, 127) == reference,
               "alternating stroke audio changed with host block partition");
    }
    if (failures == 0)
        std::cout << "Strum direction: fresh energy, scheduled releases, retained arrivals/tails, the dotted-eighth alternation window, keyswitches, strum keys, wrist reset and block invariance passed\n";
    return failures == 0 ? 0 : 1;
}
