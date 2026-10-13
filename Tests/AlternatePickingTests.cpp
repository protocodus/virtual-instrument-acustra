// A plectrum alternates: under Pick a single note is a stroke of the same
// hand as the strums, a return within the alternation window and a
// downstroke after a rest. Fingers and the thumb pluck toward the palm.
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
    static float sign(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckParallelSign; }
    static bool attackFired(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].attackFired; }
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

struct Event
{
    enum Kind { On, Off, Controller, Keyswitch } kind;
    std::int64_t at;
    int channel, note, value;
};

std::unique_ptr<Performer> player(acustra::PickingTechnique picking,
                                  bool alternate = true, double rate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.alternatePicking = alternate;
    performer->engine().setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    performer->setParameters(parameters);
    performer->prepare(rate, 512);
    performer->setTempoBpm(120.0);
    return performer;
}

// Plays the score through the Performer's MIDI path in blocks of `block`
// samples. `observe` runs after every block, so the stroke a note was given
// can be read once its (possibly scheduled) attack has fired.
template <typename Observe>
std::vector<float> play(Performer& performer, const std::vector<Event>& score,
                        std::int64_t length, int block, Observe&& observe)
{
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
            switch (event.kind)
            {
                case Event::On: case Event::Keyswitch:
                    performer.noteOn(offset, event.channel, event.note, event.value); break;
                case Event::Off:
                    performer.noteOff(offset, event.channel, event.note); break;
                case Event::Controller:
                    performer.controlChange(offset, event.channel, event.note, event.value); break;
            }
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

// The stroke each named note was given, read as it sounds.
std::vector<float> strokes(acustra::PickingTechnique picking,
                           const std::vector<Event>& score,
                           const std::vector<int>& notes, bool alternate = true)
{
    auto performer = player(picking, alternate);
    std::vector<float> signs(notes.size(), 0.0f);
    std::vector<bool> seen(notes.size(), false);
    std::int64_t length = 0;
    for (const auto& event : score) length = std::max(length, event.at + 4800);
    play(*performer, score, length, 32, [&](std::int64_t)
    {
        for (std::size_t index = 0; index < notes.size(); ++index)
        {
            const int string = performer->engine().heldString(notes[index], 1);
            if (!seen[index] && string >= 0 && Access::attackFired(performer->engine(), string))
            {
                signs[index] = Access::sign(performer->engine(), string);
                seen[index] = true;
            }
        }
    });
    for (std::size_t index = 0; index < notes.size(); ++index)
        expect(seen[index], "a picked note never sounded");
    return signs;
}

constexpr std::int64_t beat = 24000; // 120 BPM at 48 kHz

// One note at a time, each released before the next.
std::vector<Event> line(const std::vector<int>& notes, std::int64_t spacing,
                        std::int64_t start = 0)
{
    std::vector<Event> score;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const std::int64_t at = start + static_cast<std::int64_t>(index) * spacing;
        score.push_back({ Event::On, at, 1, notes[index], 100 });
        score.push_back({ Event::Off, at + spacing - 200, 1, notes[index], 64 });
    }
    std::sort(score.begin(), score.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    return score;
}

void testEighthNotesAlternate()
{
    // Eighth notes at 120 BPM: down, up, down, up, on different strings and
    // on one string, as strict alternate picking plays them.
    const std::vector<int> across { 52, 57, 62, 67, 71, 76 };
    const auto signs = strokes(acustra::PickingTechnique::Pick, line(across, beat / 2), across);
    for (std::size_t index = 0; index < signs.size(); ++index)
        expect(signs[index] == (index % 2 == 0 ? 1.0f : -1.0f),
               "eighth notes under Pick did not alternate down and up");
    const std::vector<int> tremolo { 64, 65, 64, 65 };
    const auto repeated = strokes(acustra::PickingTechnique::Pick,
                                  line(tremolo, beat / 4), tremolo);
    expect(repeated[0] == 1.0f && repeated[1] == -1.0f,
           "sixteenth notes under Pick did not alternate");
}

void testRestStartsADownstroke()
{
    // Quarter notes are outside the dotted-eighth window: every one is a
    // downstroke, as a picking hand plays quarter notes.
    const std::vector<int> notes { 55, 57, 59, 60 };
    for (const float sign : strokes(acustra::PickingTechnique::Pick, line(notes, beat), notes))
        expect(sign == 1.0f, "a quarter note under Pick was not a downstroke");
    // After a rest the hand starts again on a downstroke.
    auto score = line({ 55, 57, 59 }, beat / 2);
    const auto later = line({ 60, 62 }, beat / 2, 3 * beat);
    score.insert(score.end(), later.begin(), later.end());
    const auto signs = strokes(acustra::PickingTechnique::Pick, score, { 55, 57, 59, 60, 62 });
    expect(signs[2] == 1.0f && signs[3] == 1.0f && signs[4] == -1.0f,
           "a picked line after a rest did not restart on a downstroke");
}

void testFingersAndThumbPluckTowardThePalm()
{
    const std::vector<int> notes { 52, 57, 62, 67 };
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                  acustra::PickingTechnique::Thumb })
        for (const float sign : strokes(technique, line(notes, beat / 2), notes))
            expect(sign == 1.0f, "a finger or thumb note took an upstroke");
}

