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

void testRolledTriadStaysInOneHand()
{
    // C4-E4-G4 rolled low to high, 10 ms apart, as a keyboard hand plays it.
    // Taken one key at a time with no hand, C4 claims the B string's first
    // fret and E4 the open E, and G4 was left the G string's twelfth fret:
    // an eleven-fret stretch no hand makes.
    // D4-F#4-A4 and E4-G4-B4 stretched 12 and 8 frets the same way.
    for (const auto& triad : { std::vector<int> { 62, 66, 69 },
                               std::vector<int> { 64, 67, 71 } })
    {
        auto other = freshEngine();
        for (const int note : triad)
        {
            other->noteOn(note, 0.8f);
            run(*other, 0.010);
        }
        const auto shape = shapeOf(*other, triad);
        std::cout << "Acustra rolled triad " << triad[0] << "-" << triad[1]
                  << "-" << triad[2] << " over 20 ms: " << describe(shape) << '\n';
        expect(oneHand(shape, 4),
               "a rolled triad is not one hand's shape: " + describe(shape));
    }
    auto engine = freshEngine();
    engine->noteOn(60, 0.8f);
    run(*engine, 0.010);
    engine->noteOn(64, 0.8f);
    run(*engine, 0.010);
    engine->noteOn(67, 0.8f);
    const auto shape = shapeOf(*engine, { 60, 64, 67 });
    std::cout << "Acustra rolled C4-E4-G4 over 20 ms: " << describe(shape) << '\n';
    expect(oneHand(shape, 4),
           "a rolled C major triad is not one hand's shape: " + describe(shape));
    // The refretted string is replucked, not left sounding its old note.
    run(*engine, 0.4);
    int sounding = 0;
    for (int string = 0; string < AcustraEngine::stringCount; ++string)
        sounding += Access::soundingNote(*engine, string) >= 0 ? 1 : 0;
    expect(sounding == 3,
           "refretting the rolled triad left a fourth string sounding: "
               + std::to_string(sounding));

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
        engine->noteOff(60);
        const int string = engine->heldString(60, 1);
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
    auto engine = freshEngine();
    for (const int note : { 62, 65, 69 })
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

    // The rolled triad's refret: C4 and E4 down 10 ms apart, G4 chosen on
    // the G string's twelfth fret. The refret changes the engine, so each
    // timing runs cold on its own copy, and the best of them is the refret's
    // cost.
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
              << widest << " us), rolled-chord refret with two replucks "
              << reshape << " us\n";
    expect(single < 20.0 && plan < 500.0 && widest < 1000.0 && reshape < 500.0,
           "the hand allocator is too slow for a note-on");
}
} // namespace

int main()
{
    testRolledTriadStaysInOneHand();
    testRefrettedStrumMemberLetGoIsReleased();
    testOpenChordsOneKeyAtATime();
    testScaleRunStaysInPositionThenShifts();
    testMelodyOverHeldBassKeepsTheBass();
    testRepeatedNotesReplickTheirString();
    testControllerStringsAreUnchanged();
    testChordsOnOneSampleAreOneShape();
    testForgottenHandIsTheHandlessAllocator();
    testAllocatorCost();
    if (failures == 0)
        std::cout << "All Acustra hand allocator tests passed\n";
    return failures == 0 ? 0 : 1;
}
