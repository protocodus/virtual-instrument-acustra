// A pick does not meet a strum's strings on the sweep's own line: on
// GuitarSet's picked comping each string sounds a few milliseconds off it,
// a time that does not grow with the stroke's, and a third to two thirds of
// the strokes do not sound in string order (Tools/MeasureStrumScatter.py).
// Each swept string now takes a draw of its own after the stroke's speed
// draw; the earliest sounds at the note-on.
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
    // Samples still to wait before a scheduled pluck, read after the
    // sample its note-on arrived on: its delay. Zero once it has sounded.
    static int pluckDelay(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckDelay; }
    static bool mutedStroke(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].mutedStroke; }
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

constexpr double scatterMs = 4.35;
constexpr double halfWidthMs = 7.53;
constexpr std::array<int, 6> eMajor { 40, 47, 52, 56, 59, 64 };

void testDrawsAreUniform()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 64);
        constexpr int draws = 40000;
        double sum = 0.0, squares = 0.0, lowest = 1.0e9, highest = -1.0e9;
        for (int i = 0; i < draws; ++i)
        {
            const double ms = 1000.0 * engine->strumScatterSamples() / rate;
            sum += ms;
            squares += ms * ms;
            lowest = std::min(lowest, ms);
            highest = std::max(highest, ms);
        }
        const double mean = sum / draws;
        const double sd = std::sqrt(squares / draws - mean * mean);
        expect(std::abs(mean) < 0.06, "the scatter is not centred on the sweep");
        expect(std::abs(sd - scatterMs) < 0.06, "the scatter does not spread by the measured 4.35 ms");
        expect(lowest >= -halfWidthMs - 1.0e-3 && highest <= halfWidthMs + 1.0e-3
                   && lowest < -0.99 * halfWidthMs && highest > 0.99 * halfWidthMs,
               "the scatter is not uniform over its half-width");
    }
}

std::unique_ptr<Performer> player(bool scatter,
                                  acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
                                  double rate = 48000.0, bool muted = true)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.strumScatter = scatter;
    realism.mutedStrings = muted;
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
                          std::int64_t spacing, int velocity = 96)
{
    std::vector<Event> score;
    for (int c = 0; c < count; ++c)
    {
        const std::int64_t at = c * spacing;
        for (int note : notes)
            score.push_back({ at, note, velocity });
        for (int note : notes)
            score.push_back({ at + spacing - 400, note, 0 });
    }
    std::stable_sort(score.begin(), score.end(),
        [](const Event& a, const Event& b) { return a.at < b.at; });
    return score;
}

// Every stroke's pluck delays, string by string (-1 for a string it does
// not reach), read one sample after its note-on, and its speed draw.
struct Stroke
{
    std::array<int, 6> delay {};
    float scale = 1.0f;
};

std::vector<Stroke> strokes(Performer& performer, const std::vector<int>& notes,
                            int count, std::int64_t spacing, int velocity = 96)
{
    std::vector<Stroke> result;
    play(performer, chords(notes, count, spacing, velocity), count * spacing, 1,
         [&](std::int64_t now)
    {
        if ((now - 1) % spacing != 0)
            return;
        Stroke stroke;
        stroke.delay.fill(-1);
        for (int string = 0; string < 6; ++string)
            if (performer.engine().getStringActivity()[static_cast<std::size_t>(string)].keyDown
                || Access::mutedStroke(performer.engine(), string))
                stroke.delay[static_cast<std::size_t>(string)]
                    = Access::pluckDelay(performer.engine(), string);
        stroke.scale = performer.engine().strumSpeedScale();
        result.push_back(stroke);
    });
    return result;
}

bool inStringOrder(const std::array<int, 6>& delay)
{
    int previous = -1;
    bool rising = true, falling = true;
    for (const int d : delay)
    {
        if (d < 0)
            continue;
        if (previous >= 0)
        {
            rising &= d > previous;
            falling &= d < previous;
        }
        previous = d;
    }
    return rising || falling;
}

