// The fretting hand in the string allocator (AcustraEngine::chooseString,
// planChord and reshapeFormingChord): where live keyboard notes are fretted.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static int chooseString(const AcustraEngine& engine, int midiNote)
    {
        return engine.chooseString(midiNote);
    }
    static int chooseStringWithoutHand(const AcustraEngine& engine, int midiNote)
    {
        return engine.chooseStringWithoutHand(midiNote);
    }
    static bool contact(const AcustraEngine& engine, int string)
    {
        const auto& loops = engine.voices_[static_cast<std::size_t>(string)].loops;
        return loops[0].gestureContact.active || loops[1].gestureContact.active;
    }
    static int returnSamples(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].returnSamples;
    }
    // The level is only the allocator's observer. Changing it does not create
    // a hand contact, alter any travelling wave, or change the release deadline.
    static void quietObserver(AcustraEngine& engine, int string)
    {
        engine.voices_[static_cast<std::size_t>(string)].level = 0.0f;
    }
    static void removeContact(AcustraEngine& engine, int string, int plane)
    {
        engine.voices_[static_cast<std::size_t>(string)]
            .loops[static_cast<std::size_t>(plane)].gestureContact = {};
    }
    static void forgetHand(AcustraEngine& engine)
    {
        for (auto& finger : engine.hand_) finger.valid = false;
        engine.handPosition_.valid = false;
    }
    static int handShift(const AcustraEngine& engine)
    {
        return engine.handShiftFrets_;
    }
    // The pick's contact has begun the arrival planned at the note-on:
    // how far short of its fret the finger is, in metres of string (above
    // zero for one coming down the neck), or zero for none.
    static double arrival(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return voice.fretMotion.arriving
            ? voice.speakingLengthMetres - voice.fretMotion.length : 0.0;
    }
    static float arrivalPressure(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)]
            .fretMotion.arrivalPressure;
    }

    static int openMidi(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].openMidi;
    }
    static int soundingNote(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return voice.played ? voice.midiNote : -1;
    }
    static bool ringing(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return voice.played && voice.level > 2.0e-7f;
    }
    static int reshape(AcustraEngine& engine, int midiNote, int channel,
                       int chosen)
    {
        return engine.reshapeFormingChord(midiNote, channel, chosen);
    }
    static int pluckDelay(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].pluckDelay;
    }
    static bool keyDown(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].keyDown;
    }
    // This generator advances only when initialisePluck releases an attack;
    // the continuing excitation noise has its own generator. Its change
    // therefore detects an extra attack without adding a production counter.
    static std::array<std::uint32_t, AcustraEngine::stringCount> attackStates(
        const AcustraEngine& engine)
    {
        std::array<std::uint32_t, AcustraEngine::stringCount> states {};
        for (std::size_t string = 0; string < states.size(); ++string)
            states[string] = engine.voices_[string].randomState;
        return states;
    }
    static int owners(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].ownerCount;
    }
    static bool pedalHeld(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].pedalHeld;
    }
};
} // namespace acustra

