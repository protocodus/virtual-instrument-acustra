// Inside one picked stroke the pick strikes the strings it enters and leaves
// the chord on more lightly than those between them, in either direction:
// on GuitarSet's hex channels a mean 1.8 dB under for the entry string and
// 2.0 dB for the exit string, and what is left of the upstroke's tilt across
// the strings is 0.30 dB a string, not the 0.9 that comparing whole takes'
// upstrokes with their downstrokes gave (Tools/MeasureStrokeEdges.py).
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
    // The static force the released string gives back at the saddle, which
    // is linear in the displacement the hand let go.
    static float releasedForce(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].releaseStepRise; }
    // A scheduled pluck's release has been taken into the step's first
    // sample by the time a block returns: the step force after that sample
    // (the earlier step long run out) is the same rise at the same port.
    static float firstStepForce(const AcustraEngine& e, int string)
    {
        const auto& voice = e.voices_[static_cast<std::size_t>(string)];
        return voice.releaseStepAge == 1 ? voice.releaseStepForce : 0.0f;
    }
};
}

namespace
{
using acustra::Performer;
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
constexpr double entryGain = 0.813, exitGain = 0.794;
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

std::unique_ptr<acustra::AcustraEngine> engine(bool edges, double rate,
                                               acustra::PickingTechnique picking)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.strokeEdges = edges;
    e->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    e->setParameters(parameters);
    e->prepare(rate, 64);
    e->setStringPerChannelMode(true);
    return e;
}

void testEdgesAndTilt()
{
    // Two engines that differ only in the switch draw the same stroke, so
    // the ratio of their released forces is the change alone, exactly.
    double worst = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto picking : { acustra::PickingTechnique::Finger,
                                    acustra::PickingTechnique::Pick,
                                    acustra::PickingTechnique::Thumb })
            for (const bool upstroke : { false, true })
            {
                auto edged = engine(true, rate, picking);
                auto plain = engine(false, rate, picking);
                const int entry = upstroke ? 5 : 0, exit = upstroke ? 0 : 5;
                edged->beginStrum(0, 0, upstroke, entry, exit);
                plain->beginStrum(0, 0, upstroke, entry, exit);
                for (int string = 0; string < 6; ++string)
                {
                    edged->noteOn(open[static_cast<std::size_t>(string)] + 3, 0.7f,
                                  string + 1, 0, true);
                    plain->noteOn(open[static_cast<std::size_t>(string)] + 3, 0.7f,
                                  string + 1, 0, true);
                }
                for (int string = 0; string < 6; ++string)
                {
                    const double ratio = Access::releasedForce(*edged, string)
                        / Access::releasedForce(*plain, string);
                    const double edge = string == entry ? entryGain
                                      : string == exit ? exitGain : 1.0;
                    // The switch off keeps the 1.4.0 line, 0.9 dB a string.
                    const double tilt = upstroke
                        ? std::pow(10.0, (0.30 - 0.9) * (string - 2.5) / 20.0) : 1.0;
                    if (edge == 1.0 && !upstroke)
                        expect(Access::releasedForce(*edged, string)
                                   == Access::releasedForce(*plain, string),
                               "a downstroke's inner string changed");
                    worst = std::max(worst, std::abs(ratio / (edge * tilt) - 1.0));
                }
            }
    std::cout << "edges and tilt: worst relative error " << worst << '\n';
    expect(worst < 1.0e-5, "a string's release is not its edge's and the stroke's own tilt");

    // With no entry or exit named, nothing is an edge.
    auto edged = engine(true, 48000.0, acustra::PickingTechnique::Pick);
    auto plain = engine(false, 48000.0, acustra::PickingTechnique::Pick);
    edged->beginStrum(0, 0, false);
    plain->beginStrum(0, 0, false);
    for (int string = 0; string < 6; ++string)
    {
        edged->noteOn(open[static_cast<std::size_t>(string)] + 2, 0.7f, string + 1, 0, true);
        plain->noteOn(open[static_cast<std::size_t>(string)] + 2, 0.7f, string + 1, 0, true);
    }
    bool same = true;
    for (int string = 0; string < 6; ++string)
        same &= Access::releasedForce(*edged, string) == Access::releasedForce(*plain, string);
    expect(same, "a downstroke with no named edges changed");
}

