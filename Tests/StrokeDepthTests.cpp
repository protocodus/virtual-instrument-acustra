// An upstroke enters the strings from the treble side: on GuitarSet's hex
// pickup its attacks rise 0.9 dB per string toward the high E against the
// same strings' downstroke attacks and sit 1.2 dB under them mid-neck
// (Tools/MeasureStrumDirection.py). The downstroke is the reference and
// stays exactly as it was.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
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
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

std::unique_ptr<acustra::AcustraEngine> engine(bool depth, double rate,
    acustra::PickingTechnique picking = acustra::PickingTechnique::Finger)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.strokeDepth = depth;
    // The 1.4.0 line across the strings; its refit with the stroke's lighter
    // edges, inside one stroke, is Acustra.StrokeEdges's.
    realism.strokeEdges = false;
    e->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    e->setParameters(parameters);
    e->prepare(rate, 64);
    e->setStringPerChannelMode(true);
    return e;
}

void testUpstrokeTiltPerString()
{
    // Two engines that differ only in the switch draw the same stroke, so
    // the ratio of their released forces is the tilt alone, exactly.
    double worst = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto picking : { acustra::PickingTechnique::Finger,
                                    acustra::PickingTechnique::Pick,
                                    acustra::PickingTechnique::Thumb })
            for (const bool upstroke : { false, true })
            {
                auto tilted = engine(true, rate, picking);
                auto level = engine(false, rate, picking);
                tilted->beginStrum(0, 0, upstroke);
                level->beginStrum(0, 0, upstroke);
                for (int string = 0; string < 6; ++string)
                {
                    tilted->noteOn(open[static_cast<std::size_t>(string)] + 3, 0.7f,
                                   string + 1, 0, true);
                    level->noteOn(open[static_cast<std::size_t>(string)] + 3, 0.7f,
                                  string + 1, 0, true);
                }
                for (int string = 0; string < 6; ++string)
                {
                    const double ratio = Access::releasedForce(*tilted, string)
                        / Access::releasedForce(*level, string);
                    const double expected = upstroke
                        ? std::pow(10.0, (-1.2 + 0.9 * (string - 2.5)) / 20.0) : 1.0;
                    if (!upstroke)
                        expect(Access::releasedForce(*tilted, string)
                                   == Access::releasedForce(*level, string),
                               "a downstroke string's release changed with the stroke depth");
                    worst = std::max(worst, std::abs(ratio / expected - 1.0));
                }
            }
    std::cout << "upstroke tilt: worst relative error " << worst << '\n';
    expect(worst < 1.0e-5, "an upstroke string's release is not the measured up-minus-down line");
}

void testSingleNotesAreNotTilted()
{
    // A single picked upstroke reaches its one string as aimed: only a
    // strum's sweep across the strings is tilted.
    for (const bool upstroke : { false, true })
    {
        auto tilted = engine(true, 48000.0, acustra::PickingTechnique::Pick);
        auto level = engine(false, 48000.0, acustra::PickingTechnique::Pick);
        tilted->noteOn(43, 0.7f, 1, 0, false, upstroke);
        level->noteOn(43, 0.7f, 1, 0, false, upstroke);
        expect(Access::releasedForce(*tilted, 0) == Access::releasedForce(*level, 0),
               "a single note was tilted as if it were a strum");
    }
}

std::vector<float> render(acustra::Performer& performer,
                          const std::vector<std::array<int, 3>>& score, int length)
{
    // score: { sample, note, velocity (0 = key-up) }
    std::vector<float> out(static_cast<std::size_t>(2 * length));
    std::array<float, 64> left {}, right {};
    std::size_t next = 0;
    for (int start = 0; start < length; start += 64)
    {
        const int count = std::min(64, length - start);
        performer.beginBlock(left.data(), right.data(), count);
        while (next < score.size() && score[next][0] < start + count)
        {
            const auto& event = score[next++];
            if (event[2] > 0)
                performer.noteOn(event[0] - start, 1, event[1], event[2]);
            else
                performer.noteOff(event[0] - start, 1, event[1]);
        }
        performer.endBlock();
        for (int i = 0; i < count; ++i)
        {
            out[static_cast<std::size_t>(2 * (start + i))] = left[static_cast<std::size_t>(i)];
            out[static_cast<std::size_t>(2 * (start + i) + 1)] = right[static_cast<std::size_t>(i)];
        }
    }
    return out;
}