namespace
{
using acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void run(AcustraEngine& engine, double seconds)
{
    std::array<float, blockSize> left {}, right {};
    int remaining = static_cast<int>(std::lround(seconds * sampleRate));
    while (remaining > 0)
    {
        const int count = std::min(remaining, blockSize);
        engine.process(left.data(), right.data(), count);
        remaining -= count;
    }
}

struct Placement
{
    int string { -1 };
    int fret { -1 };
};

Placement placed(const AcustraEngine& engine, int midiNote, int channel = 1)
{
    const int string = engine.heldString(midiNote, channel);
    if (string < 0)
        return {};
    return { string, midiNote - Access::openMidi(engine, string) };
}

std::string describe(const std::vector<Placement>& shape)
{
    static const char* names[] = { "E", "A", "D", "G", "B", "e" };
    std::string text;
    for (const auto& note : shape)
        text += (note.string < 0 ? std::string("?") : names[note.string])
            + std::to_string(note.fret) + " ";
    return text;
}

// One note per string, and the fretted notes within a hand's reach: the
// highest fret at most `span` above the lowest.
bool oneHand(const std::vector<Placement>& shape, int span)
{
    unsigned used = 0u;
    int low = 99, high = -1;
    for (const auto& note : shape)
    {
        if (note.string < 0 || ((used >> note.string) & 1u) != 0u)
            return false;
        used |= 1u << note.string;
        if (note.fret >= 1)
        {
            low = std::min(low, note.fret);
            high = std::max(high, note.fret);
        }
    }
    return high < 0 || high - low <= span;
}

std::unique_ptr<AcustraEngine> freshEngine()
{
    auto engine = std::make_unique<AcustraEngine>();
    engine->prepare(sampleRate, blockSize);
    return engine;
}

std::vector<Placement> shapeOf(const AcustraEngine& engine,
                               const std::vector<int>& notes, int channel = 1)
{
    std::vector<Placement> shape;
    for (const int note : notes)
        shape.push_back(placed(engine, note, channel));
    return shape;
}

bool sameShape(const std::vector<Placement>& shape,
               const std::vector<std::array<int, 2>>& expected)
{
    if (shape.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < shape.size(); ++index)
        if (shape[index].string != expected[index][0]
            || shape[index].fret != expected[index][1])
            return false;
    return true;
}

// The allocator of 2026-10-02 to 1.9.67, kept for comparisons: a note that
// has sounded keeps its string while a chord forms.
std::unique_ptr<AcustraEngine> keepsAttacksEngine()
{
    auto engine = std::make_unique<AcustraEngine>();
    acustra::PerformanceRealism realism {};
    realism.formingChordMovesSounded = false;
    engine->setPerformanceRealism(realism);
    engine->prepare(sampleRate, blockSize);
    return engine;
}

void testRolledTriadIsOneHandShape()
{
    // With no look-ahead C4 claims B1 and E4 the open high E before G4
    // arrives, and then only the G string's twelfth fret is free. The chord
    // still forming is refretted as one shape: the notes that must move are
    // plucked again where the shape puts them, and the strings they leave
    // are let go (the owner's choice, Docs/decisions.md, 2026-11-26).
    for (const auto& triad : { std::vector<int> { 60, 64, 67 },
                               std::vector<int> { 62, 66, 69 },
                               std::vector<int> { 64, 67, 71 } })
    {
        auto engine = freshEngine();
        std::vector<int> played;
        for (const int note : triad)
        {
            const auto before = shapeOf(*engine, played);
            const auto attacks = Access::attackStates(*engine);
            engine->noteOn(note, 0.8f);
            const auto after = shapeOf(*engine, played);
            const auto nextAttacks = Access::attackStates(*engine);
            for (std::size_t index = 0; index < played.size(); ++index)
            {
                expect(after[index].string >= 0, "a rolled key cost an earlier note its string");
                if (after[index].string == before[index].string || after[index].string < 0)
                    continue;
                // A moved note sounds again on its new string, and its key
                // no longer holds the one it left.
                const auto to = static_cast<std::size_t>(after[index].string);
                expect(attacks[to] != nextAttacks[to],
                       "a note moved by a later rolled key was not plucked on its new string");
                expect(!Access::keyDown(*engine, before[index].string)
                           || Access::soundingNote(*engine, before[index].string) != played[index],
                       "a moved note still holds the string it left");
            }
            played.push_back(note);
            run(*engine, 0.010);
        }
        const auto shape = shapeOf(*engine, triad);
        std::cout << "Acustra rolled triad " << triad[0] << "-" << triad[1]
                  << "-" << triad[2] << " over 20 ms: " << describe(shape) << '\n';
        expect(oneHand(shape, 4), "a rolled triad was not fretted in one hand: " + describe(shape));
    }

    // The former rule, still reachable: each rolled key is one attack and
    // nothing that has sounded moves, whatever fingering that leaves.
    for (const auto& triad : { std::vector<int> { 60, 64, 67 },
                               std::vector<int> { 62, 66, 69 },
                               std::vector<int> { 64, 67, 71 } })
    {
        auto engine = keepsAttacksEngine();
        std::vector<int> played;
        for (const int note : triad)
        {
            const auto before = shapeOf(*engine, played);
            const auto attacks = Access::attackStates(*engine);
            engine->noteOn(note, 0.8f);
            const auto after = shapeOf(*engine, played);
            const auto nextAttacks = Access::attackStates(*engine);
            int fired = 0;
            for (std::size_t string = 0; string < attacks.size(); ++string)
                fired += attacks[string] != nextAttacks[string] ? 1 : 0;
            expect(fired == 1, "with attacks kept, one rolled key created "
                       + std::to_string(fired) + " attacks instead of one");
            for (std::size_t index = 0; index < played.size(); ++index)
                expect(after[index].string == before[index].string,
                       "with attacks kept, a later rolled key moved an already sounded note");
            played.push_back(note);
            run(*engine, 0.010);
        }
        const auto shape = shapeOf(*engine, triad);
        expect(std::all_of(shape.begin(), shape.end(), [](const Placement& note)
                   { return note.string >= 0; }),
               "preserving a rolled attack dropped a triad note");
    }

    // Spread wider than the chord window the notes are melody, not a chord,
    // and nothing already sounding moves.
    auto apart = freshEngine();
    apart->noteOn(60, 0.8f);
    run(*apart, 0.2);
    apart->noteOn(64, 0.8f);
    run(*apart, 0.2);
    const auto before = shapeOf(*apart, { 60, 64 });
    apart->noteOn(67, 0.8f);
    const auto after = shapeOf(*apart, { 60, 64 });
    expect(before[0].string == after[0].string
               && before[1].string == after[1].string,
           "notes 200 ms apart were refretted as a chord");
}

// A strum member let go before the pick reaches it is still plucked, and let
// go then (audit F2). When a note joining the forming chord refrets it onto
// another string, its key-up goes with it - also when its pick was due on the
// very sample the refret lands on, where the move plucks it at once. That one
// used to keep its key down with no owner: C4 rang on unreleased.
void testRefrettedStrumMemberLetGoIsReleased()
{
    for (const int lead : { 3, 2, 1 })
    {
        auto engine = freshEngine();
        // E4 then C4, strummed; C4's key comes up before its pick.
        engine->beginStrum();
        engine->noteOn(64, 0.8f, 1, 150, true);
        engine->noteOn(60, 0.8f, 1, 300, true);
        const int string = engine->heldString(60, 1);
        engine->noteOff(60);
        expect(engine->heldString(60, 1) < 0 && string >= 0
                   && !Access::keyDown(*engine, string) && Access::owners(*engine, string) == 0,
               "an early strum key-up kept its pending attack's key ownership");
        std::array<float, 1> left {}, right {};
        for (int guard = 0; string >= 0 && guard < 2000
                            && Access::pluckDelay(*engine, string) != lead; ++guard)
            engine->process(left.data(), right.data(), 1);
        expect(string >= 0 && Access::pluckDelay(*engine, string) == lead,
               "the let-go strum member's pick was not pending");
        // G4 joins the chord: the rolled C-E-G refrets C4 onto the G string.
        engine->noteOn(67, 0.8f);
        int moved = -1;
        for (int s = 0; s < AcustraEngine::stringCount; ++s)
            if (Access::soundingNote(*engine, s) == 60)
                moved = s;
        run(*engine, 1.0);
        bool released = true;
        for (int s = 0; s < AcustraEngine::stringCount; ++s)
            if (Access::soundingNote(*engine, s) == 60 && Access::keyDown(*engine, s))
                released = false;
        std::cout << "Acustra let-go strum member refretted " << lead
                  << " sample(s) before its pick: string " << string << " -> "
                  << moved << ", " << (released ? "released" : "still held") << '\n';
        expect(moved >= 0 && moved != string,
               "the rolled chord did not refret the let-go C4");
        expect(released, "a let-go strum member refretted as its pick fell due "
                         "kept its key down: lead " + std::to_string(lead));
    }
}

// A pending attack moved by a forming chord keeps its due sample and its
// owners, and a sounded note waiting for an explicit repeat keeps its string.
void testPendingAttacksKeepTheirTiming()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int frames : { 1, 64, 4096 })
        {
            const auto advance = [frames](AcustraEngine& engine, int samples)
            {
                std::vector<float> left(static_cast<std::size_t>(frames));
                std::vector<float> right(left.size());
                while (samples > 0)
                {
                    const int count = std::min(samples, frames);
                    engine.process(left.data(), right.data(), count);
                    samples -= count;
                }
            };
            auto engine = std::make_unique<AcustraEngine>();
            engine->prepare(rate, frames);
            // The two first keys have not sounded. A duplicate owns the
            // same pending release; it does not imply a preceding attack.
            engine->noteOn(64, 0.8f, 1, 1500, true);
            engine->noteOn(60, 0.8f, 1, 2500, true);
            engine->noteOn(60, 0.8f, 1, 2500, true);
            advance(*engine, 100);
            const int from = engine->heldString(60);
            const int delay = Access::pluckDelay(*engine, from);
            const auto before = Access::attackStates(*engine);
            engine->noteOn(67, 0.8f, 1, 2000, true);
            const int to = engine->heldString(60);
            expect(to >= 0 && to != from,
                   "the unfired doubled C4 was not moved into the pending chord shape");
            expect(Access::attackStates(*engine) == before,
                   "planning a pending shape released an attack before its pick");
            expect(to >= 0 && Access::pluckDelay(*engine, to) == delay,
                   "moving an unfired attack changed its remaining delay");
            expect(to >= 0 && Access::owners(*engine, to) == 2,
                   "moving an unfired attack dropped its duplicate owner");
            if (to >= 0)
            {
                advance(*engine, delay - 1);
                const auto waiting = Access::attackStates(*engine);
                expect(waiting[static_cast<std::size_t>(to)]
                           == before[static_cast<std::size_t>(to)],
                       "the moved attack fired before its original due sample");
                advance(*engine, 1);
                expect(Access::attackStates(*engine)[static_cast<std::size_t>(to)]
                           != waiting[static_cast<std::size_t>(to)],
                       "the moved attack missed its original due sample");
                engine->noteOff(60);
                expect(Access::keyDown(*engine, to) && Access::owners(*engine, to) == 1,
                       "the first key-up released both pending owners");
                engine->noteOff(60);
                expect(!Access::keyDown(*engine, to),
                       "the final key-up did not release the moved attack");
            }

            auto sounded = std::make_unique<AcustraEngine>();
            sounded->prepare(rate, frames);
            sounded->noteOn(60, 0.8f);
            advance(*sounded, static_cast<int>(0.010 * rate));
            sounded->noteOn(64, 0.8f);
            advance(*sounded, static_cast<int>(0.005 * rate));
            const int committed = sounded->heldString(60);
            sounded->noteOn(60, 0.8f, 1, 2500, true);
            const int repeatedDelay = Access::pluckDelay(*sounded, committed);
            const auto prior = Access::attackStates(*sounded);
            sounded->noteOn(67, 0.8f);
            const auto next = Access::attackStates(*sounded);
            expect(sounded->heldString(60) == committed
                       && Access::pluckDelay(*sounded, committed) == repeatedDelay,
                   "a pending explicit repeat moved its already sounded string");
            expect(next[static_cast<std::size_t>(committed)]
                       == prior[static_cast<std::size_t>(committed)]
                       && Access::owners(*sounded, committed) == 2,
                   "joining a live chord restarted or lost a pending explicit repeat");
        }
}

void playOneKeyAtATime(AcustraEngine& engine, const std::vector<int>& notes,
                       double gap)
{
    for (const int note : notes)
    {
        engine.noteOn(note, 0.7f);
        run(engine, gap);
    }
}

void release(AcustraEngine& engine, const std::vector<int>& notes)
{
    for (const int note : notes)
        engine.noteOff(note);
}

void testOpenChordsOneKeyAtATime()
{
    // E major, A minor, G, C, each played low to high a key at a time (80 ms
    // apart, arpeggiated rather than rolled), held, then released 0.3 s
    // before the next. Every one lands in its first-position open shape.
    // String then fret, low E string first.
    auto engine = freshEngine();
    const std::vector<std::pair<std::vector<int>, std::vector<std::array<int, 2>>>>
        chords {
            { { 40, 47, 52, 56, 59, 64 },
              { { { 0, 0 } }, { { 1, 2 } }, { { 2, 2 } }, { { 3, 1 } },
                { { 4, 0 } }, { { 5, 0 } } } },
            { { 45, 52, 57, 60, 64 },
              { { { 1, 0 } }, { { 2, 2 } }, { { 3, 2 } }, { { 4, 1 } },
                { { 5, 0 } } } },
            { { 43, 47, 50, 55, 59, 67 },
              { { { 0, 3 } }, { { 1, 2 } }, { { 2, 0 } }, { { 3, 0 } },
                { { 4, 0 } }, { { 5, 3 } } } },
            { { 48, 52, 55, 60, 64 },
              { { { 1, 3 } }, { { 2, 2 } }, { { 3, 0 } }, { { 4, 1 } },
                { { 5, 0 } } } },
        };
    const char* names[] = { "E major", "A minor", "G major", "C major" };
    for (std::size_t index = 0; index < chords.size(); ++index)
    {
        playOneKeyAtATime(*engine, chords[index].first, 0.08);
        const auto shape = shapeOf(*engine, chords[index].first);
        std::cout << "Acustra " << names[index] << " one key at a time: "
                  << describe(shape) << '\n';
        expect(sameShape(shape, chords[index].second),
               std::string(names[index]) + " one key at a time was not its open shape: "
                   + describe(shape));
        run(*engine, 0.5);
        release(*engine, chords[index].first);
        run(*engine, 0.3);
    }
}

