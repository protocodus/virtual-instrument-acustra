// A strum crosses every string between its first and last. One the chord
// leaves out is kept quiet by the fretting hand lying on it, so the pick
// gives it a dead stroke: plucked at its place in the sweep 4.6 dB under a
// played string, then damped by the finger's light touch, a T60 of 43
// periods, as GuitarSet's comping measures it (AcustraEngine::muteStroke,
// Tools/MeasureMutedStrokes.py).
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static const AcustraEngine::Voice& voice(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)]; }
};
}

namespace
{
using acustra::Performer;
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

std::unique_ptr<Performer> player(bool muted,
                                  acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
                                  double rate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.mutedStrings = muted;
    // The dead strokes' places are measured against the sweep itself; their
    // scatter about it, with the chord's own strings, is Acustra.StrumScatter's.
    realism.strumScatter = false;
    performer->engine().setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    performer->setParameters(parameters);
    performer->prepare(rate, 512);
    performer->setTempoBpm(120.0);
    return performer;
}

struct Event
{
    std::int64_t at;
    int note, velocity; // velocity 0 is a key-up, -1 Panic (CC120)
};

template <typename Observe>
std::vector<float> play(Performer& performer, std::vector<Event> score,
                        std::int64_t length, int block, Observe&& observe)
{
    std::stable_sort(score.begin(), score.end(),
        [](const Event& a, const Event& b) { return a.at < b.at; });
    std::vector<float> out(static_cast<std::size_t>(2 * length));
    std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
    std::size_t next = 0;
    for (std::int64_t start = 0; start < length; start += block)
    {
        const int count = static_cast<int>(std::min<std::int64_t>(block, length - start));
        performer.beginBlock(left.data(), right.data(), count);
        while (next < score.size() && score[next].at < start + count)
        {
            const auto& event = score[next++];
            const int offset = static_cast<int>(event.at - start);
            if (event.velocity > 0)
                performer.noteOn(offset, 1, event.note, event.velocity);
            else if (event.velocity == 0)
                performer.noteOff(offset, 1, event.note);
            else
                performer.controlChange(offset, 1, 120, 0);
        }
        performer.endBlock();
        for (int i = 0; i < count; ++i)
        {
            out[static_cast<std::size_t>(2 * (start + i))] = left[static_cast<std::size_t>(i)];
            out[static_cast<std::size_t>(2 * (start + i) + 1)] = right[static_cast<std::size_t>(i)];
        }
        observe(start + count);
    }
    return out;
}

std::vector<float> play(Performer& performer, const std::vector<Event>& score,
                        std::int64_t length, int block = 64)
{
    return play(performer, score, length, block, [](std::int64_t) {});
}

std::vector<Event> chord(const std::vector<int>& notes, std::int64_t at,
                         std::int64_t until = -1, int velocity = 100)
{
    std::vector<Event> score;
    for (int note : notes)
        score.push_back({ at, note, velocity });
    if (until >= 0)
        for (int note : notes)
            score.push_back({ until, note, 0 });
    return score;
}

std::vector<Event> join(std::vector<Event> a, const std::vector<Event>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// Open E2, B3 and E4: strings 0, 4 and 5, so the pick crosses A, D and G.
const std::vector<int> sparse { 40, 59, 64 };

void testTheSweepMutesWhatItCrosses()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const bool upstroke : { false, true })
        {
            auto performer = player(true, acustra::PickingTechnique::Pick, rate);
            const auto at = static_cast<std::int64_t>(0.1 * rate);
            // A downstroke, then its return inside the dotted eighth.
            auto score = chord(sparse, at, at + static_cast<std::int64_t>(0.1 * rate));
            if (upstroke)
                score = join(score, chord(sparse, at + static_cast<std::int64_t>(0.2 * rate)));
            const auto measured = upstroke ? at + static_cast<std::int64_t>(0.2 * rate) : at;
            std::array<int, 6> delay {};
            delay.fill(-2);
            play(*performer, score, measured + 2, 1, [&](std::int64_t now)
            {
                if (now - 1 != measured)
                    return;
                for (int string = 0; string < 6; ++string)
                {
                    const auto& v = Access::voice(performer->engine(), string);
                    delay[static_cast<std::size_t>(string)] = v.pluckDelay;
                    if (string >= 1 && string <= 3)
                        expect(v.mutedStroke && !v.keyDown && v.ownerCount == 0,
                               "a string the stroke crosses was not muted under the hand");
                }
            });
            // Down: E2 now, then A, D, G, B, E in order; up: the reverse.
            const std::array<int, 6> order = upstroke
                ? std::array<int, 6> { 5, 4, 3, 2, 1, 0 } : std::array<int, 6> { 0, 1, 2, 3, 4, 5 };
            expect(delay[static_cast<std::size_t>(order[0])] == 0,
                   "the stroke's first string did not sound at its note-on");
            for (std::size_t k = 1; k < order.size(); ++k)
                expect(delay[static_cast<std::size_t>(order[k])]
                           > delay[static_cast<std::size_t>(order[k - 1])],
                       "a muted string did not take its place in the sweep");
        }
}