void testFingerPerformanceIsUntouched()
{
    // The switch exists for Pick alone: a Finger performance with single
    // notes between strums renders bit for bit with it on or off.
    std::vector<Event> score;
    for (int bar = 0; bar < 2; ++bar)
    {
        const std::int64_t at = bar * 2 * beat;
        score.push_back({ Event::On, at, 1, 40, 96 });
        score.push_back({ Event::Off, at + beat / 2 - 100, 1, 40, 64 });
        for (int stroke = 1; stroke < 4; ++stroke)
            for (int note : { 52, 56, 59, 64 })
            {
                score.push_back({ Event::On, at + stroke * beat / 2, 1, note, 90 });
                score.push_back({ Event::Off, at + (stroke + 1) * beat / 2 - 100, 1, note, 64 });
            }
    }
    std::sort(score.begin(), score.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    auto on = player(acustra::PickingTechnique::Finger, true);
    auto off = player(acustra::PickingTechnique::Finger, false);
    expect(play(*on, score, 5 * beat) == play(*off, score, 5 * beat),
           "alternate picking changed a Finger performance");
}

void testStrumsAndPickedNotesShareOneHand()
{
    // A bass note on the beat and a strum on the off-beat: the strum is the
    // hand's return, an upstroke, and the next on-beat strum a downstroke.
    std::vector<Event> score { { Event::On, 0, 1, 40, 100 }, { Event::Off, beat / 2 - 100, 1, 40, 64 } };
    for (int note : { 59, 64, 68 })
    {
        score.push_back({ Event::On, beat / 2, 1, note, 90 });
        score.push_back({ Event::Off, beat - 100, 1, note, 64 });
        score.push_back({ Event::On, beat, 1, note, 90 });
    }
    std::sort(score.begin(), score.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    auto performer = player(acustra::PickingTechnique::Pick);
    std::vector<float> upbeat, downbeat;
    play(*performer, score, 2 * beat, 32, [&](std::int64_t now)
    {
        if (now <= beat / 2 || (now > beat - 1000 && now <= beat))
            return;
        auto& out = now <= beat ? upbeat : downbeat;
        if (out.empty())
            for (int note : { 59, 64, 68 })
            {
                const int string = performer->engine().heldString(note, 1);
                if (string >= 0)
                    out.push_back(Access::sign(performer->engine(), string));
            }
    });
    expect(upbeat.size() == 3 && std::all_of(upbeat.begin(), upbeat.end(),
                                              [](float s) { return s == -1.0f; }),
           "the strum after a picked bass note was not the hand's upstroke");
    expect(downbeat.size() == 3 && std::all_of(downbeat.begin(), downbeat.end(),
                                                [](float s) { return s == 1.0f; }),
           "the on-beat strum after an upstroke was not a downstroke");
}

void testOneStrokeArrivingOverAFewMilliseconds()
{
    // A live chord's keys, or a guitar controller's strings, a few
    // milliseconds apart are one stroke: one direction, not one per note.
    std::vector<Event> score;
    const std::vector<int> chord { 52, 55, 59, 64 };
    for (std::size_t index = 0; index < chord.size(); ++index)
        score.push_back({ Event::On, static_cast<std::int64_t>(index) * 240, 1, chord[index], 90 });
    for (const float sign : strokes(acustra::PickingTechnique::Pick, score, chord))
        expect(sign == 1.0f, "a rolled chord under Pick flipped direction between its notes");
    // The same holds when the roll ends in a group large enough to strum.
    std::vector<Event> split { { Event::On, 0, 1, 52, 90 } };
    for (int note : { 55, 59, 64 })
        split.push_back({ Event::On, 480, 1, note, 90 });
    for (const float sign : strokes(acustra::PickingTechnique::Pick, split, chord))
        expect(sign == 1.0f, "a strum continuing a picked note within the chord window flipped direction");
}

void testKeyswitchesLatchPickedStrokes()
{
    for (const int key : { Performer::keyswitchDownstroke, Performer::keyswitchUpstroke })
    {
        std::vector<Event> score { { Event::Keyswitch, 0, 1, key, 100 } };
        const auto notes = line({ 55, 57, 59, 60 }, beat / 2, 100);
        score.insert(score.end(), notes.begin(), notes.end());
        const float expected = key == Performer::keyswitchUpstroke ? -1.0f : 1.0f;
        for (const float sign : strokes(acustra::PickingTechnique::Pick, score, { 55, 57, 59, 60 }))
            expect(sign == expected, "a direction keyswitch did not latch picked notes");
    }
}

void testConnectedTransitionIsNotAStroke()
{
    // CC65 with CC84 naming the held source makes the next note a fretting
    // hand's transition: the plectrum does not move, so the note after it
    // is still the hand's return.
    std::vector<Event> score {
        { Event::On, 0, 1, 55, 100 },
        { Event::Controller, 100, 1, 65, 127 },
        { Event::Controller, 100, 1, 84, 55 },
        { Event::On, beat / 4, 1, 57, 100 },
        { Event::Off, beat / 4 + 50, 1, 55, 64 },
        { Event::Controller, beat / 4 + 60, 1, 65, 0 },
        { Event::Off, beat / 2 - 100, 1, 57, 64 },
        { Event::On, beat / 2, 1, 62, 100 },
    };
    const auto signs = strokes(acustra::PickingTechnique::Pick, score, { 55, 62 });
    expect(signs[0] == 1.0f && signs[1] == -1.0f,
           "a connected transition was counted as a plectrum stroke");
}

void testSwitchOffPicksEveryNoteDown()
{
    const std::vector<int> notes { 52, 57, 62, 67 };
    for (const float sign : strokes(acustra::PickingTechnique::Pick,
                                    line(notes, beat / 2), notes, false))
        expect(sign == 1.0f, "alternate picking switched off still alternated");
}

void testDirectionIsHeardAndBlockIndependent()
{
    // The upstroke releases its sideways plane the other way: the same two
    // notes render differently with the switch on, and identically whatever
    // the host's block size.
    const auto score = line({ 57, 57 }, beat / 2);
    auto on = player(acustra::PickingTechnique::Pick);
    auto off = player(acustra::PickingTechnique::Pick, false);
    const auto alternating = play(*on, score, 2 * beat);
    const auto downstrokes = play(*off, score, 2 * beat);
    // Interleaved stereo: the second note starts at sample beat / 2.
    const auto second = static_cast<std::size_t>(2 * (beat / 2));
    double difference = 0.0, energy = 0.0;
    for (std::size_t i = second; i < alternating.size(); ++i)
    {
        difference += (alternating[i] - downstrokes[i]) * (alternating[i] - downstrokes[i]);
        energy += downstrokes[i] * downstrokes[i];
    }
    const bool firstIdentical = std::equal(alternating.begin(),
        alternating.begin() + static_cast<std::ptrdiff_t>(second), downstrokes.begin());
    expect(firstIdentical, "the first downstroke changed with alternate picking");
    expect(difference > 1.0e-3 * energy, "an upstroke sounded exactly like a downstroke");
    for (const int block : { 1, 61, 512 })
    {
        auto other = player(acustra::PickingTechnique::Pick);
        expect(play(*other, score, 2 * beat, block) == alternating,
               "alternate picking depended on the host's block size");
    }
}
} // namespace

int main()
{
    testEighthNotesAlternate();
    testRestStartsADownstroke();
    testFingersAndThumbPluckTowardThePalm();
    testFingerPerformanceIsUntouched();
    testStrumsAndPickedNotesShareOneHand();
    testOneStrokeArrivingOverAFewMilliseconds();
    testKeyswitchesLatchPickedStrokes();
    testConnectedTransitionIsNotAStroke();
    testSwitchOffPicksEveryNoteDown();
    testDirectionIsHeardAndBlockIndependent();
    if (failures != 0)
    {
        std::cerr << failures << " alternate picking check(s) failed\n";
        return 1;
    }
    std::cout << "alternate picking checks passed\n";
    return 0;
}
