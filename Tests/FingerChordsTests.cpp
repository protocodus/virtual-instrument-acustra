// Under Finger a chord of three or four strings is plucked by the thumb and
// fingers together, not swept: GuitarSet's finger-plucked bossa nova comping
// sounds such chords in no string order, each string a few milliseconds from
// the highest (Tools/MeasureFingerChords.py). Pick, Thumb, five and six
// strings, and a latched stroke direction still strum, exactly as before.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static bool attackFired(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].attackFired; }
    static bool strumming(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].strumming; }
    static float sign(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckParallelSign; }
    // Samples still to wait before a scheduled pluck, read after the
    // sample its note-on arrived on: its delay. Zero once it has sounded.
    static int pluckDelay(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckDelay; }
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

constexpr std::array<double, 3> threePattern { 0.0, 2.27, 4.85 };
constexpr std::array<double, 4> fourPattern { 0.0, 2.36, 6.98, 3.06 };
constexpr double halfWidthMs = 7.74;

void testDrawsFollowTheMeasuredLags()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 64);
        for (const int count : { 3, 4 })
            for (int rank = 0; rank < count; ++rank)
            {
                const double pattern = count == 3 ? threePattern[static_cast<std::size_t>(rank)]
                                                  : fourPattern[static_cast<std::size_t>(rank)];
                constexpr int draws = 40000;
                double sum = 0.0, squares = 0.0, lowest = 1.0e9, highest = -1.0e9;
                for (int i = 0; i < draws; ++i)
                {
                    const double ms = 1000.0 * engine->fingerChordOffsetSamples(rank, count) / rate;
                    sum += ms;
                    squares += ms * ms;
                    lowest = std::min(lowest, ms);
                    highest = std::max(highest, ms);
                }
                const double mean = sum / draws;
                const double sd = std::sqrt(squares / draws - mean * mean);
                const double sample = 1000.0 / rate;
                expect(std::abs(mean - pattern) < 0.12,
                       "a finger's mean lag is not the measured one");
                expect(std::abs(sd - 4.47) < 0.12,
                       "the fingers' timing does not spread by the measured 4.47 ms");
                expect(lowest >= pattern - halfWidthMs - sample
                           && highest <= pattern + halfWidthMs + sample,
                       "a finger's draw left the measured bounds");
            }
    }
}

std::unique_ptr<Performer> player(acustra::PickingTechnique picking, bool fingerChords,
                                  double rate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.fingerChords = fingerChords;
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
    int note, velocity; // velocity 0 is a key-up
};

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
            if (event.velocity > 0)
                performer.noteOn(offset, 1, event.note, event.velocity);
            else
                performer.noteOff(offset, 1, event.note);
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

// One chord every `spacing` samples, each held until just before the next.
std::vector<Event> chords(const std::vector<int>& notes, int count,
                          std::int64_t spacing, int velocity = 96,
                          std::int64_t start = 0)
{
    std::vector<Event> score;
    for (int c = 0; c < count; ++c)
    {
        const std::int64_t at = start + c * spacing;
        for (int note : notes)
            score.push_back({ at, note, velocity });
        for (int note : notes)
            score.push_back({ at + spacing - 400, note, 0 });
    }
    std::stable_sort(score.begin(), score.end(),
        [](const Event& a, const Event& b) { return a.at < b.at; });
    return score;
}

// Every chord's strings' pluck delays after its note-on, string by string:
// read one sample in, when a scheduled pluck has waited one sample of its
// delay and an immediate one has sounded.
struct Attacks
{
    std::vector<std::array<std::int64_t, 6>> chords;
    int stringCount = 0;
};

Attacks attacks(Performer& performer, const std::vector<int>& notes, int count,
                std::int64_t spacing, int velocity = 96)
{
    Attacks result;
    std::set<int> used;
    const auto score = chords(notes, count, spacing, velocity);
    play(performer, score, count * spacing, 1, [&](std::int64_t now)
    {
        if ((now - 1) % spacing != 0)
            return;
        std::array<std::int64_t, 6> at {};
        at.fill(-1);
        for (int note : notes)
        {
            const int string = performer.engine().heldString(note, 1);
            if (string < 0)
                continue;
            used.insert(string);
            at[static_cast<std::size_t>(string)] = Access::pluckDelay(performer.engine(), string);
        }
        result.chords.push_back(at);
    });
    result.stringCount = static_cast<int>(used.size());
    return result;
}

