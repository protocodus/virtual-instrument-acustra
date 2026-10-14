// A stroke's span grows as the 0.64 power of the spacings it crosses, so a
// small stroke crosses each string more slowly than a full one: within each
// of GuitarSet's picked takes a two-string stroke takes 1.48 times as long a
// string as the take's four-string strokes and a six-string one 0.85 times
// (Tools/MeasureStrokeSpan.py). The six-string stroke keeps its speed.
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

void testLaw()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const bool on : { true, false })
        {
            auto engine = std::make_unique<acustra::AcustraEngine>();
            acustra::PerformanceRealism realism;
            realism.strokeSpanLaw = on;
            engine->setPerformanceRealism(realism);
            engine->prepare(rate, 64);
            for (const float velocity : { 0.1f, 0.5f, 0.756f, 1.0f })
            {
                const int full = engine->strumDelaySamples(5, velocity);
                expect(engine->strumDelaySamples(5, velocity, 5) == full,
                       "the six-string stroke's span moved");
                for (int gaps = 1; gaps <= 5; ++gaps)
                {
                    const double expected = on ? full * std::pow(gaps / 5.0, 0.64) : full * gaps / 5.0;
                    expect(std::abs(engine->strumDelaySamples(gaps, velocity, gaps) - expected) <= 1.0,
                           on ? "a stroke's span is not the six-string span times (gaps / 5)^0.64"
                              : "with the switch off a stroke's span is not in proportion to its gaps");
                    // Inside one stroke the strings stay evenly spaced in time.
                    const int one = engine->strumDelaySamples(1, velocity, gaps);
                    const int span = engine->strumDelaySamples(gaps, velocity, gaps);
                    expect(std::abs(span - gaps * one) <= gaps,
                           "a stroke's own strings are not evenly spaced");
                }
            }
        }
}

std::unique_ptr<Performer> player(bool on, acustra::PickingTechnique picking,
                                  bool scatter = false, double rate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.strokeSpanLaw = on;
    realism.strumScatter = scatter;
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

// The open strings from the high E down: the top two, three ... six.
std::vector<int> topStrings(int count)
{
    constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
    return std::vector<int>(open.end() - count, open.end());
}

void testStrokesThroughThePerformer()
{
    // Unscattered, so each stroke's span is its sweep's: its speed draw
    // times the law's span for its strings.
    std::array<double, 7> timeAString {};
    for (int strings = 2; strings <= 6; ++strings)
    {
        auto performer = player(true, acustra::PickingTechnique::Pick);
        const auto notes = topStrings(strings);
        constexpr std::int64_t spacing = 12000;
        constexpr int count = 40;
        const int nominal = performer->engine().strumDelaySamples(strings - 1, 96.0f / 127.0f,
                                                                  strings - 1);
        double sum = 0.0;
        int strokes = 0;
        play(*performer, chords(notes, count, spacing), count * spacing, 1, [&](std::int64_t now)
        {
            if ((now - 1) % spacing != 0)
                return;
            int span = 0;
            for (int string = 6 - strings; string < 6; ++string)
                span = std::max(span, Access::pluckDelay(performer->engine(), string));
            const float scale = performer->engine().strumSpeedScale();
            expect(std::abs(span - std::lround(nominal * scale)) <= 1,
                   "a stroke's span is not its speed draw times the law's span");
            sum += span / static_cast<double>(scale) / (strings - 1) * 1000.0 / 48000.0;
            ++strokes;
        });
        expect(strokes == count, "a stroke went missing");
        timeAString[static_cast<std::size_t>(strings)] = sum / strokes;
    }
    std::cout << "Time a string, unscattered, by stroke size at MIDI 96:";
    for (int strings = 2; strings <= 6; ++strings)
        std::cout << ' ' << strings << ": " << timeAString[static_cast<std::size_t>(strings)] << " ms";
    std::cout << '\n';
    for (int strings = 2; strings <= 5; ++strings)
        expect(std::abs(timeAString[static_cast<std::size_t>(strings)] / timeAString[6]
                        - std::pow(5.0 / (strings - 1), 0.36)) < 0.02,
               "a smaller stroke does not cross each string (5 / gaps)^0.36 times more slowly");
}

std::vector<float> render(bool on, acustra::PickingTechnique picking,
                          const std::vector<Event>& score, std::int64_t length, int block = 64)
{
    auto performer = player(on, picking, true);
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

void testUnchangedPlaying()
{
    struct Case { acustra::PickingTechnique picking; std::vector<Event> score; const char* what; };
    std::vector<Event> melody;
    for (int i = 0; i < 12; ++i)
        melody.push_back({ i * 4000, 52 + (i * 5) % 17, 70 + i * 3 });
    const std::vector<Case> cases {
        { acustra::PickingTechnique::Pick, chords(topStrings(6), 6, 12000), "six-string strums" },
        { acustra::PickingTechnique::Pick, chords({ 43, 47, 50, 55, 59, 67 }, 6, 12000), "G major" },
        { acustra::PickingTechnique::Finger, chords({ 55, 59, 62, 67 }, 6, 12000), "a finger-plucked chord" },
        { acustra::PickingTechnique::Finger, chords({ 45, 52 }, 6, 12000), "finger pairs" },
        { acustra::PickingTechnique::Pick, melody, "picked single notes" } };
    for (const auto& c : cases)
    {
        const auto length = c.score.back().at + 24000;
        const bool same = render(true, c.picking, c.score, length)
            == render(false, c.picking, c.score, length);
        if (!same)
            std::cerr << "  changed: " << c.what << '\n';
        expect(same, "the span law changed playing whose strokes cross all six strings or none");
    }
    const auto small = chords({ 52, 55, 59 }, 6, 12000);
    const double change = difference(render(false, acustra::PickingTechnique::Pick, small, 96000),
                                     render(true, acustra::PickingTechnique::Pick, small, 96000));
    std::cout << "Three-string strums under the law against in proportion: normalised difference "
              << change << '\n';
    expect(change > 0.05, "the span law did not change a three-string strum audibly");
}

void testBlocks()
{
    const auto score = chords({ 50, 55, 59, 64 }, 10, 7000, 100);
    const auto reference = render(true, acustra::PickingTechnique::Pick, score, 80000, 1);
    for (const int block : { 61, 64, 512 })
        expect(render(true, acustra::PickingTechnique::Pick, score, 80000, block) == reference,
               "the span law depends on the block size");
}
}

int main()
{
    testLaw();
    testStrokesThroughThePerformer();
    testUnchangedPlaying();
    testBlocks();
    if (failures != 0)
    {
        std::cerr << failures << " stroke span check(s) failed\n";
        return 1;
    }
    std::cout << "Stroke span tests passed\n";
    return 0;
}