void testScaleRunStaysInPositionThenShifts()
{
    // C major up two octaves from C3, each note released as the next is
    // played, 150 ms apart.
    auto engine = freshEngine();
    const std::vector<int> scale { 48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65,
                                   67, 69, 71, 72 };
    std::vector<Placement> run_;
    for (const int note : scale)
    {
        engine->noteOn(note, 0.7f);
        run_.push_back(placed(*engine, note));
        run(*engine, 0.12);
        engine->noteOff(note);
        run(*engine, 0.03);
    }
    std::cout << "Acustra C major scale run: " << describe(run_) << '\n';
    // Up to G4 the whole run is played in one position.
    int low = 99, high = -1;
    for (std::size_t index = 0; index < 12; ++index)
        if (run_[index].fret >= 1)
        {
            low = std::min(low, run_[index].fret);
            high = std::max(high, run_[index].fret);
        }
    expect(high - low <= 3,
           "the scale's first octave and a half left the four-fret position: "
               + describe(run_));
    // Then the hand shifts up the neck, a few frets at a time.
    expect(run_.back().fret > high,
           "the scale's top never shifted position: " + describe(run_));
    for (std::size_t index = 12; index < run_.size(); ++index)
        expect(run_[index].fret >= 1 && run_[index].fret - run_[index - 1].fret <= 3
                   && run_[index].fret - run_[index - 1].fret >= -3,
               "the scale's shift jumped: " + describe(run_));
}

void testMelodyOverHeldBassKeepsTheBass()
{
    // A2 held on the open A; C3, D3, E3, C3 over it. C3 is the A string's
    // third fret, but that string holds a key that is down, so the melody
    // goes elsewhere and the bass rings on.
    auto engine = freshEngine();
    engine->noteOn(45, 0.8f);
    run(*engine, 0.1);
    const int bass = engine->heldString(45);
    expect(bass == 1, "A2 was not placed on the open A string");
    for (const int note : { 48, 50, 52, 48 })
    {
        engine->noteOn(note, 0.7f);
        const auto melody = placed(*engine, note);
        expect(melody.string >= 0 && melody.string != bass,
               "a melody note " + std::to_string(note) + " took the held bass string");
        expect(engine->heldString(45) == bass,
               "the held bass stopped sounding under the melody");
        run(*engine, 0.2);
        engine->noteOff(note);
        run(*engine, 0.05);
    }
}

// A line played as a keyboard player plays legato: each key goes down
// `overlap` seconds before the last one comes up. Returns each note's
// placement as it is struck.
std::vector<Placement> playOverlapped(AcustraEngine& engine, const std::vector<int>& line,
                                      double step, double overlap)
{
    std::vector<Placement> placements;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        engine.noteOn(line[index], 0.7f);
        placements.push_back(placed(engine, line[index]));
        run(engine, overlap);
        if (index > 0)
            engine.noteOff(line[index - 1]);
        run(engine, step - overlap);
    }
    engine.noteOff(line.back());
    return placements;
}

void testLegatoLineKeepsItsString()
{
    // C major up two octaves from C3 and back, an eighth note at 120 BPM
    // apart, every key held 20 ms into the next. Detached, the same scale
    // stays under the eighth fret (testScaleRunStaysInPositionThenShifts).
    std::vector<int> scale;
    const int major[] { 0, 2, 4, 5, 7, 9, 11 };
    for (int degree = 0; degree < 15; ++degree)
        scale.push_back(48 + 12 * (degree / 7) + major[degree % 7]);
    for (int degree = 13; degree >= 0; --degree)
        scale.push_back(48 + 12 * (degree / 7) + major[degree % 7]);
    const auto widestMove = [](const std::vector<Placement>& line)
    {
        int widest = 0, previous = -1;
        for (const auto& note : line)
        {
            if (note.fret >= 1 && previous >= 1)
                widest = std::max(widest, std::abs(note.fret - previous));
            if (note.fret >= 1)
                previous = note.fret;
        }
        return widest;
    };
    const auto highestFret = [](const std::vector<Placement>& line)
    {
        int highest = 0;
        for (const auto& note : line)
            highest = std::max(highest, note.fret);
        return highest;
    };

    auto engine = freshEngine();
    const auto legato = playOverlapped(*engine, scale, 0.25, 0.02);
    std::cout << "Acustra C major scale, legato: " << describe(legato) << '\n';
    for (const auto& note : legato)
        expect(note.string >= 0, "a legato scale note found no string");
    expect(highestFret(legato) <= 8,
           "the legato scale left the low positions: " + describe(legato));
    expect(widestMove(legato) <= 4,
           "the legato scale's hand jumped more than four frets: " + describe(legato));

    // What the rule replaced, kept reachable for comparisons: every
    // overlapped note on another string, up to the twentieth fret.
    auto former = freshEngine();
    acustra::PerformanceRealism realism {};
    realism.legatoLines = false;
    former->setPerformanceRealism(realism);
    const auto climbed = playOverlapped(*former, scale, 0.25, 0.02);
    std::cout << "Acustra C major scale, legato, rule off: " << describe(climbed) << '\n';
    expect(highestFret(climbed) >= 17,
           "the former allocator no longer shows the climb the rule answers: "
               + describe(climbed));

    // Thirds within the line stay on its string too: E4 G4 A4 B4 D5 E5 and
    // back. The first overlap has shown nothing yet and goes across as
    // before; from the second note on nothing passes the twelfth fret.
    auto lick = freshEngine();
    const auto pentatonic = playOverlapped(*lick,
        { 64, 67, 69, 71, 74, 76, 74, 71, 69, 67, 64 }, 0.125, 0.02);
    std::cout << "Acustra pentatonic line, legato: " << describe(pentatonic) << '\n';
    expect(highestFret(pentatonic) <= 12,
           "the legato pentatonic line went up the body: " + describe(pentatonic));
    expect(pentatonic[4].string == pentatonic[3].string,
           "B4 to D5, a third inside a legato line, left the line's string: "
               + describe(pentatonic));
}

void testNotesLeftToRingKeepTheirStrings()
{
    // The same notes held on, not let go: nothing has shown a line, so no
    // held note gives up its string, however far the next one must go.
    {
        // D major from D3, every key held: F#3 must not take the D string.
        auto engine = freshEngine();
        for (const int note : { 50, 54, 57, 62, 66 })
        {
            engine->noteOn(note, 0.7f);
            run(*engine, 0.25);
        }
        for (const int note : { 50, 54, 57, 62, 66 })
            expect(engine->heldString(note) >= 0,
                   "a held arpeggio lost note " + std::to_string(note));
    }
    {
        // Seconds left to ring across strings, each key held one second into
        // the next two, as the DADGAD demo's melody is: D4 E4 F#4.
        auto engine = freshEngine();
        engine->noteOn(62, 0.7f);
        run(*engine, 0.3);
        engine->noteOn(64, 0.7f);
        run(*engine, 0.3);
        engine->noteOn(66, 0.7f);
        run(*engine, 0.3);
        expect(engine->heldString(62) >= 0 && engine->heldString(64) >= 0
                   && engine->heldString(66) >= 0,
               "seconds left to ring did not all keep a string");
        // The first comes up long after the second went down: not an overlap.
        engine->noteOff(62);
        run(*engine, 0.1);
        engine->noteOn(67, 0.7f);
        expect(engine->heldString(64) >= 0 && engine->heldString(66) >= 0,
               "a key let go after ringing was read as a legato overlap");
    }
    {
        // A line over a held bass: the bass is not the line's last note, so
        // the line's legato steps never take its string. A2 on the open A,
        // then B2 C3 D3 E3 overlapped above it.
        auto engine = freshEngine();
        engine->noteOn(45, 0.8f);
        run(*engine, 0.4);
        const int bass = engine->heldString(45);
        playOverlapped(*engine, { 59, 60, 62, 64, 62, 60, 59 }, 0.25, 0.02);
        expect(bass >= 0 && engine->heldString(45) == bass,
               "a legato line took the held bass's string");
    }
    {
        // A chord rolled inside the chord window is a chord, not a line:
        // C4 D4 G4 ten milliseconds apart keeps all three.
        auto engine = freshEngine();
        for (const int note : { 60, 62, 67 })
        {
            engine->noteOn(note, 0.8f);
            run(*engine, 0.010);
        }
        expect(engine->heldString(60) >= 0 && engine->heldString(62) >= 0
                   && engine->heldString(67) >= 0,
               "a rolled sus2 chord lost a note to the legato rule");
    }
}