void testFingersPluckChordsTogether()
{
    // C major on the top three strings, and G with an open G and B on four.
    struct Case { std::vector<int> notes; int strings; const std::array<double, 4> pattern; };
    const std::vector<Case> cases {
        { { 60, 64, 67 }, 3, { 0.0, 2.27, 4.85, 0.0 } },
        { { 55, 59, 62, 67 }, 4, { 0.0, 2.36, 6.98, 3.06 } } };
    for (const auto& c : cases)
    {
        auto performer = player(acustra::PickingTechnique::Finger, true);
        constexpr int count = 60;
        constexpr std::int64_t spacing = 9600; // 0.2 s
        const auto result = attacks(*performer, c.notes, count, spacing);
        expect(result.stringCount == c.strings, "the chord did not land on the strings expected");
        int highestFirst = 0, lowestFirst = 0, complete = 0;
        std::vector<double> lagSum(6, 0.0);
        double worstSpread = 0.0;
        for (const auto& chord : result.chords)
        {
            std::vector<std::pair<int, std::int64_t>> sounding;
            for (int string = 0; string < 6; ++string)
                if (chord[static_cast<std::size_t>(string)] >= 0)
                    sounding.emplace_back(string, chord[static_cast<std::size_t>(string)]);
            if (static_cast<int>(sounding.size()) != c.strings)
                continue;
            ++complete;
            std::int64_t first = sounding.front().second, last = first;
            for (const auto& s : sounding)
            {
                first = std::min(first, s.second);
                last = std::max(last, s.second);
            }
            expect(first == 0, "the earliest finger did not sound at the note-on");
            worstSpread = std::max(worstSpread, 1000.0 * static_cast<double>(last - first) / 48000.0);
            // Rank from the top: sounding is low to high.
            const auto top = sounding.back().second;
            for (std::size_t k = 0; k < sounding.size(); ++k)
                lagSum[sounding.size() - 1 - k]
                    += 1000.0 * static_cast<double>(sounding[k].second - top) / 48000.0;
            highestFirst += sounding.back().second == first ? 1 : 0;
            lowestFirst += sounding.front().second == first ? 1 : 0;
        }
        expect(complete >= count - 1, "a plucked chord lost a string");
        const double maxPattern = *std::max_element(c.pattern.begin(), c.pattern.end());
        std::cout << c.strings << " strings: worst spread " << worstSpread
                  << " ms, highest first " << highestFirst << ", lowest first "
                  << lowestFirst << " of " << complete << '\n';
        expect(worstSpread <= maxPattern + 2.0 * halfWidthMs + 0.05,
               "a plucked chord spread wider than any finger can lag");
        expect(highestFirst > complete / 6 && lowestFirst > complete / 10,
               "the fingers kept one order: that is a sweep, not a pluck");
        for (int rank = 1; rank < c.strings; ++rank)
        {
            const double mean = lagSum[static_cast<std::size_t>(rank)] / complete;
            expect(std::abs(mean - c.pattern[static_cast<std::size_t>(rank)]) < 2.5,
                   "a string's mean lag behind the highest is not the measured one");
        }
    }
}

void testPlucksAreNotStrokes()
{
    // A plucked chord takes the single-note direction and no strum's
    // jitter, and leaves the strumming hand's alternation where it was: a
    // six-string strum right after it still starts down, then the return.
    auto performer = player(acustra::PickingTechnique::Finger, true);
    std::vector<Event> score;
    for (int note : { 60, 64, 67 }) score.push_back({ 0, note, 96 });
    for (int note : { 60, 64, 67 }) score.push_back({ 4000, note, 0 });
    const std::vector<int> e { 40, 47, 52, 56, 59, 64 };
    for (int note : e) score.push_back({ 6000, note, 96 });
    for (int note : e) score.push_back({ 11000, note, 0 });
    for (int note : e) score.push_back({ 12000, note, 96 });
    for (int note : e) score.push_back({ 17000, note, 0 });
    std::stable_sort(score.begin(), score.end(),
        [](const Event& a, const Event& b) { return a.at < b.at; });
    bool pluckSeen = false, strumOk = true, pluckOk = true;
    std::array<float, 2> strumSign { 0.0f, 0.0f };
    play(*performer, score, 20000, 16, [&](std::int64_t now)
    {
        if (now <= 4000)
            for (int note : { 60, 64, 67 })
            {
                const int s = performer->engine().heldString(note, 1);
                if (s >= 0 && Access::attackFired(performer->engine(), s))
                {
                    pluckSeen = true;
                    pluckOk &= !Access::strumming(performer->engine(), s)
                        && Access::sign(performer->engine(), s) == 1.0f;
                }
            }
        const int low = performer->engine().heldString(40, 1);
        if (low >= 0 && Access::attackFired(performer->engine(), low))
        {
            const std::size_t which = now < 12000 ? 0 : 1;
            strumOk &= Access::strumming(performer->engine(), low);
            strumSign[which] = Access::sign(performer->engine(), low);
        }
    });
    expect(pluckSeen && pluckOk, "a plucked chord took a strum's direction or jitter");
    expect(strumOk && strumSign[0] == 1.0f && strumSign[1] == -1.0f,
           "a plucked chord moved the strumming hand's alternation");
}

