// A plectrum cannot reach two strings at once. GuitarSet's picked two-string
// groups sound their strings a median 13 ms apart, only 5% within 2 ms, and
// mostly in the stroke's direction (Tools/MeasureTwoStringStrokes.py). Under
// Pick a two-note group is now a stroke across its two strings, swept and
// alternated as a strum; Finger and Thumb pairs, single notes and chords of
// three or more play exactly as before.
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
    static float sign(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckParallelSign; }
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

std::unique_ptr<Performer> player(bool twoStringStrokes,
                                  acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
                                  double rate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.twoStringStrokes = twoStringStrokes;
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

// One group every `spacing` samples, each held until just before the next.
std::vector<Event> groups(const std::vector<int>& notes, int count,
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

struct Pair
{
    int low = -1, high = -1;   // pluck delays of the lower and higher string
    float sign = 0.0f;         // the stroke's direction: +1 down, -1 up
};

// E2 and B2, the low E and A strings: a power chord.
std::vector<Pair> pairs(Performer& performer, int count, std::int64_t spacing,
                        const std::vector<int>& notes = { 40, 47 })
{
    std::vector<Pair> result;
    play(performer, groups(notes, count, spacing), count * spacing, 1, [&](std::int64_t now)
    {
        if ((now - 1) % spacing != 0)
            return;
        const int low = performer.engine().heldString(notes[0], 1);
        const int high = performer.engine().heldString(notes[1], 1);
        if (low < 0 || high < 0)
            return;
        result.push_back({ Access::pluckDelay(performer.engine(), low),
                           Access::pluckDelay(performer.engine(), high),
                           Access::sign(performer.engine(), low) });
    });
    return result;
}

void testPickedPairsAreSwept()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto swept = player(true, acustra::PickingTechnique::Pick, rate);
        auto together = player(false, acustra::PickingTechnique::Pick, rate);
        constexpr int count = 200;
        const auto spacing = static_cast<std::int64_t>(0.25 * rate);
        const auto on = pairs(*swept, count, spacing);
        const auto off = pairs(*together, count, spacing);
        expect(on.size() == static_cast<std::size_t>(count) && off.size() == on.size(),
               "a two-note group went missing");
        const double ms = 1000.0 / rate;
        const double place = ms * swept->engine().strumDelaySamples(1, 96.0f / 127.0f);
        int within = 0, alongDown = 0, downs = 0, alongUp = 0, ups = 0, sameDirection = 0;
        double gapSum = 0.0;
        for (std::size_t k = 0; k < std::min(on.size(), off.size()); ++k)
        {
            const auto& a = on[k];
            expect(std::min(a.low, a.high) == 0, "the stroke's first string did not sound at its note-on");
            expect(off[k].low == 0 && off[k].high == 0, "with the switch off a pair was not struck together");
            const double gap = ms * (a.high - a.low);   // the higher string's lag
            within += std::abs(gap) < 2.0 ? 1 : 0;
            const bool down = a.sign > 0.0f;
            (down ? downs : ups) += 1;
            (down ? alongDown : alongUp) += (down ? gap > 2.0 : gap < -2.0) ? 1 : 0;
            gapSum += down ? gap : -gap;
            sameDirection += a.sign == off[k].sign ? 1 : 0;
        }
        const double meanGap = gapSum / static_cast<double>(on.size());
        std::cout << rate << " Hz: within 2 ms " << within << " of " << on.size()
                  << ", lower first in " << alongDown << " of " << downs
                  << " downstrokes, higher first in " << alongUp << " of " << ups
                  << " upstrokes; mean gap along the stroke " << meanGap
                  << " ms against its place " << place << " ms\n";
        expect(within < static_cast<int>(on.size()) * 3 / 10,
               "picked pairs still sound together as often as not");
        expect(downs > count / 3 && ups > count / 3, "the pairs did not alternate down and up");
        expect(sameDirection == static_cast<int>(on.size()),
               "sweeping a pair changed its stroke direction");
        expect(alongDown > downs * 6 / 10 && alongUp > ups * 6 / 10,
               "a pair's strings did not mostly follow the stroke's direction");
        expect(std::abs(meanGap - place) < 1.5,
               "a pair's strings are not a stroke's place apart on average");
    }
}