// How often a chord that one hand can hold ends up in a shape it cannot:
// I-IV-V-I, I-vi-IV-V, I-V-vi-IV, ii-V-I-vi and vi-IV-I-V in twelve keys, a
// left-hand root under the right hand's nearest close triad, each chord held
// half a second and let go, the hand keeping its memory between chords.
// On one sample (a sequenced chord, or a gathered one) none may miss, and
// none played live either, the keys 8 ms apart from the bass up: the first
// key is placed before the rest are known, and the chord still forming is
// refretted as the later keys arrive. With sounded notes kept on their
// strings instead (2026-10-02 to 1.9.67) 63 of the 214 missed.
// Chords as a keyboard player plays them, for the reach tests: the frets of
// a shape and whether shapeCost's rule holds them in one hand (stopped frets
// at most four apart), whether standard tuning has such a shape for a set of
// notes, and five common progressions, each chord a left-hand root under the
// right hand's close triad, voiced nearest the last one.
bool reachable(const std::array<int, 6>& frets)
{
    int low = 99, high = -1;
    for (const int fret : frets)
        if (fret >= 1)
        {
            low = std::min(low, fret);
            high = std::max(high, fret);
        }
    return high < 0 || high - low <= 4;
}

bool someShape(const std::vector<int>& notes)
{
    const std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
    std::array<int, 6> strings { 0, 1, 2, 3, 4, 5 };
    do
    {
        std::array<int, 6> frets;
        frets.fill(-1);
        bool fits = true;
        for (std::size_t index = 0; index < notes.size() && fits; ++index)
        {
            const int fret = notes[index] - open[static_cast<std::size_t>(strings[index])];
            fits = fret >= 0 && fret <= AcustraEngine::fretCount;
            frets[static_cast<std::size_t>(strings[index])] = fret;
        }
        if (fits && reachable(frets))
            return true;
    } while (std::next_permutation(strings.begin(), strings.end()));
    return false;
}

const std::vector<std::vector<std::pair<int, bool>>> keyboardProgressions {
    { { 0, false }, { 5, false }, { 7, false }, { 0, false } },
    { { 0, false }, { 9, true }, { 5, false }, { 7, false } },
    { { 0, false }, { 7, false }, { 9, true }, { 5, false } },
    { { 2, true }, { 7, false }, { 0, false }, { 9, true } },
    { { 9, true }, { 5, false }, { 0, false }, { 7, false } } };

// The chord on `degree` of `key`, bass first; `top` follows the right hand.
std::vector<int> keyboardChord(int key, const std::pair<int, bool>& degree, int& top)
{
    const int pitchClass = (key + degree.first) % 12;
    const std::array<int, 3> tones { 0, degree.second ? 3 : 4, 7 };
    std::vector<int> triad;
    int nearest = 999;
    for (int low = 52; low <= 72; ++low)
        for (int inversion = 0; inversion < 3; ++inversion)
        {
            if (low % 12 != (pitchClass + tones[static_cast<std::size_t>(inversion)]) % 12)
                continue;
            std::vector<int> notes;
            for (int index = 0; index < 3; ++index)
                notes.push_back(low - tones[static_cast<std::size_t>(inversion)]
                    + tones[static_cast<std::size_t>((inversion + index) % 3)]
                    + (inversion + index >= 3 ? 12 : 0));
            if (notes[2] <= 76 && std::abs(notes[2] - top) < nearest)
            {
                nearest = std::abs(notes[2] - top);
                triad = notes;
            }
        }
    top = triad[2];
    int bass = 36 + pitchClass;
    while (bass < 40)
        bass += 12;
    std::vector<int> chord { bass };
    for (const int note : triad)
        if (note != bass)
            chord.push_back(note);
    return chord;
}

// The chord's keys on one sample, planned as the Performer plans them, or
// played live: 8 ms apart from the bass up.
void strike(AcustraEngine& engine, const std::vector<int>& chord, bool live)
{
    if (!live)
        engine.planChord(chord.data(), static_cast<int>(chord.size()));
    for (const int note : chord)
    {
        engine.noteOn(note, 0.7f);
        if (live)
            run(engine, 0.008);
    }
}

// The chord's shape now: -1 where a key has no string.
bool heldInOneHand(const AcustraEngine& engine, const std::vector<int>& chord)
{
    std::array<int, 6> frets;
    frets.fill(-1);
    for (const int note : chord)
    {
        const auto where = placed(engine, note);
        if (where.string < 0)
            return false;
        frets[static_cast<std::size_t>(where.string)] = where.fret;
    }
    return reachable(frets);
}

void testLiveChordReach()
{
    int playable = 0;
    std::array<int, 2> missed {};
    for (int live = 0; live < 2; ++live)
        for (int key = 0; key < 12; ++key)
            for (const auto& progression : keyboardProgressions)
            {
                auto engine = freshEngine();
                int top = 60 + key;
                for (const auto& degree : progression)
                {
                    const auto chord = keyboardChord(key, degree, top);
                    strike(*engine, chord, live == 1);
                    run(*engine, 0.5);
                    if (someShape(chord))
                    {
                        playable += live == 0 ? 1 : 0;
                        missed[static_cast<std::size_t>(live)]
                            += heldInOneHand(*engine, chord) ? 0 : 1;
                    }
                    release(*engine, chord);
                    run(*engine, 0.125);
                }
            }
    std::cout << "Acustra chords with a one-hand shape: " << playable
              << "; missed on one sample " << missed[0]
              << ", played live from the bass up " << missed[1] << '\n';
    expect(missed[0] == 0, "a chord on one sample missed its one-hand shape "
               + std::to_string(missed[0]) + " times");
    expect(missed[1] == 0, "a chord played live missed its one-hand shape "
               + std::to_string(missed[1]) + " times");
}

// The allocator up to 1.9.68, kept for comparisons: a key that is down
// always keeps its string.
std::unique_ptr<AcustraEngine> heldKeysKeepStringsEngine()
{
    auto engine = std::make_unique<AcustraEngine>();
    acustra::PerformanceRealism realism {};
    realism.chordChangeTakesHand = false;
    engine->setPerformanceRealism(realism);
    engine->prepare(sampleRate, blockSize);
    return engine;
}