void testEverythingElseStillStrums()
{
    // Bit-identical with the switch on and off: Pick and Thumb chords, five-
    // and six-string Finger chords, and Finger chords under a latched C0.
    const std::vector<std::vector<int>> shapes {
        { 60, 64, 67 }, { 55, 59, 62, 67 }, { 45, 52, 57, 61, 64 }, { 40, 47, 52, 56, 59, 64 } };
    for (const auto picking : { acustra::PickingTechnique::Pick, acustra::PickingTechnique::Thumb })
        for (const auto& shape : shapes)
        {
            auto on = player(picking, true), off = player(picking, false);
            const auto score = chords(shape, 6, 12000);
            expect(play(*on, score, 6 * 12000) == play(*off, score, 6 * 12000),
                   "a Pick or Thumb chord changed with the finger chords");
        }
    for (std::size_t s = 2; s < shapes.size(); ++s)
    {
        auto on = player(acustra::PickingTechnique::Finger, true);
        auto off = player(acustra::PickingTechnique::Finger, false);
        const auto score = chords(shapes[s], 6, 12000);
        expect(play(*on, score, 6 * 12000) == play(*off, score, 6 * 12000),
               "a five- or six-string Finger strum changed");
    }
    {
        auto on = player(acustra::PickingTechnique::Finger, true);
        auto off = player(acustra::PickingTechnique::Finger, false);
        auto score = chords(shapes[0], 6, 12000, 96, 1000);
        score.insert(score.begin(), Event { 0, 24, 100 }); // C0: downstrokes
        expect(play(*on, score, 7 * 12000) == play(*off, score, 7 * 12000),
               "a latched stroke direction no longer strums a Finger chord");
    }
    {
        // And the change itself is audible.
        auto on = player(acustra::PickingTechnique::Finger, true);
        auto off = player(acustra::PickingTechnique::Finger, false);
        const auto score = chords(shapes[1], 6, 12000);
        const auto a = play(*on, score, 6 * 12000), b = play(*off, score, 6 * 12000);
        double difference = 0.0, level = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            difference += (a[i] - b[i]) * (a[i] - b[i]);
            level += b[i] * b[i];
        }
        expect(difference > 1.0e-3 * level, "plucking a Finger chord changed nothing audible");
    }
}

void testBlockAndRateIndependence()
{
    const auto score = chords({ 55, 59, 62, 67 }, 8, 9000);
    auto reference = player(acustra::PickingTechnique::Finger, true);
    const auto expected = play(*reference, score, 8 * 9000, 64);
    for (const int block : { 1, 61, 512 })
    {
        auto performer = player(acustra::PickingTechnique::Finger, true);
        expect(play(*performer, score, 8 * 9000, block) == expected,
               "a plucked chord depends on the block size");
    }
    for (const double rate : { 44100.0, 96000.0 })
    {
        auto performer = player(acustra::PickingTechnique::Finger, true, rate);
        const auto spacing = static_cast<std::int64_t>(0.2 * rate);
        const auto result = attacks(*performer, { 60, 64, 67 }, 20, spacing);
        double worst = 0.0;
        for (const auto& chord : result.chords)
        {
            std::int64_t first = -1, last = -1;
            for (const auto at : chord)
                if (at >= 0)
                {
                    first = first < 0 ? at : std::min(first, at);
                    last = std::max(last, at);
                }
            if (first >= 0)
                worst = std::max(worst, 1000.0 * static_cast<double>(last - first) / rate);
        }
        expect(worst > 1.0 && worst <= 4.85 + 2.0 * halfWidthMs + 0.05,
               "a plucked chord's spread is not the same in milliseconds at every rate");
    }
}
} // namespace

int main()
{
    std::cout << std::unitbuf;
    testDrawsFollowTheMeasuredLags();
    testFingersPluckChordsTogether();
    testPlucksAreNotStrokes();
    testEverythingElseStillStrums();
    testBlockAndRateIndependence();
    if (failures != 0)
    {
        std::cerr << failures << " finger chord check(s) failed\n";
        return 1;
    }
    std::cout << "finger chord checks passed\n";
    return 0;
}
