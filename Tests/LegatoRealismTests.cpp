// Connected articulations are explicit; ordinary chord overlap and the
// retired CC68 remain unchanged. The source's existing waves and ownership
// transfer intact, while finger contact has a bounded smooth energy budget.
#include "DSP/AcustraPerformer.h"

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
    struct WaveState
    {
        std::array<std::array<float, AcustraEngine::maximumDelaySamples>, 2> lines;
        std::array<int, 2> index;
        std::array<float, 2> delay;
        std::uint32_t random, releaseRandom;
        bool tail;
        float pitch, contactAmplitude;
        int contactSamples;
        std::array<float, 1153> contactPulse;
    };
    static WaveState wave(const AcustraEngine& engine, int string)
    {
        const auto& v = engine.voices_[static_cast<std::size_t>(string)];
        return { { v.loops[0].delay, v.loops[1].delay },
                 { v.loops[0].writeIndex, v.loops[1].writeIndex },
                 { v.loops[0].currentDelay, v.loops[1].currentDelay },
                 v.randomState, v.releaseNoiseState, v.tailActive, v.attackPitchCents,
                 v.legatoContactAmplitude, v.legatoContactSamples,
                 v.legatoContactPulse };
    }
    static double lineSlope(const AcustraEngine& e, int string)
    {
        double result = 0.0;
        for (const auto& loop : e.voices_[static_cast<std::size_t>(string)].loops)
        {
            const int n = std::clamp(static_cast<int>(std::round(loop.currentDelay)),
                                     8, AcustraEngine::maximumDelaySamples - 3);
            const auto at = [&](int age)
            { return loop.delay[static_cast<std::size_t>((loop.writeIndex - age
                       + AcustraEngine::maximumDelaySamples) % AcustraEngine::maximumDelaySamples)]; };
            float previous = at(n);
            for (int age = n - 1; age >= 0; --age)
            {
                const float x = at(age);
                result += static_cast<double>(x - previous) * (x - previous);
                previous = x;
            }
        }
        return result;
    }
    struct FingerTravel
    {
        std::array<float, AcustraEngine::maximumDelaySamples> history;
        int writeIndex, remaining, age, samples;
        bool active;
        float amplitude;
        std::array<float, 1153> pulse;
    };
    static FingerTravel fingerTravel(const AcustraEngine& e, bool tail)
    {
        const auto& v = e.voices_[0];
        const auto& t = tail ? v.tailLegatoContactTravel : v.legatoContactTravel;
        return { t.history, t.writeIndex, t.historyRemaining,
                 tail ? v.tailLegatoContactAge : v.legatoContactAge,
                 tail ? v.tailLegatoContactSamples : v.legatoContactSamples,
                 t.active, tail ? v.tailLegatoContactAmplitude : v.legatoContactAmplitude,
                 tail ? v.tailLegatoContactPulse : v.legatoContactPulse };
    }
};
}