// A chord change played legato: the new chord's keys go down while the old
// chord's are still held, and those come up 80 ms later. Up to 1.9.68 every
// string the hand was on was then a key that kept its string, and the new
// chord was fretted around the old one, up the neck.
void testChordChangeTakesTheHand()
{
    // The phrase it was found in. A over C# fingerpicked and left held:
    // C#3 on the A string's 4th fret pinched with the open E, then A3 and
    // C#4. Then B2 and F#4 pinched together, the next bar's Bm7. B2 belongs
    // two frets down the string C#3 is on.
    for (const bool takesHand : { true, false })
    {
        auto engine = takesHand ? freshEngine() : heldKeysKeepStringsEngine();
        const std::vector<int> held { 49, 57, 61, 64 };
        const std::vector<int> first { 64, 49 };
        engine->planChord(first.data(), 2);
        for (const int note : { 64, 49, 57, 61 })
        {
            engine->noteOn(note, 0.7f);
            if (note != 64)
                run(*engine, 0.25);
        }
        expect(sameShape(shapeOf(*engine, held), { { { 1, 4 } }, { { 3, 2 } }, { { 4, 2 } }, { { 5, 0 } } }),
               "A over C# was not fingered in second position: " + describe(shapeOf(*engine, held)));
        const std::vector<int> pinch { 66, 47 };
        engine->planChord(pinch.data(), 2);
        for (const int note : pinch)
            engine->noteOn(note, 0.7f);
        const auto shape = shapeOf(*engine, pinch);
        if (takesHand)
        {
            expect(sameShape(shape, { { { 5, 2 } }, { { 1, 2 } } }),
                   "a pinch over the keys still down was not fretted under the hand: " + describe(shape));
            expect(engine->heldString(49) < 0 && engine->heldString(64) < 0,
                   "the notes whose strings the pinch took still hold a string");
            expect(engine->heldString(57) == 3 && engine->heldString(61) == 4,
                   "the notes the pinch left alone lost their strings");
        }
        else
            // The former rule, so the fault stays on record: the two notes
            // on the only strings with no key down, more than a hand apart
            // (F#4 at the D string's 16th fret, B2 at the low E's 7th).
            expect(shape[0].string >= 0 && shape[1].string >= 0 && !oneHand(shape, 4),
                   "with held keys keeping their strings the pinch was not sent up the neck: " + describe(shape));
        run(*engine, 0.08);
        release(*engine, held);
        run(*engine, 0.3);
        expect(describe(shapeOf(*engine, pinch)) == describe(shape),
               "the old keys' key-ups moved or ended the pinch: " + describe(shapeOf(*engine, pinch)));
        release(*engine, pinch);
        run(*engine, 0.3);
        for (int string = 0; string < AcustraEngine::stringCount; ++string)
            expect(Access::owners(*engine, string) == 0,
                   "a key is still counted on string " + std::to_string(string)
                       + " after every key came up");
    }

    // Six keys' progressions, every change overlapped by 80 ms; a key
    // common to both chords is struck again after a 30 ms lift. The former
    // allocator plays two of the keys, enough to keep the fault on record.
    std::array<std::array<int, 2>, 2> missed {};
    std::array<int, 2> playable {};
    for (int former = 0; former < 2; ++former)
        for (int live = 0; live < 2; ++live)
            for (int key = 0; key < 12; key += former == 1 ? 6 : 2)
                for (const auto& progression : keyboardProgressions)
                {
                    auto engine = former == 1 ? heldKeysKeepStringsEngine() : freshEngine();
                    int top = 60 + key;
                    std::vector<int> old;
                    for (const auto& degree : progression)
                    {
                        const auto chord = keyboardChord(key, degree, top);
                        std::vector<int> common, leaving;
                        for (const int note : old)
                            (std::find(chord.begin(), chord.end(), note) != chord.end()
                                ? common : leaving).push_back(note);
                        release(*engine, common);
                        run(*engine, 0.03);
                        strike(*engine, chord, live == 1);
                        run(*engine, 0.08);
                        release(*engine, leaving);
                        run(*engine, 0.12);
                        if (someShape(chord))
                        {
                            playable[static_cast<std::size_t>(former)] += live == 0 ? 1 : 0;
                            missed[static_cast<std::size_t>(former)][static_cast<std::size_t>(live)]
                                += heldInOneHand(*engine, chord) ? 0 : 1;
                        }
                        old = chord;
                    }
                }
    std::cout << "Acustra overlapped chord changes with a one-hand shape: " << playable[0]
              << "; beyond one hand on one sample " << missed[0][0] << ", live " << missed[0][1]
              << "; with held keys keeping their strings " << missed[1][0] << " and "
              << missed[1][1] << " of " << playable[1] << '\n';
    expect(missed[0][0] == 0, "an overlapped chord change on one sample ended beyond one hand "
               + std::to_string(missed[0][0]) + " times");
    // Played live, a key common to both chords that is struck again inside
    // the release join (1/32 note) rejoins its old string before the rest
    // of the chord is known: 5 of 107 as 1.9.69 is, a Known gap.
    expect(missed[0][1] <= 5, "an overlapped chord change played live ended beyond one hand "
               + std::to_string(missed[0][1]) + " times");
    expect(5 * missed[1][0] > playable[1] && 5 * missed[1][1] > playable[1],
           "the former allocator no longer shows the fault this test is for");

    // A chord that fits beside the keys still down is left alone: A2 held
    // on the open A, and an A major triad over it.
    std::array<std::vector<Placement>, 2> beside;
    for (int former = 0; former < 2; ++former)
    {
        auto engine = former == 1 ? heldKeysKeepStringsEngine() : freshEngine();
        engine->noteOn(45, 0.8f);
        run(*engine, 0.4);
        const std::vector<int> triad { 64, 61, 57 };
        engine->planChord(triad.data(), 3);
        for (const int note : triad)
            engine->noteOn(note, 0.7f);
        beside[static_cast<std::size_t>(former)] = shapeOf(*engine, { 45, 57, 61, 64 });
    }
    expect(describe(beside[0]) == describe(beside[1]) && beside[0][0].string == 1,
           "a chord that fits beside a held bass was moved: " + describe(beside[0])
               + "against " + describe(beside[1]));
}

void testRepeatedNotesReplickTheirString()
{
    // A note struck again after its key came up is replucked on the string
    // still ringing it, wherever the hand has been since.
    auto engine = freshEngine();
    for (const int note : { 59, 62, 64, 62, 59, 62, 62, 64, 64 })
    {
        const int previous = [&]
        {
            for (int string = 0; string < AcustraEngine::stringCount; ++string)
                if (Access::soundingNote(*engine, string) == note
                    && Access::ringing(*engine, string))
                    return string;
            return -1;
        }();
        engine->noteOn(note, 0.7f);
        const int now = engine->heldString(note);
        if (previous >= 0)
            expect(now == previous, "repeated " + std::to_string(note)
                       + " moved from string " + std::to_string(previous)
                       + " to " + std::to_string(now));
        run(*engine, 0.1);
        engine->noteOff(note);
        run(*engine, 0.02);
    }
}

// Follow the real key-up and hand landing until its measured level crosses
// the allocator floor. This G3 case needs no injected observer value: the
// damping contact is still present when the old allocator chooses the D string.
void testQuietContactKeepsRepeatedString()
{
    auto engine = freshEngine();
    engine->noteOn(55, 0.7f);
    expect(engine->heldString(55) == 3, "quiet repeat fixture did not use open G");
    run(*engine, 0.2);
    engine->noteOff(55);
    std::array<float, blockSize> left {}, right {};
    for (int block = 0; block < 1500
         && (!Access::contact(*engine, 3) || Access::ringing(*engine, 3)); ++block)
        engine->process(left.data(), right.data(), blockSize);
    expect(Access::soundingNote(*engine, 3) == 55
               && Access::contact(*engine, 3) && !Access::ringing(*engine, 3),
           "G3 did not become quiet before its physical hand-back deadline");
    expect(!Access::keyDown(*engine, 3) && Access::owners(*engine, 3) == 0,
           "a quiet physical contact retained MIDI ownership");
    expect(Access::chooseString(*engine, 55) == 3
               && Access::chooseStringWithoutHand(*engine, 55) == 3,
           "a quiet release contact let the repeated G3 hop to a different string");
    for (const int removedPlane : { 0, 1 })
    {
        auto onePlane = std::make_unique<AcustraEngine>(*engine);
        Access::removeContact(*onePlane, 3, removedPlane);
        expect(Access::chooseString(*onePlane, 55) == 3
                   && Access::chooseStringWithoutHand(*onePlane, 55) == 3,
               "the other plane's release contact lost its repeated-string preference");
    }
    auto noContact = std::make_unique<AcustraEngine>(*engine);
    Access::removeContact(*noContact, 3, 0);
    Access::removeContact(*noContact, 3, 1);
    // Since 1.9.43 an open string leaves the hand in first position, and
    // that hand plays the open G again rather than the D string's fifth
    // fret; the handless allocator is as it was.
    expect(Access::chooseString(*noContact, 55) == 3
               && Access::chooseStringWithoutHand(*noContact, 55) == 2,
           "an inactive contact changed the existing quiet-string allocator");
    auto repeated = std::make_unique<AcustraEngine>(*engine);
    const auto attacks = Access::attackStates(*repeated);
    repeated->noteOn(55, 0.7f);
    expect(repeated->heldString(55) == 3 && Access::owners(*repeated, 3) == 1,
           "the quiet repeat failed to establish its new owner on the same string");
    const auto after = Access::attackStates(*repeated);
    for (int string = 0; string < AcustraEngine::stringCount; ++string)
        expect((attacks[static_cast<std::size_t>(string)]
                    != after[static_cast<std::size_t>(string)]) == (string == 3),
               "the quiet repeat excited an additional or different string");

    // Retaining the preferred string never extends its physical deadline.
    auto expired = std::make_unique<AcustraEngine>(*engine);
    int remaining = Access::returnSamples(*expired, 3);
    expect(remaining > 1, "quiet contact had no future hand-back deadline");
    while (remaining > 1)
    {
        const int count = std::min(blockSize, remaining - 1);
        expired->process(left.data(), right.data(), count);
        remaining -= count;
    }
    expect(Access::contact(*expired, 3), "physical contact ended before its deadline");
    expired->process(left.data(), right.data(), 1);
    expect(!Access::contact(*expired, 3) && Access::soundingNote(*expired, 3) < 0,
           "repeat preference kept the physical string after its hand-back deadline");
    engine->allSoundOff(1);
    expect(!Access::contact(*engine, 3) && Access::soundingNote(*engine, 3) < 0
               && Access::owners(*engine, 3) == 0,
           "panic retained a quiet contact or its MIDI owner");
}

