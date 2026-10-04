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
    static std::array<std::uint32_t, 6> attackStates(const AcustraEngine& engine)
    {
        std::array<std::uint32_t, 6> result {};
        for (std::size_t string = 0; string < result.size(); ++string)
            result[string] = engine.voices_[string].randomState;
        return result;
    }
    static int delay(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].pluckDelay;
    }
};
}

namespace
{
using acustra::Performer;
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> open { 40, 45, 50, 55, 59, 64 };
int failures = 0;
void expect(bool passes, const char* description)
{
    if (!passes)
    {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}
std::vector<float> rapidPhrase(double rate, int frames, bool reverse, bool gather)
{
    auto performer = std::make_unique<Performer>();
    performer->prepare(rate, frames);
    performer->setGatherChords(gather);
    const int interval = static_cast<int>(std::lround(0.075 * rate));
    const int length = 5 * interval + Performer::gatherWindowSamples(rate);
    std::vector<float> left(static_cast<std::size_t>(length));
    std::vector<float> right(left.size());
    for (int start = 0; start < length; start += frames)
    {
        const int count = std::min(frames, length - start);
        performer->beginBlock(left.data() + start, right.data() + start, count);
        for (int stroke = 0; stroke < 5; ++stroke)
        {
            const int at = stroke * interval;
            if (at < start || at >= start + count)
                continue;
            for (std::size_t index = 0; index < open.size(); ++index)
                performer->noteOn(at - start, 1,
                    open[reverse ? open.size() - 1 - index : index], 16);
        }
        performer->endBlock();
    }
    expect(performer->latencySamples() == acustra::AcustraEngine::outputLatencySamples()
        + (gather ? Performer::gatherWindowSamples(rate) : 0),
        "rhythmic stroke fitting changed host latency");
    left.insert(left.end(), right.begin(), right.end());
    return left;
}
}

int main()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto performer = std::make_unique<Performer>();
        performer->prepare(rate, 64);
        const int interval = static_cast<int>(std::lround(0.075 * rate));
        std::array<float, 64> left {}, right {};
        // At this MIDI dynamic an unadapted full-neck stroke is slower than
        // the supplied 200 BPM sixteenth-note rhythm. Formerly, subsequent
        // strokes could replace far strings' attacks before reaching them.
        for (int stroke = 0; stroke < 5; ++stroke)
        {
            const auto before = Access::attackStates(performer->engine());
            performer->beginBlock(left.data(), right.data(), 1);
            for (const int note : open)
                performer->noteOn(0, 1, note, 16);
            performer->endBlock();
            int remaining = interval - 1;
            while (remaining > 0)
            {
                const int count = std::min(remaining, 64);
                performer->process(left.data(), right.data(), count);
                remaining -= count;
            }
            if (stroke == 0)
                continue; // The first gesture has no observed rhythm.
            const auto after = Access::attackStates(performer->engine());
            for (std::size_t string = 0; string < open.size(); ++string)
            {
                expect(before[string] != after[string],
                       "a repeated stroke never reached a physical string before the next stroke");
                expect(Access::delay(performer->engine(), static_cast<int>(string)) == 0,
                       "a repeated stroke left an attack waiting beyond its observed interval");
            }
        }

        // Panic establishes a fresh wrist gesture, even with no rendered
        // two-second rest. The next chord must begin low-to-high again.
        performer->reset();
        performer->beginBlock(left.data(), right.data(), 1);
        for (const int note : open)
            performer->noteOn(0, 1, note, 100);
        performer->endBlock();
        expect(Access::delay(performer->engine(), 0) == 0
                   && Access::delay(performer->engine(), 5) > 0,
               "reset resumed a stale upstroke instead of starting a downstroke");

        // Duplicate owners remain two physical pitches, with no automatic
        // stroke or delayed dyad attack. MIDI ownership remains balanced.
        auto dyad = std::make_unique<Performer>();
        dyad->prepare(rate, 64);
        dyad->beginBlock(left.data(), right.data(), 1);
        for (const int note : { 59, 64, 59 })
            dyad->noteOn(0, 1, note, 100);
        dyad->endBlock();
        expect(Access::delay(dyad->engine(), dyad->engine().heldString(59)) == 0
                   && Access::delay(dyad->engine(), dyad->engine().heldString(64)) == 0,
               "duplicate owners turned a dyad into a physical three-string strum");
        dyad->beginBlock(left.data(), right.data(), 1);
        dyad->noteOff(0, 1, 59);
        dyad->endBlock();
        expect(dyad->engine().heldString(59) >= 0,
               "duplicate dyad lost a MIDI owner");
        dyad->beginBlock(left.data(), right.data(), 1);
        dyad->noteOff(0, 1, 59);
        dyad->endBlock();
        expect(dyad->engine().heldString(59) < 0,
               "duplicate dyad retained a key after all owners released");

        for (const bool gather : { false, true })
        {
            const auto reference = rapidPhrase(rate, 1, false, gather);
            for (const int frames : { 64, 4096 })
                expect(rapidPhrase(rate, frames, true, gather) == reference,
                       "rhythmic stroke timing changed with insertion order or block partition");
        }
    }
    if (failures == 0)
        std::cout << "Repeated stroke traversal, reset, physical-string count and determinism passed.\n";
    return failures == 0 ? 0 : 1;
}