void testTheDeadStrokeDiesUnderTheFinger()
{
    // The A string's dead stroke takes the measured light touch, a T60 of 43
    // periods from the finger's landing; its level is 60 dB down within that
    // T60, and the string is handed back to the open string after the T60
    // and the hand's 80 ms.
    for (const double rate : { 48000.0, 96000.0 })
    {
        auto performer = player(true, acustra::PickingTechnique::Pick, rate);
        // This audits the original transient dead-stroke retirement. Held
        // shape ownership and its longer contact are tested separately.
        auto realism = performer->engine().performanceRealism();
        realism.heldShapeMute = false;
        performer->engine().setPerformanceRealism(realism);
        const auto at = static_cast<std::int64_t>(0.05 * rate);
        const double t60 = 43.0 / 110.0;
        float peak = 0.0f;
        double lawError = 0.0;
        std::int64_t firedAt = -1, peakAt = -1, quietAt = -1, returnedAt = -1;
        play(*performer, chord(sparse, at), static_cast<std::int64_t>(0.8 * rate), 1,
             [&](std::int64_t now)
        {
            const auto& v = Access::voice(performer->engine(), 1);
            if (now > at && v.mutedStroke)
                lawError = std::max(lawError, std::abs(v.releaseSeconds - t60));
            if (firedAt < 0 && v.attackFired && v.mutedStroke)
                firedAt = now;
            if (firedAt >= 0 && returnedAt < 0)
            {
                if (v.level > peak)
                {
                    peak = v.level;
                    peakAt = now;
                }
                if (quietAt < 0 && v.level < 1.0e-3f * peak)
                    quietAt = now;
                if (!v.played)
                    returnedAt = now;
            }
        });
        std::cout << rate << " Hz: A's dead stroke 60 dB down after "
                  << 1000.0 * static_cast<double>(quietAt - peakAt) / rate << " ms, returned after "
                  << 1000.0 * static_cast<double>(returnedAt - firedAt) / rate << " ms\n";
        expect(lawError < 1.0e-4, "the muted string did not take the measured 43-period touch");
        expect(firedAt > 0 && peak > 0.0f && quietAt > peakAt
                   && static_cast<double>(quietAt - peakAt) < t60 * rate,
               "the dead stroke did not fall 60 dB within its T60");
        expect(std::abs(static_cast<double>(returnedAt - firedAt) - (t60 + 0.08) * rate)
                   < 0.002 * rate,
               "the muted string was not handed back after its T60 and the hand's 80 ms");
    }
}

void testHeldAndPedalledStringsAreLeftAlone()
{
    // A key holding the D string (D3 open), or the pedal holding it, keeps
    // it out of the stroke's muting: the chord does not own it but the
    // player does.
    for (const bool pedal : { false, true })
    {
        auto performer = player(true);
        std::vector<Event> score { { 1000, 50, 90 } };
        if (pedal)
        {
            score.push_back({ 1500, 50, 0 });
        }
        score = join(score, chord(sparse, 4800));
        bool muted = false, sounding = true;
        std::vector<float> left(64), right(64);
        // Pedal down before the key-up so the D3 is held by it.
        if (pedal)
        {
            performer->beginBlock(left.data(), right.data(), 1);
            performer->controlChange(0, 1, 64, 127);
            performer->endBlock();
        }
        play(*performer, score, 9600, 16, [&](std::int64_t now)
        {
            if (now < 4800)
                return;
            const auto& v = Access::voice(performer->engine(), 2);
            muted |= v.mutedStroke;
            sounding &= v.played && v.midiNote == 50;
        });
        expect(!muted && sounding, "a held or pedalled string was muted by the stroke");
    }
}