void testQuietContactChordPreference()
{
    auto engine = freshEngine();
    engine->setStringPerChannelMode(true);
    engine->noteOn(60, 0.7f, 4); // C4 on G5, with an ordinary B1 alternative.
    engine->setStringPerChannelMode(false);
    run(*engine, 0.2);
    engine->noteOff(60, 4);
    run(*engine, 0.1);
    expect(Access::contact(*engine, 3), "chord fixture did not land its fretting contact");
    Access::quietObserver(*engine, 3);
    // Equalize only the allocator's posture cost to isolate its repeat
    // preference from the stronger existing hand-reachability preference.
    Access::forgetHand(*engine);
    auto noContact = std::make_unique<AcustraEngine>(*engine);
    Access::removeContact(*noContact, 3, 0);
    Access::removeContact(*noContact, 3, 1);
    const std::array<int, 2> notes { 60, 64 };
    engine->planChord(notes.data(), static_cast<int>(notes.size()));
    noContact->planChord(notes.data(), static_cast<int>(notes.size()));
    expect(engine->plannedString(60) == 3 && noContact->plannedString(60) == 4,
           "the chord planner lost its quiet-contact repeat preference");
    expect(engine->plannedString(64) == 5 && noContact->plannedString(64) == 5,
           "quiet repeat preference displaced the chord's available open E");

    auto disabled = std::make_unique<AcustraEngine>();
    disabled->setPerformanceRealism({ false, false, false, false });
    disabled->prepare(sampleRate, blockSize);
    disabled->noteOn(55, 0.7f);
    run(*disabled, 0.2);
    disabled->noteOff(55);
    run(*disabled, 0.1);
    Access::quietObserver(*disabled, 3);
    // The hand's first position after an open string, as above.
    expect(!Access::contact(*disabled, 3)
               && Access::chooseString(*disabled, 55) == 3
               && Access::chooseStringWithoutHand(*disabled, 55) == 2,
           "all-off realism changed the old quiet-string allocator");
}

void testControllerStringsAreUnchanged()
{
    // String-per-channel: the channel names the string, hand or no hand.
    auto engine = freshEngine();
    engine->setStringPerChannelMode(true);
    engine->noteOn(52, 0.7f, 1);   // low E, fret 12
    run(*engine, 0.02);
    engine->noteOn(57, 0.7f, 3);   // D string, fret 7
    engine->noteOn(69, 0.7f, 6);   // high E, fret 5
    expect(engine->heldString(52, 1) == 0 && engine->heldString(57, 3) == 2
               && engine->heldString(69, 6) == 5,
           "string-per-channel notes left their channels' strings");

    // MPE members: the allocator they always had, rolled triad and all.
    auto mpe = freshEngine();
    mpe->setLowerZoneMemberCount(15);
    mpe->noteOn(60, 0.8f, 2);
    run(*mpe, 0.010);
    mpe->noteOn(64, 0.8f, 3);
    run(*mpe, 0.010);
    mpe->noteOn(67, 0.8f, 4);
    const auto shape = std::vector<Placement> {
        placed(*mpe, 60, 2), placed(*mpe, 64, 3), placed(*mpe, 67, 4) };
    std::cout << "Acustra MPE rolled triad: " << describe(shape) << '\n';
    expect(sameShape(shape, { { { 4, 1 } }, { { 5, 0 } }, { { 3, 12 } } }),
           "MPE member notes were not allocated as before: " + describe(shape));
}

void testChordsOnOneSampleAreOneShape()
{
    struct Case
    {
        const char* name;
        std::vector<int> notes;
        std::vector<std::array<int, 2>> expected;
    };
    // Sorted high to low, as the plug-in sends them.
    const std::vector<Case> cases {
        { "C4-E4-G4", { 67, 64, 60 }, {} },
        { "E major", { 64, 59, 56, 52, 47, 40 },
          { { { 5, 0 } }, { { 4, 0 } }, { { 3, 1 } }, { { 2, 2 } },
            { { 1, 2 } }, { { 0, 0 } } } },
        { "C major", { 64, 60, 55, 52, 48 },
          { { { 5, 0 } }, { { 4, 1 } }, { { 3, 0 } }, { { 2, 2 } },
            { { 1, 3 } } } },
        { "D major", { 66, 62, 57, 50 },
          { { { 5, 2 } }, { { 4, 3 } }, { { 3, 2 } }, { { 2, 0 } } } },
        // These ungathered downstrokes can use up all three strings the
        // final high note reaches. A complete plan keeps every note.
        { "B2-G3-C4-E4-B4", { 71, 64, 60, 55, 47 }, {} },
        { "B2-G3-B3-G4-B4", { 71, 67, 59, 55, 47 }, {} },
        { "B2-G3-C4-G4-C5", { 72, 67, 60, 55, 47 }, {} },
    };
    for (const auto& chord : cases)
    {
        auto engine = freshEngine();
        engine->planChord(chord.notes.data(), static_cast<int>(chord.notes.size()));
        engine->beginStrum();
        for (std::size_t index = 0; index < chord.notes.size(); ++index)
            engine->noteOn(chord.notes[index], 0.8f, 1,
                           engine->strumDelaySamples(
                               static_cast<int>(chord.notes.size() - 1 - index), 0.8f),
                           true);
        const auto shape = shapeOf(*engine, chord.notes);
        std::cout << "Acustra planned " << chord.name << ": " << describe(shape) << '\n';
        expect(oneHand(shape, 4), std::string("planned ") + chord.name
                   + " is not one hand's shape: " + describe(shape));
        if (!chord.expected.empty())
            expect(sameShape(shape, chord.expected), std::string("planned ")
                       + chord.name + " is not its open shape: " + describe(shape));
    }

    // Without a plan (a host that calls noteOn alone) the one-sample chord
    // still comes out in one hand, one key at a time.
    auto unplanned = freshEngine();
    for (const int note : { 67, 64, 60 })
        unplanned->noteOn(note, 0.8f);
    const auto shape = shapeOf(*unplanned, { 67, 64, 60 });
    expect(oneHand(shape, 2), "an unplanned one-sample triad is not within two frets: "
               + describe(shape));

    // A chord that follows another is shaped from where the hand is: the
    // same C major struck again after its keys came up is the same shape.
    auto again = freshEngine();
    const std::vector<int> cMajor { 64, 60, 55, 52, 48 };
    again->planChord(cMajor.data(), 5);
    for (const int note : cMajor)
        again->noteOn(note, 0.8f);
    const auto first = shapeOf(*again, cMajor);
    run(*again, 0.3);
    release(*again, cMajor);
    run(*again, 0.1);
    again->planChord(cMajor.data(), 5);
    for (const int note : cMajor)
        again->noteOn(note, 0.8f);
    const auto second = shapeOf(*again, cMajor);
    bool same = true;
    for (std::size_t index = 0; index < first.size(); ++index)
        same &= first[index].string == second[index].string;
    expect(same, "a repeated chord changed shape: " + describe(first) + "-> "
                     + describe(second));
}