namespace
{
int failures = 0;
void expect(bool ok, const std::string& message)
{ if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

void continuousAndOwned()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 384000.0 })
        for (const std::array<int, 3> notes : {
            std::array<int, 3>{43, 45, 1}, {45, 43, 1},
            {52, 55, 3}, {55, 52, 3}, {76, 79, 6}, {79, 76, 6} })
        {
            const int string = notes[2] - 1;
            auto e = std::make_unique<acustra::AcustraEngine>();
            e->prepare(rate, 127);
            e->setStringPerChannelMode(true);
            e->noteOn(notes[0], 0.8f, notes[2]);
            std::array<float, 127> left {}, right {};
            for (int i = 0; i < 30; ++i) e->process(left.data(), right.data(), 127);
            const auto before = Access::wave(*e, string);
            const double energy = Access::lineSlope(*e, string);
            expect(e->transitionNote(notes[0], notes[1], 0.8f, notes[2]), "valid connected transition was rejected");
            const auto after = Access::wave(*e, string);
            expect(before.lines == after.lines && before.index == after.index,
                   "connected note replaced or re-plucked the existing travelling wave");
            expect(before.random == after.random, "connected note drew fresh picking noise");
            expect(before.releaseRandom == after.releaseRandom,
                   "finger friction consumed the key-up noise stream");
            expect(before.tail == after.tail, "connected note manufactured a repluck tail branch");
            expect(e->heldString(notes[0], notes[2]) < 0 && e->heldString(notes[1], notes[2]) == string,
                   "connected note did not transfer ownership on its original string");
            double pulseEnergy = 0.0, previous = 0.0;
            for (int age = 0; age <= after.contactSamples; ++age)
            {
                const double sample = after.contactAmplitude
                    * after.contactPulse[static_cast<std::size_t>(age)];
                pulseEnergy += (sample - previous) * (sample - previous);
                previous = sample;
            }
            expect(pulseEnergy <= energy * 0.02 * 0.8 * 0.8 * 1.00001,
                   "combined finger contact/friction exceeded its source slope-energy budget");
            expect(after.contactPulse[0] == 0.0f
                       && after.contactPulse[static_cast<std::size_t>(after.contactSamples)] == 0.0f,
                   "finger friction did not start/end at rest");
            // Remove the smooth contact in the slope inner product. The
            // remaining roughness must be present but a small energy share.
            double smoothNorm = 0.0, cross = 0.0, total = 0.0;
            double lastSmooth = 0.0, lastPulse = 0.0;
            for (int age = 1; age <= after.contactSamples; ++age)
            {
                const double sine = std::sin(3.14159265358979323846 * age / after.contactSamples);
                const double smooth = age == after.contactSamples ? 0.0 : sine * sine;
                const double pulse = after.contactPulse[static_cast<std::size_t>(age)];
                const double ds = smooth - lastSmooth, dp = pulse - lastPulse;
                smoothNorm += ds * ds; cross += ds * dp; total += dp * dp;
                lastSmooth = smooth; lastPulse = pulse;
            }
            const double roughShare = (total - cross * cross / smoothNorm) / total;
            expect(roughShare > 0.01 && roughShare < 0.15,
                   "fret friction was absent or overwhelmed the smooth contact");
            e->noteOff(notes[0], notes[2]);
            expect(e->heldString(notes[1], notes[2]) == string, "late source off stole target ownership");
            for (int i = 0; i < 100; ++i)
            {
                e->process(left.data(), right.data(), 127);
                for (float x : left) expect(std::isfinite(x), "connected transition became non-finite");
                for (float x : right) expect(std::isfinite(x), "connected transition became non-finite");
            }
            expect(std::abs(Access::wave(*e, string).delay[0] - before.delay[0]) > 0.1f,
                   "connected note did not change its sounding pitch");
            e->noteOff(notes[1], notes[2]);
            expect(e->heldString(notes[1], notes[2]) < 0, "target off failed to release ownership");
            e->allSoundOff(notes[2]);
            e->process(left.data(), right.data(), 127);
            expect(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0f; }),
                   "all sound off left finger-contact output running");
        }
}

void invalidIsInert()
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    e->prepare(48000.0, 127); e->setStringPerChannelMode(true);
    e->noteOn(43, 0.8f); e->noteOn(43, 0.6f);
    const auto before = acustra::AcustraEngineTestAccess::wave(*e, 0);
    expect(!e->transitionNote(43, 45, 0.8f), "multiply owned source was silently transferred");
    e->noteOff(43);
    for (const std::array<int, 2> notes : { std::array<int, 2>{42, 45}, {43, 39}, {43, 80}, {43, 43} })
        expect(!e->transitionNote(notes[0], notes[1], 0.8f), "unsupported transition changed state");
    expect(!e->transitionNote(43, 45, std::numeric_limits<float>::quiet_NaN()), "NaN velocity was accepted");
    expect(!e->transitionNote(43, 45, 0.8f, 17), "invalid channel was accepted");
    expect(before.lines == acustra::AcustraEngineTestAccess::wave(*e, 0).lines,
           "rejected transition wrote to the existing wave");
    e->reset();
    expect(!e->transitionNote(43, 45, 0.8f), "reset left stale source ownership");
    e->noteOn(43, 0.8f, 1, 480);
    expect(!e->transitionNote(43, 45, 0.8f), "scheduled source was silently plucked by transition");
}

void repluckRetainsFingerTransit()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (int arrived : { 0, 1, 60, 180, 400 })
    {
        auto e = std::make_unique<acustra::AcustraEngine>();
        e->prepare(48000.0, 127); e->setStringPerChannelMode(true);
        e->noteOn(43, 0.8f);
        std::array<float, 127> left {}, right {};
        for (int i = 0; i < 40; ++i) e->process(left.data(), right.data(), 127);
        expect(e->transitionNote(43, 45, 0.8f), "transit fixture did not start its hammer-on");
        for (int at = 0; at < arrived; at += 127)
            e->process(left.data(), right.data(), std::min(127, arrived - at));
        const auto before = Access::fingerTravel(*e, false);
        e->noteOn(45, 0.7f);
        const auto main = Access::fingerTravel(*e, false);
        const auto tail = Access::fingerTravel(*e, true);
        expect(!main.active && main.samples == 0,
               "re-pluck left the old finger contact entering the new main loop");
        expect(tail.history == before.history && tail.writeIndex == before.writeIndex
                   && tail.remaining == before.remaining && tail.age == before.age
                   && tail.samples == before.samples && tail.active == before.active
                   && tail.pulse == before.pulse,
               "re-pluck discarded or changed the finger contact's retained transit/pulse");
        const int tailIndex = tail.writeIndex;
        e->process(left.data(), right.data(), 127);
        expect(Access::fingerTravel(*e, true).writeIndex != tailIndex,
               "retained finger contact did not continue through the old tail branch");
        expect(!Access::fingerTravel(*e, false).active,
               "old finger contact reappeared in the new main string");
        e->allSoundOff();
        expect(!Access::fingerTravel(*e, true).active
                   && Access::fingerTravel(*e, true).samples == 0,
               "panic retained stale finger-contact transit");
    }
}