void testWhatStaysUnchanged()
{
    // Contiguous chords have no string to cross; a finger-plucked chord
    // crosses none; and with the switch off the crossed strings are left as
    // they were. All bit-identical with the switch on and off.
    const std::vector<std::vector<int>> contiguous {
        { 40, 47, 52, 56, 59, 64 }, { 45, 52, 57, 60, 64 }, { 55, 59, 62, 67 } };
    for (const auto picking : { acustra::PickingTechnique::Pick, acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Thumb })
        for (const auto& shape : contiguous)
        {
            auto on = player(true, picking), off = player(false, picking);
            const auto score = join(chord(shape, 1000, 20000), chord(shape, 24000, 40000));
            expect(play(*on, score, 48000) == play(*off, score, 48000),
                   "a chord with no string to cross changed");
        }
    {
        auto on = player(true, acustra::PickingTechnique::Finger);
        auto off = player(false, acustra::PickingTechnique::Finger);
        const auto score = chord(sparse, 1000, 20000);
        expect(play(*on, score, 48000) == play(*off, score, 48000),
               "a finger-plucked chord muted a string it never crosses");
    }
    {
        auto off = player(false);
        bool touched = false;
        play(*off, chord(sparse, 1000), 9600, 16, [&](std::int64_t)
        {
            for (int string = 1; string <= 3; ++string)
                touched |= Access::voice(off->engine(), string).played;
        });
        expect(!touched, "with the switch off a crossed string was still taken");
    }
    {
        // And the dead strokes are there to hear.
        auto on = player(true), off = player(false);
        const auto score = chord(sparse, 1000);
        const auto a = play(*on, score, 9600), b = play(*off, score, 9600);
        double difference = 0.0, level = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            difference += (a[i] - b[i]) * (a[i] - b[i]);
            level += b[i] * b[i];
        }
        expect(difference > 1.0e-3 * level, "the dead strokes made no audible difference");
    }
}

void testANoteTakesAMutedStringBack()
{
    // A key on a muted string is an ordinary note: it owns the string, the
    // finger lifts, and it rings as on a fresh engine.
    auto muted = player(true), plain = player(false);
    auto score = chord(sparse, 1000);
    score.push_back({ 1600, 45, 100 });  // the open A, inside the dead stroke's life
    std::array<bool, 2> owned { false, false };
    const auto a = play(*muted, score, 48000, 16, [&](std::int64_t now)
    {
        if (now > 1700)
        {
            const auto& v = Access::voice(muted->engine(), 1);
            owned[0] = v.keyDown && !v.mutedStroke && v.midiNote == 45;
        }
    });
    const auto b = play(*plain, score, 48000, 16, [&](std::int64_t now)
    {
        if (now > 1700)
            owned[1] = Access::voice(plain->engine(), 1).keyDown;
    });
    expect(owned[0] && owned[1], "a key could not take back a muted string");
    // Its sustain matches the unmuted engine's within a decibel from 0.2 s.
    double ea = 0.0, eb = 0.0;
    for (std::size_t i = 2 * 9600; i < a.size(); ++i)
    {
        ea += a[i] * a[i];
        eb += b[i] * b[i];
    }
    expect(std::abs(10.0 * std::log10(ea / eb)) < 1.0,
           "a note on a once-muted string did not ring as an ordinary note");
}

void testPanicAndBlocks()
{
    {
        auto performer = player(true);
        auto score = chord(sparse, 1000);
        score.push_back({ 1400, 0, -1 }); // CC120 all sound off
        bool clean = true;
        play(*performer, score, 4800, 16, [&](std::int64_t now)
        {
            if (now > 1500)
                for (int string = 0; string < 6; ++string)
                    clean &= !Access::voice(performer->engine(), string).mutedStroke;
        });
        expect(clean, "panic left a muted stroke behind");
    }
    const auto score = join(chord(sparse, 1000, 9000), chord(sparse, 12000, 30000));
    auto reference = player(true);
    const auto expected = play(*reference, score, 36000, 64);
    for (const int block : { 1, 61, 512 })
    {
        auto performer = player(true);
        expect(play(*performer, score, 36000, block) == expected,
               "a muted stroke depends on the block size");
    }
}
} // namespace

int main()
{
    std::cout << std::unitbuf;
    testTheSweepMutesWhatItCrosses();
    testTheDeadStrokeDiesUnderTheFinger();
    testHeldAndPedalledStringsAreLeftAlone();
    testWhatStaysUnchanged();
    testANoteTakesAMutedStringBack();
    testPanicAndBlocks();
    if (failures != 0)
    {
        std::cerr << failures << " muted string check(s) failed\n";
        return 1;
    }
    std::cout << "muted string checks passed\n";
    return 0;
}