std::unique_ptr<Performer> player(bool edges, acustra::PickingTechnique picking)
{
    auto performer = std::make_unique<Performer>();
    acustra::PerformanceRealism realism;
    realism.strokeEdges = edges;
    performer->engine().setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    performer->setParameters(parameters);
    performer->prepare(48000.0, 512);
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

// Each stroke's released forces, caught on each pluck's first sample, with
// the switch on over off. The strokes are 1.2 s apart with the keys let go
// after 0.2 s, so every stroke strikes strings at rest (a ringing string's
// own motion would join the release); at 30 BPM they still alternate.
std::vector<std::array<double, 6>> ratios(const std::vector<int>& notes, int count,
                                          acustra::PickingTechnique picking)
{
    constexpr std::int64_t spacing = 57600;
    std::vector<Event> score;
    for (int c = 0; c < count; ++c)
        for (int note : notes)
        {
            score.push_back({ c * spacing, note, 96 });
            score.push_back({ c * spacing + 9600, note, 0 });
        }
    std::stable_sort(score.begin(), score.end(),
        [](const Event& a, const Event& b) { return a.at < b.at; });
    std::array<std::vector<std::array<double, 6>>, 2> forces;
    for (const bool on : { true, false })
    {
        auto performer = player(on, picking);
        performer->setTempoBpm(30.0);
        auto& strokes = forces[on ? 0 : 1];
        strokes.assign(static_cast<std::size_t>(count), std::array<double, 6> {});
        play(*performer, score, count * spacing, 1, [&](std::int64_t now)
        {
            const auto stroke = static_cast<std::size_t>((now - 1) / spacing);
            for (int string = 0; string < 6; ++string)
            {
                const float force = Access::firstStepForce(performer->engine(), string);
                if (force != 0.0f)
                    strokes[stroke][static_cast<std::size_t>(string)] = force;
            }
        });
    }
    std::vector<std::array<double, 6>> result;
    for (std::size_t k = 0; k < forces[0].size(); ++k)
    {
        std::array<double, 6> r {};
        for (std::size_t s = 0; s < 6; ++s)
            r[s] = forces[1][k][s] != 0.0 ? forces[0][k][s] / forces[1][k][s] : 1.0;
        result.push_back(r);
    }
    return result;
}

void testThePerformerNamesTheEdges()
{
    // D major on the top four strings, down then up: the D string and the
    // high E are the stroke's ends, the other two strings inside it.
    const auto d = ratios({ 50, 57, 62, 66 }, 6, acustra::PickingTechnique::Pick);
    expect(d.size() == 6, "a stroke went missing");
    for (std::size_t k = 0; k < d.size(); ++k)
    {
        const bool up = k % 2 == 1;
        const auto tilt = [&](int string)
        { return up ? std::pow(10.0, (0.30 - 0.9) * (string - 2.5) / 20.0) : 1.0; };
        // Strings at rest, but the open strings and those handed back keep
        // a trace of the bridge's motion: within 0.2%, against edges 18 and
        // 21% lighter.
        const auto near = [](double a, double b) { return std::abs(a / b - 1.0) < 2.0e-3; };
        expect(near(d[k][2], (up ? exitGain : entryGain) * tilt(2))
                   && near(d[k][5], (up ? entryGain : exitGain) * tilt(5)),
               "a stroke's end strings were not struck as its entry and exit");
        expect(near(d[k][3], tilt(3)) && near(d[k][4], tilt(4)),
               "a string inside the stroke was struck as an edge");
    }
    // E2, B3 and E4 cross the A, D and G strings, which take dead strokes
    // and are inside the stroke; the low and high E are its ends.
    const auto gap = ratios({ 40, 59, 64 }, 4, acustra::PickingTechnique::Pick);
    for (std::size_t k = 0; k < gap.size(); ++k)
    {
        const bool up = k % 2 == 1;
        const auto tilt = [&](int string)
        { return up ? std::pow(10.0, (0.30 - 0.9) * (string - 2.5) / 20.0) : 1.0; };
        for (int string = 1; string <= 3; ++string)
            expect(std::abs(gap[k][static_cast<std::size_t>(string)] / tilt(string) - 1.0) < 2.0e-3,
                   "a dead stroke inside the stroke was struck as an edge");
        expect(std::abs(gap[k][0] / ((up ? exitGain : entryGain) * tilt(0)) - 1.0) < 2.0e-3,
               "the low E was not the stroke's end");
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

void testUnchangedPlaying()
{
    struct Case { acustra::PickingTechnique picking; std::vector<Event> score; const char* what; };
    std::vector<Event> melody;
    for (int i = 0; i < 12; ++i)
        melody.push_back({ i * 4000, 52 + (i * 5) % 17, 70 + i * 3 });
    // Half a second apart every stroke is a downstroke, which no tilt
    // touches: a pair and a group the hand cannot plan have no edges.
    const std::vector<Case> cases {
        { acustra::PickingTechnique::Pick, melody, "picked single notes" },
        { acustra::PickingTechnique::Pick, chords({ 40, 47 }, 6, 24000), "picked pairs" },
        { acustra::PickingTechnique::Finger, chords({ 55, 59, 62, 67 }, 6, 12000), "a finger-plucked chord" },
        { acustra::PickingTechnique::Pick, chords({ 40, 45, 50, 55, 59, 64, 69 }, 4, 24000),
          "a seven-note group the hand cannot plan" } };
    for (const auto& c : cases)
    {
        const auto length = c.score.back().at + 24000;
        const bool same = render(true, c.picking, c.score, length)
            == render(false, c.picking, c.score, length);
        if (!same)
            std::cerr << "  changed: " << c.what << '\n';
        expect(same, "the stroke's edges changed playing that has none");
    }
    const auto strums = chords({ 40, 47, 52, 56, 59, 64 }, 6, 12000);
    const double change = difference(render(false, acustra::PickingTechnique::Pick, strums, 96000),
                                     render(true, acustra::PickingTechnique::Pick, strums, 96000));
    std::cout << "Strums with lighter edges against without: normalised difference " << change << '\n';
    expect(change > 0.02, "the stroke's edges made no audible difference");
}

void testBlocks()
{
    const auto score = chords({ 43, 47, 50, 55, 59, 67 }, 8, 9000, 100);
    const auto reference = render(true, acustra::PickingTechnique::Pick, score, 80000, 1);
    for (const int block : { 61, 64, 512 })
        expect(render(true, acustra::PickingTechnique::Pick, score, 80000, block) == reference,
               "the stroke's edges depend on the block size");
}
}

int main()
{
    testEdgesAndTilt();
    testThePerformerNamesTheEdges();
    testUnchangedPlaying();
    testBlocks();
    if (failures != 0)
    {
        std::cerr << failures << " stroke edge check(s) failed\n";
        return 1;
    }
    std::cout << "Stroke edge tests passed\n";
    return 0;
}