void testStringsScatterAboutTheSweep()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto scattered = player(true, acustra::PickingTechnique::Pick, rate);
        auto swept = player(false, acustra::PickingTechnique::Pick, rate);
        constexpr int count = 160;
        const auto spacing = static_cast<std::int64_t>(0.25 * rate);
        const std::vector<int> notes(eMajor.begin(), eMajor.end());
        const auto on = strokes(*scattered, notes, count, spacing);
        const auto off = strokes(*swept, notes, count, spacing);
        expect(on.size() == static_cast<std::size_t>(count) && off.size() == on.size(),
               "a stroke went missing");
        int ordered = 0, sweptOrdered = 0, lowFirst = 0, otherFirst = 0;
        // A smaller speed draw is a faster stroke.
        double squares = 0.0, fastSquares = 0.0, slowSquares = 0.0;
        int values = 0, fastValues = 0, slowValues = 0;
        for (std::size_t k = 0; k < std::min(on.size(), off.size()); ++k)
        {
            const auto& a = on[k];
            const auto& b = off[k];
            expect(a.scale == b.scale, "the scatter moved a stroke's speed draw");
            int earliest = 1 << 30;
            double mean = 0.0;
            std::array<double, 6> offset {};
            for (std::size_t s = 0; s < 6; ++s)
            {
                expect(a.delay[s] >= 0 && b.delay[s] >= 0, "a string of the chord was not reached");
                earliest = std::min(earliest, a.delay[s]);
                offset[s] = 1000.0 * (a.delay[s] - b.delay[s]) / rate;
                mean += offset[s] / 6.0;
            }
            expect(earliest == 0, "the stroke's earliest string did not sound at its note-on");
            ordered += inStringOrder(a.delay) ? 1 : 0;
            sweptOrdered += inStringOrder(b.delay) ? 1 : 0;
            // A downstroke starts at the low E unscattered; scattered, any
            // string can be the first the pick is heard on.
            if (b.delay[0] == 0)
                (a.delay[0] == 0 ? lowFirst : otherFirst) += 1;
            for (std::size_t s = 0; s < 6; ++s)
            {
                const double deviation = (offset[s] - mean) * (offset[s] - mean);
                squares += deviation;
                ++values;
                if (a.scale < 0.8f) { fastSquares += deviation; ++fastValues; }
                if (a.scale > 1.2f) { slowSquares += deviation; ++slowValues; }
            }
        }
        // Each string's own draw about the stroke's mean: sigma sqrt(5/6).
        const double expected = scatterMs * std::sqrt(5.0 / 6.0);
        const double sd = std::sqrt(squares / values);
        const double fastest = std::sqrt(fastSquares / std::max(1, fastValues));
        const double slowest = std::sqrt(slowSquares / std::max(1, slowValues));
        std::cout << rate << " Hz: scatter about the stroke " << sd << " ms (expected "
                  << expected << "), fastest third " << fastest << ", slowest third "
                  << slowest << "; in string order " << ordered << " of " << count
                  << " (unscattered " << sweptOrdered << "); a downstroke led by the low E "
                  << lowFirst << ", by another string " << otherFirst << '\n';
        expect(std::abs(sd - expected) < 0.3, "the strings do not scatter by the measured 4.35 ms");
        expect(fastValues > 120 && slowValues > 120 && std::abs(fastest - slowest) < 0.8,
               "the scatter grew or shrank with the stroke's speed: it is a time, not a place");
        expect(sweptOrdered == count, "the unscattered sweep lost its string order");
        expect(ordered > count / 10 && ordered < 7 * count / 10,
               "six-string strokes are not out of string order as often as GuitarSet's");
        expect(lowFirst > 0 && otherFirst > 0, "the stroke's first string never changed");
    }
}

void testDeadStrokesScatterWithTheChord()
{
    // E2, B3 and E4: the pick crosses A, D and G between them.
    auto scattered = player(true);
    auto swept = player(false);
    constexpr int count = 60;
    constexpr std::int64_t spacing = 12000;
    const std::vector<int> notes { 40, 59, 64 };
    const auto on = strokes(*scattered, notes, count, spacing);
    const auto off = strokes(*swept, notes, count, spacing);
    int moved = 0, mutedFirst = 0, reordered = 0;
    for (std::size_t k = 0; k < std::min(on.size(), off.size()); ++k)
    {
        int earliest = 1 << 30;
        for (std::size_t s = 0; s < 6; ++s)
        {
            expect(on[k].delay[s] >= 0 && off[k].delay[s] >= 0,
                   "a crossed string took no dead stroke");
            earliest = std::min(earliest, on[k].delay[s]);
        }
        expect(earliest == 0, "the stroke's earliest contact did not sound at its note-on");
        for (std::size_t s = 1; s <= 3; ++s)
            moved += on[k].delay[s] != off[k].delay[s] ? 1 : 0;
        mutedFirst += on[k].delay[1] == 0 || on[k].delay[2] == 0 || on[k].delay[3] == 0 ? 1 : 0;
        reordered += inStringOrder(on[k].delay) ? 0 : 1;
    }
    expect(moved > 100, "the dead strokes kept the unscattered sweep's places");
    expect(reordered > 0, "the dead strokes never sounded out of string order");
    std::cout << "Dead strokes: " << moved << " of " << 3 * count << " moved, "
              << mutedFirst << " strokes led by one, " << reordered << " out of order\n";
}