std::unique_ptr<acustra::Performer> player(bool depth)
{
    auto p = std::make_unique<acustra::Performer>();
    acustra::PerformanceRealism realism;
    realism.strokeDepth = depth;
    realism.strokeEdges = false;
    p->engine().setPerformanceRealism(realism);
    p->prepare(48000.0, 64);
    p->setTempoBpm(120.0);
    return p;
}

// E major strummed: one bar of quarter notes (all downstrokes), then down
// and up eighths, as the Performer times and directs them.
std::vector<std::array<int, 3>> eMajor(bool eighths, int start, int bars)
{
    std::vector<std::array<int, 3>> score;
    const int spacing = eighths ? 12000 : 24000;
    for (int stroke = 0; stroke < bars * (eighths ? 8 : 4); ++stroke)
    {
        const int at = start + stroke * spacing;
        for (int note : { 40, 47, 52, 56, 59, 64 })
        {
            score.push_back({ at, note, 92 });
            score.push_back({ at + spacing - 300, note, 0 });
        }
    }
    std::stable_sort(score.begin(), score.end(),
        [](const auto& a, const auto& b) { return a[0] < b[0]; });
    return score;
}

void testDownstrokesAreUnchangedAndUpstrokesLeanTreble()
{
    const auto quarters = eMajor(false, 0, 2);
    auto on = player(true), off = player(false);
    expect(render(*on, quarters, 9 * 24000) == render(*off, quarters, 9 * 24000),
           "downstroke strums changed with the stroke depth");

    // Down-up eighths: the first downstroke is identical; every upstroke
    // carries less of its energy in the bass band than in the treble, and
    // is never louder than it was, by more than a quarter decibel, nor more
    // than two decibels quieter.
    const auto eighths = eMajor(true, 0, 1);
    auto tiltedPlayer = player(true), levelPlayer = player(false);
    const auto tilted = render(*tiltedPlayer, eighths, 8 * 12000 + 24000);
    const auto level = render(*levelPlayer, eighths, 8 * 12000 + 24000);
    expect(std::equal(tilted.begin(), tilted.begin() + 2 * 12000, level.begin()),
           "the first downstroke of a down-up pattern changed");
    // Low-band and high-band energy over the upstrokes' first 60 ms.
    const auto bands = [](const std::vector<float>& audio, int from, int count)
    {
        double low = 0.0, high = 0.0, lowState = 0.0, previous = 0.0;
        const double pole = std::exp(-2.0 * 3.141592653589793 * 250.0 / 48000.0);
        for (int n = from; n < from + count; ++n)
        {
            const double x = 0.5 * (audio[static_cast<std::size_t>(2 * n)]
                                    + audio[static_cast<std::size_t>(2 * n + 1)]);
            lowState = (1.0 - pole) * x + pole * lowState;
            const double highPart = x - previous;
            previous = x;
            low += lowState * lowState;
            high += highPart * highPart;
        }
        return std::array<double, 2> { low, high };
    };
    double lowRatio = 0.0, highRatio = 0.0, wholeTilted = 0.0, wholeLevel = 0.0;
    for (int stroke = 1; stroke < 8; stroke += 2)
    {
        const auto t = bands(tilted, stroke * 12000, 2880);
        const auto l = bands(level, stroke * 12000, 2880);
        lowRatio += t[0] / l[0] / 4.0;
        highRatio += t[1] / l[1] / 4.0;
        for (int n = stroke * 12000; n < (stroke + 1) * 12000; ++n)
        {
            wholeTilted += tilted[static_cast<std::size_t>(2 * n)] * tilted[static_cast<std::size_t>(2 * n)];
            wholeLevel += level[static_cast<std::size_t>(2 * n)] * level[static_cast<std::size_t>(2 * n)];
        }
    }
    std::cout << "upstrokes: low band " << 10.0 * std::log10(lowRatio)
              << " dB, high band " << 10.0 * std::log10(highRatio)
              << " dB, whole " << 10.0 * std::log10(wholeTilted / wholeLevel) << " dB\n";
    expect(highRatio > lowRatio,
           "upstrokes did not move their attack from the bass toward the treble");
    const double whole = 10.0 * std::log10(wholeTilted / wholeLevel);
    expect(whole < 0.25 && whole > -2.0,
           "an upstroke grew louder, or lost more than two decibels");
}
} // namespace

int main()
{
    testUpstrokeTiltPerString();
    testSingleNotesAreNotTilted();
    testDownstrokesAreUnchangedAndUpstrokesLeanTreble();
    if (failures != 0)
    {
        std::cerr << failures << " stroke depth check(s) failed\n";
        return 1;
    }
    std::cout << "stroke depth checks passed\n";
    return 0;
}