void testCrossedStringIsMuted()
{
    // E2 and E3, an octave shape on the low E and D strings, crosses the A.
    for (const bool on : { true, false })
    {
        auto performer = player(on);
        int muted = 0;
        constexpr std::int64_t spacing = 12000;
        play(*performer, groups({ 40, 52 }, 8, spacing), 8 * spacing, 1, [&](std::int64_t now)
        {
            if ((now - 1) % spacing == 0)
                muted += Access::mutedStroke(performer->engine(), 1) ? 1 : 0;
        });
        expect(on ? muted == 8 : muted == 0,
               on ? "an octave shape's crossed string took no dead stroke"
                  : "a dead stroke without the two-string stroke");
    }
}

std::vector<float> render(bool on, acustra::PickingTechnique picking,
                          const std::vector<Event>& score, std::int64_t length, int block = 64)
{
    auto performer = player(on, picking);
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

void testOtherPlayingIsUnchanged()
{
    struct Case { acustra::PickingTechnique picking; std::vector<Event> score; const char* what; };
    const auto pairsScore = groups({ 45, 52 }, 8, 9000);
    std::vector<Event> melody;
    for (int i = 0; i < 12; ++i)
        melody.push_back({ i * 4000, 52 + (i * 5) % 17, 70 + i * 3 });
    const std::vector<Case> cases {
        { acustra::PickingTechnique::Finger, pairsScore, "finger pairs" },
        { acustra::PickingTechnique::Thumb, pairsScore, "thumb pairs" },
        { acustra::PickingTechnique::Pick, melody, "picked single notes" },
        { acustra::PickingTechnique::Pick, groups({ 40, 47, 52, 56, 59, 64 }, 6, 12000), "picked strums" } };
    for (const auto& c : cases)
    {
        const auto length = c.score.back().at + 24000;
        const bool same = render(true, c.picking, c.score, length)
            == render(false, c.picking, c.score, length);
        if (!same)
            std::cerr << "  changed: " << c.what << '\n';
        expect(same, "the two-string stroke changed playing that is not a picked pair");
    }
    const double change = difference(render(false, acustra::PickingTechnique::Pick, pairsScore, 96000),
                                     render(true, acustra::PickingTechnique::Pick, pairsScore, 96000));
    std::cout << "Swept picked pairs against struck together: normalised difference " << change << '\n';
    expect(change > 0.05, "sweeping picked pairs made no audible difference");
}

void testContinuedStroke()
{
    // A pair 10 ms after a picked note is the same stroke still travelling:
    // it takes that note's direction, as it did before it was swept.
    for (const bool on : { true, false })
    {
        auto performer = player(on);
        std::vector<Event> score;
        for (int i = 0; i < 6; ++i)
        {
            const std::int64_t at = i * 14400;
            score.push_back({ at, 64, 90 });
            score.push_back({ at + 480, 40, 90 });
            score.push_back({ at + 480, 47, 90 });
        }
        int agreeing = 0, checked = 0;
        float noteSign = 0.0f;
        play(*performer, score, 6 * 14400, 1, [&](std::int64_t now)
        {
            const auto phase = (now - 1) % 14400;
            if (phase == 0)
                noteSign = Access::sign(performer->engine(), 5);
            if (phase == 480)
            {
                ++checked;
                agreeing += Access::sign(performer->engine(), 0) == noteSign ? 1 : 0;
            }
        });
        expect(checked == 6 && agreeing == checked,
               "a pair inside the chord window did not continue the picked note's stroke");
    }
}

void testBlocks()
{
    const auto score = groups({ 43, 50 }, 12, 7000, 100);
    const auto reference = render(true, acustra::PickingTechnique::Pick, score, 90000, 1);
    for (const int block : { 61, 64, 512 })
        expect(render(true, acustra::PickingTechnique::Pick, score, 90000, block) == reference,
               "the swept pairs depend on the block size");
}
}

int main()
{
    testPickedPairsAreSwept();
    testCrossedStringIsMuted();
    testOtherPlayingIsUnchanged();
    testContinuedStroke();
    testBlocks();
    if (failures != 0)
    {
        std::cerr << failures << " two-string stroke check(s) failed\n";
        return 1;
    }
    std::cout << "Two-string stroke tests passed\n";
    return 0;
}
