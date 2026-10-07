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
    static int chooseHarmonic(const AcustraEngine& engine, int midiNote)
    {
        return engine.chooseHarmonic(midiNote).string;
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
    static int harmonic(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].harmonic;
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

void testRolledTriadKeepsItsAttacks()
{
    // With no look-ahead C4 claims B1 and E4 the open high E before G4
    // arrives. Moving C4 to G5 then used to pluck it again. These exposed
    // rolls keep each attack where it sounded, accepting the wider final
    // fingering. The whole-shape/Gather tests retain the one-hand gate.
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
            int fired = 0;
            for (std::size_t string = 0; string < attacks.size(); ++string)
                fired += attacks[string] != nextAttacks[string] ? 1 : 0;
            expect(fired == 1, "one rolled key created " + std::to_string(fired)
                       + " attacks instead of one");
            for (std::size_t index = 0; index < played.size(); ++index)
                expect(after[index].string == before[index].string,
                       "a later rolled key moved an already sounded note");
            played.push_back(note);
            run(*engine, 0.010);
        }
        const auto shape = shapeOf(*engine, triad);
        std::cout << "Acustra rolled triad " << triad[0] << "-" << triad[1]
                  << "-" << triad[2] << " over 20 ms: " << describe(shape) << '\n';
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

void testOnlyUnfiredAttacksMove()
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
    expect(Access::chooseString(*noContact, 55) == 2
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

void testQuietContactHarmonicAndChordPreference()
{
    for (const int note : { 86, 95 })
    {
        auto engine = freshEngine();
        engine->noteOn(note, 0.7f);
        const int string = engine->heldString(note);
        expect(string == (note == 86 ? 3 : 5), "quiet harmonic used wrong initial string");
        if (string < 0) continue;
        run(*engine, 0.2);
        engine->noteOff(note);
        run(*engine, 0.1);
        expect(Access::contact(*engine, string), "harmonic key-up failed to land its contact");
        // Exercise the observer boundary independently of the noise seed and
        // decay rate, retaining the actual contact and all its wave histories.
        Access::quietObserver(*engine, string);
        expect(Access::chooseHarmonic(*engine, note) == string,
               "a quiet harmonic abandoned the string under its release contact");
        auto noContact = std::make_unique<AcustraEngine>(*engine);
        Access::removeContact(*noContact, string, 0);
        Access::removeContact(*noContact, string, 1);
        expect(Access::chooseHarmonic(*noContact, note) == string - 1,
               "inactive harmonic contact changed the existing unused-string preference");
        auto repeated = std::make_unique<AcustraEngine>(*engine);
        repeated->noteOn(note, 0.7f);
        expect(repeated->heldString(note) == string
                   && Access::harmonic(*repeated, string) == 6,
               "quiet harmonic reattack moved to another string or harmonic node");
        if (note == 95)
        {
            auto parameters = acustra::EngineParameters {};
            parameters.tuning = acustra::Tuning::HalfStepDown;
            engine->setParameters(parameters);
            run(*engine, 0.002);
            expect(Access::contact(*engine, string) && !engine->canSound(note)
                       && Access::chooseHarmonic(*engine, note) < 0,
                   "an active quiet contact bypassed harmonic tuning eligibility");
        }
    }
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
    disabled->setPerformanceRealism({ false, false, false, false, false });
    disabled->prepare(sampleRate, blockSize);
    disabled->noteOn(55, 0.7f);
    run(*disabled, 0.2);
    disabled->noteOff(55);
    run(*disabled, 0.1);
    Access::quietObserver(*disabled, 3);
    expect(!Access::contact(*disabled, 3)
               && Access::chooseString(*disabled, 55) == 2
               && Access::chooseStringWithoutHand(*disabled, 55) == 2,
           "all-off realism changed the old quiet-string allocator");
}

// D6 is the G string's sixth or the D string's eighth harmonic; B6 is the
// high E's sixth or the B's eighth. A lower harmonic does not justify taking
// a held string when the other physical string is available. Both candidates
// are above the fretted range and within the existing 25-cent tolerance.
void testNaturalHarmonicsKeepHeldStrings()
{
    enum class State { Silent, HeldAndSilent, HeldAndReleased,
                       ReleasedAndHeld, BothHeld, BothReleased,
                       HeldAndPedalled, DuplicateHeldAndReleased };
    for (const auto model : { acustra::GuitarModel::Original,
                              acustra::GuitarModel::Bellido1978 })
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int harmonicNote : { 86, 95 })
                for (const auto state : { State::Silent, State::HeldAndSilent,
                         State::HeldAndReleased, State::ReleasedAndHeld,
                         State::BothHeld, State::BothReleased,
                         State::HeldAndPedalled, State::DuplicateHeldAndReleased })
                {
                    auto engine = std::make_unique<AcustraEngine>();
                    engine->prepare(rate, blockSize);
                    acustra::EngineParameters parameters;
                    parameters.room = 0.0f;
                    parameters.guitarModel = model;
                    if (model == acustra::GuitarModel::Bellido1978)
                    {
                        parameters.shape = acustra::BodyShape::Auditorium;
                        parameters.bodyMaterial = acustra::BodyMaterial::Mahogany;
                    }
                    engine->setParameters(parameters);
                    const auto advance = [&] (int samples)
                    {
                        std::array<float, blockSize> left {}, right {};
                        while (samples > 0)
                        {
                            const int count = std::min(samples, blockSize);
                            engine->process(left.data(), right.data(), count);
                            samples -= count;
                        }
                    };
                    const int wait = static_cast<int>(std::lround(0.05 * rate));
                    advance(wait);
                    const int lowOrderString = harmonicNote == 86 ? 3 : 5;
                    const int highOrderString = lowOrderString - 1;
                    const int lowOrderNote = harmonicNote == 86 ? 55 : 64;
                    const int highOrderNote = harmonicNote == 86 ? 50 : 59;
                    if (state != State::Silent)
                    {
                        engine->noteOn(lowOrderNote, 0.7f);
                        advance(wait);
                    }
                    if (state != State::Silent && state != State::HeldAndSilent)
                    {
                        engine->noteOn(highOrderNote, 0.7f);
                        advance(wait);
                    }
                    if (state == State::ReleasedAndHeld || state == State::BothReleased)
                        engine->noteOff(lowOrderNote);
                    if (state == State::HeldAndReleased || state == State::BothReleased
                        || state == State::DuplicateHeldAndReleased)
                        engine->noteOff(highOrderNote);
                    if (state == State::HeldAndPedalled)
                    {
                        engine->setSustainPedal(true);
                        engine->noteOff(highOrderNote);
                    }
                    if (state == State::DuplicateHeldAndReleased)
                        engine->noteOn(lowOrderNote, 0.7f);
                    advance(blockSize);
                    const bool preserveHeld = state == State::HeldAndSilent
                        || state == State::HeldAndReleased
                        || state == State::HeldAndPedalled
                        || state == State::DuplicateHeldAndReleased;
                    if (preserveHeld)
                    {
                        expect(engine->heldString(lowOrderNote) == lowOrderString,
                               "harmonic fixture did not hold its low-order candidate");
                        expect(!Access::keyDown(*engine, highOrderString),
                               "harmonic fixture's available candidate was held");
                    }
                    if (state == State::HeldAndPedalled)
                        expect(Access::pedalHeld(*engine, highOrderString),
                               "harmonic fixture did not retain the released note under the pedal");
                    const auto before = Access::attackStates(*engine);
                    const int owners = Access::owners(*engine, lowOrderString);
                    engine->noteOn(harmonicNote, 0.7f);
                    const int chosen = preserveHeld ? highOrderString : lowOrderString;
                    expect(engine->heldString(harmonicNote) == chosen
                               && Access::harmonic(*engine, chosen) == (preserveHeld ? 8 : 6),
                           "natural harmonic took a held string ahead of an available one"
                               " or changed the existing equal-availability choice");
                    const auto after = Access::attackStates(*engine);
                    for (int string = 0; string < AcustraEngine::stringCount; ++string)
                        expect((before[static_cast<std::size_t>(string)]
                                    != after[static_cast<std::size_t>(string)]) == (string == chosen),
                               "harmonic allocation released an attack on the wrong string");
                    if (preserveHeld)
                    {
                        expect(engine->heldString(lowOrderNote) == lowOrderString
                                   && Access::owners(*engine, lowOrderString) == owners,
                               "a natural harmonic stole a held note or its duplicate owners");
                        if (owners == 2)
                        {
                            engine->noteOff(lowOrderNote);
                            expect(engine->heldString(lowOrderNote) == lowOrderString
                                       && Access::owners(*engine, lowOrderString) == 1,
                                   "a harmonic lost one of the held note's owners");
                            engine->noteOff(lowOrderNote);
                            expect(engine->heldString(lowOrderNote) < 0,
                                   "a held note preserved by a harmonic ignored its final key-up");
                        }
                    }
                    if (state == State::BothHeld || state == State::ReleasedAndHeld)
                        expect(engine->heldString(highOrderNote) == highOrderString,
                               "a harmonic changed its existing all-held fallback");
                }
}

void testReleasedHarmonicReplucksItsString()
{
    for (const auto model : { acustra::GuitarModel::Original,
                              acustra::GuitarModel::Bellido1978 })
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int note : { 86, 95 })
            {
                auto engine = std::make_unique<AcustraEngine>();
                engine->prepare(rate, blockSize);
                acustra::EngineParameters parameters;
                parameters.room = 0.0f;
                parameters.guitarModel = model;
                if (model == acustra::GuitarModel::Bellido1978)
                {
                    parameters.shape = acustra::BodyShape::Auditorium;
                    parameters.bodyMaterial = acustra::BodyMaterial::Mahogany;
                }
                engine->setParameters(parameters);
                std::array<float, blockSize> left {}, right {};
                for (int block = 0; block < 80; ++block)
                    engine->process(left.data(), right.data(), blockSize);
                engine->noteOn(note, 0.7f);
                const int original = engine->heldString(note);
                expect(original == (note == 86 ? 3 : 5)
                           && Access::harmonic(*engine, original) == 6,
                       "an isolated harmonic's original string choice changed");
                if (original < 0)
                    continue;
                for (int block = 0; block < 40; ++block)
                    engine->process(left.data(), right.data(), blockSize);
                engine->noteOff(note);
                engine->process(left.data(), right.data(), blockSize);
                expect(!Access::keyDown(*engine, original)
                           && Access::ringing(*engine, original),
                       "repeated harmonic fixture was no longer ringing after key-up");
                const auto before = Access::attackStates(*engine);
                engine->noteOn(note, 0.7f);
                expect(engine->heldString(note) == original
                           && Access::harmonic(*engine, original) == 6,
                       "a released harmonic hopped to an unused string on its repeat");
                const auto after = Access::attackStates(*engine);
                for (int string = 0; string < AcustraEngine::stringCount; ++string)
                    expect((before[static_cast<std::size_t>(string)]
                                != after[static_cast<std::size_t>(string)]) == (string == original),
                           "repeating a harmonic excited a different physical string");
                // A tuning change retunes the old vibration. The stale MIDI
                // owner must not make canSound accept an unreachable pitch:
                // B6 has no eligible natural harmonic in Half Step Down.
                if (note == 95)
                {
                    engine->noteOff(note);
                    parameters.tuning = acustra::Tuning::HalfStepDown;
                    engine->setParameters(parameters);
                    engine->process(left.data(), right.data(), blockSize);
                    expect(!engine->canSound(note),
                           "a ringing harmonic bypassed the new tuning's pitch eligibility");
                    const auto tunedBefore = Access::attackStates(*engine);
                    engine->noteOn(note, 0.7f);
                    expect(engine->heldString(note) < 0
                               && Access::attackStates(*engine) == tunedBefore,
                           "a stale harmonic assignment sounded an unreachable note after retuning");
                    // Current tuning governs eligibility during the join
                    // window and after it. Waiting for the grace to expire
                    // must not be necessary to reject an impossible pitch.
                    int remaining = static_cast<int>(std::ceil(rate * 7.5 / 120.0)) + 2;
                    while (remaining > 0)
                    {
                        const int count = std::min(remaining, blockSize);
                        engine->process(left.data(), right.data(), count);
                        remaining -= count;
                    }
                    const auto expiredBefore = Access::attackStates(*engine);
                    expect(!engine->canSound(note),
                           "an expired grace revived an unreachable harmonic's pitch eligibility");
                    engine->noteOn(note, 0.7f);
                    expect(engine->heldString(note) < 0
                               && Access::attackStates(*engine) == expiredBefore,
                           "an expired grace sounded an unreachable harmonic after retuning");
                }
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
void testHandTimedStrumsKeepTheirAttacks()
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
                auto engine = freshEngine();
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
                    expect(fired == 1, "one hand-timed key regenerated earlier attacks");
                    for (int string = 0; string < AcustraEngine::stringCount; ++string)
                    {
                        const int earlier = committed[static_cast<std::size_t>(string)];
                        if (earlier < 0 || engine->heldString(earlier) == string)
                            continue;
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
                              << "stroke " << gap * 1000.0 << " ms apart: "
                              << describe(shape) << '\n';
                }
            }
    std::cout << "Acustra hand-timed strums outside one hand: " << impossible
              << " of " << strums << "; incoming notes took an occupied string in "
              << takenNotes << " of " << strums << " (" << forcedTakes
              << " takes with no valid free string)\n";
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
    testRolledTriadKeepsItsAttacks();
    testRefrettedStrumMemberLetGoIsReleased();
    testOnlyUnfiredAttacksMove();
    testOpenChordsOneKeyAtATime();
    testScaleRunStaysInPositionThenShifts();
    testMelodyOverHeldBassKeepsTheBass();
    testRepeatedNotesReplickTheirString();
    testQuietContactKeepsRepeatedString();
    testQuietContactHarmonicAndChordPreference();
    testNaturalHarmonicsKeepHeldStrings();
    testReleasedHarmonicReplucksItsString();
    testControllerStringsAreUnchanged();
    testChordsOnOneSampleAreOneShape();
    testHandTimedStrumsKeepTheirAttacks();
    testForgottenHandIsTheHandlessAllocator();
    testAllocatorCost();
    if (failures == 0)
        std::cout << "All Acustra hand allocator tests passed\n";
    return failures == 0 ? 0 : 1;
}