void keyUpFinishesExistingFingerPulse()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (bool hammer : { false, true })
        for (int age : { 0, 1, 48, 96 })
        {
            auto e = std::make_unique<acustra::AcustraEngine>();
            e->prepare(48000.0, 127); e->setStringPerChannelMode(true);
            const int source = hammer ? 43 : 45;
            const int target = hammer ? 45 : 43;
            e->noteOn(source, 0.8f);
            std::array<float, 127> left {}, right {};
            for (int i = 0; i < 40; ++i) e->process(left.data(), right.data(), 127);
            expect(e->transitionNote(source, target, 0.8f), "release fixture did not start its connected note");
            if (age > 0) e->process(left.data(), right.data(), age);
            const auto before = Access::fingerTravel(*e, false);
            e->noteOff(target);
            const auto after = Access::fingerTravel(*e, false);
            expect(after.age == before.age && after.samples == before.samples
                       && after.amplitude == before.amplitude
                       && after.history == before.history && after.writeIndex == before.writeIndex,
                   "key-up cut or restarted the explicitly initiated smooth finger pulse");
            expect(e->heldString(target) < 0, "finishing contact retained target-key ownership");
            for (int block = 0; block < 5; ++block)
            {
                e->process(left.data(), right.data(), 127);
                for (float x : left) expect(std::isfinite(x), "released finger contact became non-finite");
            }
            expect(Access::fingerTravel(*e, false).samples == 0,
                   "released contact pulse did not finish within its original duration");
        }
}

std::vector<float> midiPerformance(int mode, bool reverseOffOrder = false,
                                   bool gather = false)
{
    auto p = std::make_unique<acustra::Performer>();
    p->prepare(48000.0, 64); p->setGatherChords(gather);
    p->engine().setStringPerChannelMode(true);
    std::vector<float> left(24000), right(left.size());
    for (int at = 0; at < static_cast<int>(left.size()); at += 64)
    {
        const int count = std::min(64, static_cast<int>(left.size()) - at);
        p->beginBlock(left.data() + at, right.data() + at, count);
        if (at == 0) p->noteOn(0, 1, 43, 100);
        if (at == 4800)
        {
            if (mode == 1 || mode == 2 || mode == 4) p->controlChange(0, 1, 65, 127);
            if (mode == 1 || mode == 3 || mode == 4) p->controlChange(0, 1, 84, 43);
            if (mode == 4 || mode == 6) p->controlChange(0, 1, 121, 0);
            if (mode == 5) p->controlChange(0, 1, 68, 127);
            if (reverseOffOrder) p->noteOff(0, 1, 43, 64);
            p->noteOn(0, 1, 45, 90);
            if (!reverseOffOrder) p->noteOff(0, 1, 43, 64);
        }
        if (at == 9600) p->noteOff(0, 1, 45, 64);
        p->endBlock();
    }
    expect(p->engine().heldString(43) < 0 && p->engine().heldString(45) < 0,
           "MIDI connected phrase retained stale key ownership");
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void midiRequiresExplicitSource()
{
    const auto ordinary = midiPerformance(0);
    expect(midiPerformance(1) == midiPerformance(1),
           "fresh connected performances did not reproduce their fret friction");
    expect(ordinary != midiPerformance(1), "CC65 plus CC84 did not enable a connected articulation");
    for (int mode : { 2, 3, 5 })
        expect(ordinary == midiPerformance(mode), "switch/source/CC68 inferred a connected note: " + std::to_string(mode));
    expect(midiPerformance(4) == midiPerformance(6), "controller reset left a stale transition request");
    expect(midiPerformance(1) == midiPerformance(1, true), "same-sample source-off ordering changed articulation");
    expect(midiPerformance(1, false, true) == midiPerformance(1, true, true),
           "gathered source-off ordering changed articulation");
}
}

int main()
{
    continuousAndOwned(); invalidIsInert(); repluckRetainsFingerTransit();
    keyUpFinishesExistingFingerPulse();
    midiRequiresExplicitSource();
    if (failures) return 1;
    std::cout << "Connected finger articulation: wave continuity, bounded contact, explicit MIDI and ownership passed\n";
    return 0;
}