// An ungathered physical stroke reaches the engine without its future
// pitches. Commit each attack as it sounds; a later key can choose another
// string but cannot regenerate earlier attacks to improve the final shape.
// Report the fingering compromise rather than requiring an extra pluck to
// make every arrival order end in the same one-hand shape.
// Eleven chords strummed by hand, the keys 5 to 20 ms apart, down and up,
// after an open G has left the hand in first position (88 strums).
// movesSounded is the shipping rule: every strum ends as a one-hand shape
// with every key sounding. Off is the rule of 2026-10-02 to 1.9.67: each
// key is one attack and no sounded note leaves its string unless no free
// string can fret the incoming one, and the count beyond one hand is printed.
void handTimedStrums(bool movesSounded)
{
    const std::vector<std::vector<int>> chords {
        { 40, 47, 52, 56, 59, 64 }, { 45, 52, 57, 60, 64 },
        { 43, 47, 50, 55, 59, 67 }, { 48, 52, 55, 60, 64 },
        { 50, 57, 62, 66 }, { 40, 47, 52, 55, 59, 64 },
        { 45, 52, 57, 61, 64 }, { 47, 55, 60, 64, 71 },
        { 45, 48, 55, 62, 67 }, { 47, 55, 59, 67, 71 },
        { 47, 55, 60, 67, 72 } };
    const std::vector<int> openG { 43, 47, 50, 55, 59, 67 };
    int strums = 0, impossible = 0, takenNotes = 0, forcedTakes = 0;
    for (const auto& chord : chords)
        for (const double gap : { 0.005, 0.010, 0.015, 0.020 })
            for (const bool upstroke : { false, true })
            {
                auto engine = movesSounded ? freshEngine() : keepsAttacksEngine();
                for (const int note : openG)
                    engine->noteOn(note, 0.8f);
                run(*engine, 0.4);
                release(*engine, openG);
                run(*engine, 0.1);
                auto order = chord;
                if (upstroke)
                    std::reverse(order.begin(), order.end());
                for (const int note : order)
                {
                    const auto before = Access::attackStates(*engine);
                    std::array<int, AcustraEngine::stringCount> committed {};
                    bool available = false;
                    for (int string = 0; string < AcustraEngine::stringCount; ++string)
                    {
                        committed[static_cast<std::size_t>(string)]
                            = Access::keyDown(*engine, string)
                                ? Access::soundingNote(*engine, string) : -1;
                        const int fret = note - Access::openMidi(*engine, string);
                        available |= committed[static_cast<std::size_t>(string)] < 0
                            && fret >= 0 && fret <= AcustraEngine::fretCount;
                    }
                    engine->noteOn(note, 0.7f);
                    const auto after = Access::attackStates(*engine);
                    int fired = 0;
                    for (std::size_t string = 0; string < before.size(); ++string)
                        fired += before[string] != after[string] ? 1 : 0;
                    if (!movesSounded)
                        expect(fired == 1, "with attacks kept, one hand-timed key regenerated earlier attacks");
                    for (int string = 0; string < AcustraEngine::stringCount; ++string)
                    {
                        const int earlier = committed[static_cast<std::size_t>(string)];
                        if (earlier < 0 || engine->heldString(earlier) == string)
                            continue;
                        if (movesSounded)
                        {
                            // A note that left its string is sounding on another.
                            expect(engine->heldString(earlier) >= 0,
                                   "a hand-timed key cost an earlier note its string");
                            continue;
                        }
                        expect(!available,
                               "an incoming key took a sounded note while a valid free string served");
                        expect(engine->heldString(note) == string,
                               "an incoming key removed a sounded note from a different string");
                        ++forcedTakes;
                    }
                    run(*engine, gap);
                }
                const auto shape = shapeOf(*engine, chord);
                ++strums;
                takenNotes += std::any_of(shape.begin(), shape.end(),
                    [](const Placement& note) { return note.string < 0; }) ? 1 : 0;
                if (!oneHand(shape, 4))
                {
                    ++impossible;
                    std::cout << "Acustra hand-timed " << (upstroke ? "up" : "down")
                              << "stroke " << gap * 1000.0 << " ms apart"
                              << (movesSounded ? ": " : ", attacks kept: ")
                              << describe(shape) << '\n';
                }
            }
    std::cout << "Acustra hand-timed strums" << (movesSounded ? "" : ", attacks kept,")
              << " outside one hand: " << impossible
              << " of " << strums << "; incoming notes took an occupied string in "
              << takenNotes << " of " << strums << " (" << forcedTakes
              << " takes with no valid free string)\n";
    if (movesSounded)
        expect(impossible == 0 && takenNotes == 0,
               "a hand-timed strum ended beyond one hand or lost a key: "
                   + std::to_string(impossible) + " shapes, " + std::to_string(takenNotes)
                   + " lost");
}

// A pitch struck by two owners (MIDI and CV in the Rack, or a doubled Note On)
// is one key, not a chord: a bass held alone keeps its string under the next
// chord however many messages struck it (takeHandForChord takes the hand
// only from a run of two or more keys).
void testDoubledKeyIsOneKey()
{
    std::array<std::string, 2> shapes;
    for (const int strikes : { 1, 2 })
    {
        auto engine = freshEngine();
        for (int strike = 0; strike < strikes; ++strike)
            engine->noteOn(40, 0.7f);
        run(*engine, 0.3);
        const std::vector<int> chord { 46, 70 };
        engine->planChord(chord.data(), 2);
        for (const int note : chord)
            engine->noteOn(note, 0.7f);
        expect(engine->heldString(40) == 0,
               "a bass held alone lost its string to the next chord after "
                   + std::to_string(strikes) + " Note On(s)");
        shapes[static_cast<std::size_t>(strikes - 1)] = describe(shapeOf(*engine, chord));
    }
    expect(shapes[0] == shapes[1],
           "a doubled Note On changed where the next chord is fretted: " + shapes[0] + " against " + shapes[1]);
}

void testHandTimedStrums()
{
    handTimedStrums(true);
    handTimedStrums(false);
}

void testForgottenHandIsTheHandlessAllocator()
{
    // With no hand on the neck a lone note is placed exactly as before the
    // hand existed: in a fresh engine for every note, and again once two
    // seconds have passed with nothing fretted.
    for (int note = 36; note <= 100; ++note)
    {
        auto engine = freshEngine();
        expect(Access::chooseString(*engine, note)
                   == Access::chooseStringWithoutHand(*engine, note),
               "a fresh engine placed " + std::to_string(note)
                   + " differently from the handless allocator");
    }
    // Up the neck (the first string's eighth to twelfth frets): a hand in
    // first position, where 1.9.43 leaves one that played D4-F4-A4, places
    // every note where the handless allocator does.
    auto engine = freshEngine();
    for (const int note : { 72, 74, 76 })
    {
        engine->noteOn(note, 0.7f);
        run(*engine, 0.1);
        engine->noteOff(note);
    }
    // A released fret still places the hand for a moment...
    run(*engine, 0.5);
    bool differs = false;
    for (int note = 45; note <= 76; ++note)
        differs |= Access::chooseString(*engine, note)
            != Access::chooseStringWithoutHand(*engine, note);
    expect(differs, "a hand released half a second ago changed no placement");
    // ...and after two seconds of nothing fretted it is forgotten.
    run(*engine, 1.8);
    for (int note = 36; note <= 100; ++note)
        expect(Access::chooseString(*engine, note)
                   == Access::chooseStringWithoutHand(*engine, note),
               "a hand two seconds gone still placed " + std::to_string(note));
}

std::vector<Placement> playLine(AcustraEngine& engine, const std::vector<int>& notes,
                                double held = 0.12, double gap = 0.03)
{
    std::vector<Placement> line;
    for (const int note : notes)
    {
        engine.noteOn(note, 0.7f);
        line.push_back(placed(engine, note));
        run(engine, held);
        engine.noteOff(note);
        run(engine, gap);
    }
    return line;
}

// The hand has a position of its own (1.9.43), beside each string's last
// finger: an open string does not lift the hand off the neck, a line does
// not drag it a fret at a time, and it comes down a scale in position.
void testHandKeepsItsPosition()
{
    // E4 on the open first string, then F4: the first fret beside it, where
    // a hand playing open strings is. It was the B string's sixth.
    auto open = freshEngine();
    auto line = playLine(*open, { 64, 65, 67 });
    std::cout << "Acustra open E4, F4, G4: " << describe(line) << '\n';
    expect(sameShape(line, { { { 5, 0 } }, { { 5, 1 } }, { { 5, 3 } } }),
           "a hand that played an open string left first position: " + describe(line));

    // A chromatic octave up from F3 is played four frets to a string, as in
    // position, with no fret beyond the fourth. Before the hand had a
    // position each string remembered one finger and the run crept up it.
    auto chromatic = freshEngine();
    line = playLine(*chromatic, { 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65 });
    std::cout << "Acustra chromatic octave from F3: " << describe(line) << '\n';
    expect(std::all_of(line.begin(), line.end(), [](const Placement& note)
               { return note.string >= 0 && note.fret <= 4; }),
           "a chromatic octave left first position: " + describe(line));

    // C major down from C5 after climbing the first string to it: the hand
    // stays where the climb left it (frets 4-8) and takes the open strings
    // it passes, rather than walking back down to first position.
    auto scale = freshEngine();
    playLine(*scale, { 64, 65, 67, 69, 71 });
    line = playLine(*scale, { 72, 71, 69, 67, 65, 64, 62, 60, 59, 57, 55 });
    std::cout << "Acustra C major down from C5: " << describe(line) << '\n';
    int low = 99, high = -1;
    for (const auto& note : line)
        if (note.fret >= 1)
        {
            low = std::min(low, note.fret);
            high = std::max(high, note.fret);
        }
    expect(high - low <= AcustraEngine::fretCount && high - low <= 5,
           "the descending scale left the hand's position: " + describe(line));
    expect(std::count_if(line.begin(), line.end(), [](const Placement& note)
               { return note.fret == 0; }) >= 3,
           "the descending scale passed its open strings by: " + describe(line));

    // The same descent after two seconds' rest starts from no hand at all.
    auto rested = freshEngine();
    playLine(*rested, { 64, 65, 67, 69, 71, 72 });
    run(*rested, 2.1);
    expect(Access::chooseString(*rested, 62)
               == Access::chooseStringWithoutHand(*rested, 62),
           "a hand two seconds gone kept its position");
}