void testRepeatsStayClear()
{
    // A soft six-string stroke every 60 ms: the speed draw is held so the
    // whole sweep ends before the next, and the scatter must not undo it.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto performer = player(true, acustra::PickingTechnique::Pick, rate);
        const auto spacing = static_cast<std::int64_t>(std::lround(0.060 * rate));
        constexpr int count = 40;
        const std::vector<int> open { 40, 45, 50, 55, 59, 64 };
        std::vector<Event> score;
        for (int c = 0; c < count; ++c)
            for (int note : open)
                score.push_back({ c * spacing, note, 16 });
        int late = 0, unsounded = 0;
        play(*performer, score, count * spacing, 1, [&](std::int64_t now)
        {
            // Just before a stroke: every string of the last has sounded.
            if (now % spacing == 0 && now > spacing)
                for (int string = 0; string < 6; ++string)
                    unsounded += Access::pluckDelay(performer->engine(), string) > 0 ? 1 : 0;
            if ((now - 1) % spacing == 0 && now > spacing)
                for (int string = 0; string < 6; ++string)
                    late += Access::pluckDelay(performer->engine(), string) > spacing - 1 ? 1 : 0;
        });
        expect(late == 0, "the scatter scheduled a string past the next stroke");
        expect(unsounded == 0, "a repeated stroke left a string waiting into the next");
    }
}

std::vector<float> render(bool scatter, acustra::PickingTechnique picking,
                          const std::vector<Event>& score, std::int64_t length, int block = 64)
{
    auto performer = player(scatter, picking);
    return play(*performer, score, length, block);
}

double difference(const std::vector<float>& a, const std::vector<float>& b)
{
    double energy = 0.0, error = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        energy += static_cast<double>(a[i]) * a[i];
        error += static_cast<double>(a[i] - b[i]) * (a[i] - b[i]);
    }
    return energy > 0.0 ? std::sqrt(error / energy) : 1.0;
}

void testUnsweptPlayingIsUnchanged()
{
    struct Case { acustra::PickingTechnique picking; std::vector<Event> score; const char* what; };
    std::vector<Event> melody;
    for (int i = 0; i < 12; ++i)
        melody.push_back({ i * 4000, 52 + (i * 5) % 17, 70 + i * 3 });
    std::vector<Event> dyads;
    for (int i = 0; i < 8; ++i)
        for (int note : { 52, 59 })
            dyads.push_back({ i * 6000, note + i % 3, 90 });
    const auto plucked = chords({ 55, 59, 62, 67 }, 8, 9600);
    const auto cluster = chords({ 40, 45, 50, 55, 59, 64, 69 }, 4, 12000);
    const std::vector<Case> cases {
        { acustra::PickingTechnique::Pick, melody, "picked single notes" },
        { acustra::PickingTechnique::Finger, melody, "finger single notes" },
        { acustra::PickingTechnique::Finger, dyads, "finger two-note groups" },
        { acustra::PickingTechnique::Finger, plucked, "a finger-plucked four-string chord" },
        { acustra::PickingTechnique::Pick, cluster, "a seven-note group the hand cannot plan" } };
    for (const auto& c : cases)
    {
        const auto length = c.score.back().at + 24000;
        const bool same = render(true, c.picking, c.score, length)
            == render(false, c.picking, c.score, length);
        if (!same)
            std::cerr << "  changed: " << c.what << '\n';
        expect(same, "the scatter changed playing that has no sweep");
    }
    const auto strums = chords(std::vector<int>(eMajor.begin(), eMajor.end()), 6, 12000);
    const double change = difference(render(false, acustra::PickingTechnique::Pick, strums, 96000),
                                     render(true, acustra::PickingTechnique::Pick, strums, 96000));
    std::cout << "Scattered strums against the sweep: normalised difference " << change << '\n';
    expect(change > 0.05, "the scatter did not change a strum audibly");
}

void testBlocksAndRestart()
{
    const auto score = chords({ 43, 47, 50, 55, 59, 67 }, 10, 7000, 100);
    const auto reference = render(true, acustra::PickingTechnique::Pick, score, 80000, 1);
    for (const int block : { 61, 64, 512 })
        expect(render(true, acustra::PickingTechnique::Pick, score, 80000, block) == reference,
               "the scattered strums depend on the block size");
    // Preparing again restarts the draws, so it plays as a new performer.
    auto performer = player(true);
    const auto first = play(*performer, score, 80000);
    performer->prepare(48000.0, 512);
    performer->setTempoBpm(120.0);
    expect(play(*performer, score, 80000) == first,
           "preparing again did not restart the scatter's draws");
}
}

int main()
{
    testDrawsAreUniform();
    testStringsScatterAboutTheSweep();
    testDeadStrokesScatterWithTheChord();
    testRepeatsStayClear();
    testUnsweptPlayingIsUnchanged();
    testBlocksAndRestart();
    if (failures != 0)
    {
        std::cerr << failures << " strum scatter check(s) failed\n";
        return 1;
    }
    std::cout << "Strum scatter tests passed\n";
    return 0;
}