// Frets up the body (handBodyFret) are taken only by a hand already there.
void testHandComesBackDownTheBody()
{
    // Up the first string to C6 and back: E5 returns to its twelfth fret,
    // D5 to its tenth, not to the B and G strings' seventeenth and
    // nineteenth, where the hand's position alone would have kept them.
    auto engine = freshEngine();
    const auto line = playLine(*engine, { 76, 79, 81, 83, 84, 83, 81, 79, 76, 74 });
    std::cout << "Acustra first string to C6 and back: " << describe(line) << '\n';
    expect(line[8].string == 5 && line[8].fret == 12
               && line[9].string == 5 && line[9].fret == 10,
           "the hand stayed up the body: " + describe(line));
    // A fret up the body with no other place is still played.
    auto top = freshEngine();
    top->noteOn(84, 0.7f);
    expect(placed(*top, 84).fret == 20, "C6 was not placed at the twentieth fret");
}

// A lone note keeps to the strings beside the last one plucked when a fret
// under the hand can sound it (pickCrossingCost).
void testPickingHandStaysClose()
{
    // E5 and D5 on the first string, C4 on the D string's tenth fret: E4 is
    // the G string's ninth, under the hand, not the open first string three
    // strings away.
    auto engine = freshEngine();
    const auto line = playLine(*engine, { 76, 74, 60, 64 });
    std::cout << "Acustra picking hand up the neck: " << describe(line) << '\n';
    expect(line[2].string == 2 && line[2].fret == 10
               && line[3].string == 3 && line[3].fret == 9,
           "the line crossed the neck to an open string: " + describe(line));
    // In first position no fret is nearer, and the open string is taken.
    auto low = freshEngine();
    const auto open = playLine(*low, { 64, 60, 57, 64 });
    expect(open[2].string == 3 && open[3].string == 5 && open[3].fret == 0,
           "first position left its open string: " + describe(open));
}

// The hand shifting position with fingers on the strings slides the finger
// that lands on a wound string (planHandShiftArrival), lightly; a hand set
// down after a rest, a plain string and a note under the hand do not.
void testHandShiftSlidesItsArrival()
{
    const auto shifted = [] (double rest, int to)
    {
        // At the presets' 10% Release Noise; at zero no finger is heard.
        auto engine = std::make_unique<AcustraEngine>();
        acustra::EngineParameters parameters;
        parameters.releaseNoise = 0.1f;
        engine->setParameters(parameters);
        engine->prepare(sampleRate, blockSize);
        // A4, B4 and C5 on the first string leave the hand at the fifth fret.
        playLine(*engine, { 69, 71, 72 });
        run(*engine, rest);
        engine->noteOn(to, 0.7f);
        return engine;
    };
    // F2 is the low E string's first fret and nowhere else: four frets down.
    auto engine = shifted(0.0, 41);
    expect(placed(*engine, 41).string == 0 && placed(*engine, 41).fret == 1,
           "the shift fixture did not land on the low E string's first fret");
    const int frets = Access::handShift(*engine);
    expect(frets == -4, "the hand did not record its shift down the neck");
    expect(Access::arrival(*engine, 0) > 0.0,
           "the landing finger did not slide down the neck to its fret");
    expect(Access::arrivalPressure(*engine, 0) == 0.5f,
           "the landing finger did not ride the string at half force");
    // The same note half a second later: the hand had left the strings.
    auto rested = shifted(0.5, 41);
    expect(placed(*rested, 41).string == 0 && Access::arrival(*rested, 0) == 0.0,
           "a hand set down after a rest slid along the string");
    // F5 is five frets up the first string, which has no winding to slide on.
    auto plain = shifted(0.0, 77);
    expect(placed(*plain, 77).string == 5 && Access::handShift(*plain) > 0
               && Access::arrival(*plain, 5) == 0.0,
           "a plain string slid a hand shift");
    // E3 under the hand, at the A string's seventh fret, moves nothing.
    auto still = shifted(0.0, 52);
    expect(placed(*still, 52).string == 1 && placed(*still, 52).fret == 7
               && Access::arrival(*still, 1) == 0.0,
           "a note under the hand slid");
}

void testAllocatorCost()
{
    // Worst cases for one note-on's allocation, timed on the allocator
    // alone: a single note with five strings held and every string
    // remembered, a six-note chord plan, and a rolled chord refretted.
    auto engine = freshEngine();
    for (const int note : { 43, 47, 50, 55, 59 })
        engine->noteOn(note, 0.7f);
    run(*engine, 0.05);
    // Each figure is the best of several passes, as the output-bus cost
    // gate does: a scheduler preemption or a cold cache only ever adds time,
    // so the minimum is the allocator's own cost, and it still fails when
    // that cost itself is over the gate. A mean of wall-clock time read up to
    // eight times the quiet figure on a loaded machine and failed there.
    constexpr int repeats = 2000;
    constexpr int passes = 5;
    volatile int sink = 0;
    const auto microseconds = [] (auto from)
    {
        return std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - from).count();
    };
    double single = 1.0e30;
    for (int pass = 0; pass < passes; ++pass)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < repeats; ++repeat)
            sink = sink + Access::chooseString(*engine, 67 + (repeat & 3));
        single = std::min(single, microseconds(start) / repeats);
    }

    auto chordEngine = freshEngine();
    for (const int note : { 62, 65, 69 })
    {
        chordEngine->noteOn(note, 0.7f);
        run(*chordEngine, 0.05);
        chordEngine->noteOff(note);
    }
    const std::array<int, 6> chord { 64, 59, 56, 52, 47, 40 };
    double plan = 1.0e30;
    for (int pass = 0; pass < passes; ++pass)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < repeats; ++repeat)
            chordEngine->planChord(chord.data(), 6);
        plan = std::min(plan, microseconds(start) / repeats);
    }
    // Six notes each on four or five strings, filling all six: the widest
    // search a six-note chord makes.
    const std::array<int, 6> cluster { 64, 62, 60, 59, 57, 55 };
    double widest = 1.0e30;
    for (int pass = 0; pass < passes; ++pass)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < repeats / 10; ++repeat)
            chordEngine->planChord(cluster.data(), 6);
        widest = std::min(widest, microseconds(start) / (repeats / 10));
    }

    // The exposed rolled triad keeps C4 and E4 on their sounded strings;
    // searching the constrained shape cannot regenerate their attacks.
    auto rolled = freshEngine();
    rolled->noteOn(60, 0.8f);
    run(*rolled, 0.010);
    rolled->noteOn(64, 0.8f);
    run(*rolled, 0.010);
    double reshape = 1.0e30;
    constexpr int copies = 100;
    for (int repeat = 0; repeat < copies; ++repeat)
    {
        auto copy = std::make_unique<AcustraEngine>(*rolled);
        const auto start = std::chrono::steady_clock::now();
        sink = sink + Access::reshape(*copy, 67, 1, 3);
        reshape = std::min(reshape, microseconds(start));
    }
    std::cout << "Acustra allocator cost per note-on (best of "
              << passes << " passes, refret best of " << copies << "): single " << single
              << " us, six-note plan " << plan << " us (widest search "
              << widest << " us), committed rolled-chord search "
              << reshape << " us\n";
    expect(single < 20.0 && plan < 500.0 && widest < 1000.0 && reshape < 500.0,
           "the hand allocator is too slow for a note-on");
}
} // namespace

int main()
{
    testRolledTriadIsOneHandShape();
    testRefrettedStrumMemberLetGoIsReleased();
    testPendingAttacksKeepTheirTiming();
    testOpenChordsOneKeyAtATime();
    testScaleRunStaysInPositionThenShifts();
    testMelodyOverHeldBassKeepsTheBass();
    testLegatoLineKeepsItsString();
    testNotesLeftToRingKeepTheirStrings();
    testLiveChordReach();
    testChordChangeTakesTheHand();
    testDoubledKeyIsOneKey();
    testRepeatedNotesReplickTheirString();
    testQuietContactKeepsRepeatedString();
    testQuietContactChordPreference();
    testControllerStringsAreUnchanged();
    testChordsOnOneSampleAreOneShape();
    testHandTimedStrums();
    testForgottenHandIsTheHandlessAllocator();
    testHandKeepsItsPosition();
    testHandComesBackDownTheBody();
    testPickingHandStaysClose();
    testHandShiftSlidesItsArrival();
    testAllocatorCost();
    if (failures == 0)
        std::cout << "All Acustra hand allocator tests passed\n";
    return failures == 0 ? 0 : 1;
}
