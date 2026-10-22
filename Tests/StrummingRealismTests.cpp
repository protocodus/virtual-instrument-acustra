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
    static float strokeScale(const AcustraEngine& engine)
    {
        return engine.strumSpeedScale_;
    }
    static float strokeSign(const AcustraEngine& engine)
    {
        return engine.strumParallelSign_;
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

void testContinuousWristSweep(double rate)
{
    auto full = std::make_unique<Performer>();
    auto sparse = std::make_unique<Performer>();
    // The sweep itself: the sparse three-string chord under Finger would
    // otherwise be plucked together (PerformanceRealism::fingerChords), and
    // each string would sound at its own moment about the sweep
    // (strumScatter).
    acustra::PerformanceRealism sweep;
    sweep.fingerChords = false;
    sweep.strumScatter = false;
    full->engine().setPerformanceRealism(sweep);
    sparse->engine().setPerformanceRealism(sweep);
    full->prepare(rate, 64);
    sparse->prepare(rate, 64);
    std::array<float, 64> left {}, right {};
    const auto strike = [&](Performer& performer, bool skipMiddle)
    {
        performer.beginBlock(left.data(), right.data(), 1);
        for (std::size_t string = 0; string < open.size(); ++string)
            if (!skipMiddle || string == 0 || string >= 4)
                performer.noteOn(0, 1, open[string], 32);
        performer.endBlock();
        std::array<int, 6> arrivals {};
        for (std::size_t string = 0; string < open.size(); ++string)
        {
            const int pending = Access::delay(performer.engine(), static_cast<int>(string));
            // noteOn queues delay + 1 so the first rendered sample consumes
            // that scheduling guard; what remains is the intended delay.
            arrivals[string] = pending;
        }
        return arrivals;
    };
    for (const bool upstroke : { false, true })
    {
        const auto arrivals = strike(*full, false);
        const auto skipped = strike(*sparse, true);
        const int expectedSpan = static_cast<int>(std::round(
            full->engine().strumDelaySamples(5, 32.0f / 127.0f)
                * Access::strokeScale(full->engine())));
        const std::size_t start = upstroke ? 5 : 0;
        const std::size_t finish = upstroke ? 0 : 5;
        expect(arrivals[start] == 0 && arrivals[finish] == expectedSpan,
               "curved wrist moved the first onset or total traversal");
        std::array<int, 5> gaps {};
        for (std::size_t gap = 0; gap < gaps.size(); ++gap)
            gaps[gap] = std::abs(arrivals[gap + 1] - arrivals[gap]);
        expect(gaps[0] > gaps[1] && gaps[1] > gaps[2]
                   && gaps[4] > gaps[3] && gaps[3] > gaps[2],
               "wrist sweep did not accelerate into and decelerate out of the strings");
        expect(std::abs(gaps[0] - gaps[4]) <= 2
                   && std::abs(gaps[1] - gaps[3]) <= 2,
               "the symmetric wrist path acquired asymmetric independent timing");
        expect(gaps[0] < 1.2f * gaps[2] && gaps[4] < 1.2f * gaps[2],
               "curved wrist produced an excessive edge slowdown");
        for (const std::size_t string : { 0u, 4u, 5u })
            expect(skipped[string] == arrivals[string],
                   "skipping strings shortened the wrist's physical travel");

        // A direction change follows the same path in reverse, after the
        // old stroke has finished. Its independent shared speed may differ.
        int remaining = static_cast<int>(std::lround(0.2 * rate));
        while (remaining > 0)
        {
            const int count = std::min(remaining, 64);
            full->process(left.data(), right.data(), count);
            sparse->process(left.data(), right.data(), count);
            remaining -= count;
        }
    }
}

void testSweepChangesAudio()
{
    auto performer = std::make_unique<Performer>();
    auto linear = std::make_unique<acustra::AcustraEngine>();
    constexpr double rate = 48000.0;
    constexpr int frames = 7200;
    constexpr float velocity = 32.0f / 127.0f;
    // Isolate the wrist curve: the Performer otherwise adds independent
    // scatter and edge gains which this manual linear scheduler omits.
    acustra::PerformanceRealism timingOnly;
    timingOnly.strumScatter = false;
    timingOnly.strokeEdges = false;
    performer->engine().setPerformanceRealism(timingOnly);
    linear->setPerformanceRealism(timingOnly);
    performer->prepare(rate, frames);
    linear->prepare(rate, frames);
    std::array<std::vector<float>, 4> audio;
    for (auto& channel : audio)
        channel.resize(frames);
    performer->beginBlock(audio[0].data(), audio[1].data(), frames);
    for (const int note : open)
        performer->noteOn(0, 1, note, 32);
    performer->endBlock();

    // The old constant-speed scheduler, using the same engine and draw
    // sequence, is a signal witness for the timing change alone.
    const std::array<int, 6> descending { 64, 59, 55, 50, 45, 40 };
    linear->planChord(descending.data(), 6);
    linear->beginStrum(linear->strumDelaySamples(5, velocity), 0, false);
    for (std::size_t index = 0; index < descending.size(); ++index)
    {
        const int string = 5 - static_cast<int>(index);
        linear->noteOn(descending[index], velocity, 1,
                       linear->strumDelaySamples(string, velocity), true);
    }
    linear->process(audio[2].data(), audio[3].data(), frames);
    double referenceEnergy = 0.0;
    double differenceEnergy = 0.0;
    for (int frame = 0; frame < frames; ++frame)
        for (const int channel : { 0, 1 })
        {
            const float candidate = audio[static_cast<std::size_t>(channel)][frame];
            const float reference = audio[static_cast<std::size_t>(channel + 2)][frame];
            expect(std::isfinite(candidate) && std::abs(candidate) < 2.0f,
                   "curved wrist rendered nonfinite or unbounded audio");
            referenceEnergy += reference * reference;
            const double difference = static_cast<double>(candidate) - reference;
            differenceEnergy += difference * difference;
        }
    expect(referenceEnergy > 1.0e-8 && differenceEnergy > 0.0001 * referenceEnergy,
           "curved wrist produced no material audio difference from uniform string timing");
    std::cout << "Curved/linear strum normalized signal difference: "
              << std::sqrt(differenceEnergy / std::max(referenceEnergy, 1.0e-20)) << '\n';
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

void testFixedVelocityRepeatedStrokeLevels()
{
    // Render real repeated chords through the MIDI performer. Equal command
    // velocity must not manufacture large random accents. Ringing waves and
    // alternating stroke direction legitimately change the resulting level,
    // so measure a broad set of adjacent RMS changes rather than flattening
    // individual strokes or requiring identical audio.
    constexpr int rate = 48000;
    constexpr int block = 128;
    constexpr int repeats = 12;
    constexpr int start = rate / 10;
    constexpr std::array<std::array<int, 6>, 2> shapes {{
        {{ 40, 47, 52, 56, 59, 64 }},
        {{ 48, 52, 55, 60, 64, 67 }}
    }};
    std::vector<double> increases;
    bool finite = true;
    for (const auto picking : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Pick,
                                acustra::PickingTechnique::Thumb })
        for (const auto& shape : shapes)
            for (const auto direction : { Performer::StrokeMode::Automatic,
                                          Performer::StrokeMode::Down })
                for (const int velocity : { 64, 100 })
                    for (const double seconds : { 0.15, 0.3, 0.6 })
                    {
                        auto performer = std::make_unique<Performer>();
                        acustra::EngineParameters parameters;
                        parameters.picking = picking;
                        parameters.room = 0.0f;
                        parameters.detune = 0.0f;
                        performer->setParameters(parameters);
                        performer->prepare(rate, block);
                        performer->setGatherChords(false);
                        performer->setStrokeMode(direction);
                        const int interval = static_cast<int>(std::lround(seconds * rate));
                        const int samples = start + repeats * interval;
                        std::vector<float> left(static_cast<std::size_t>(samples));
                        std::vector<float> right(left.size());
                        for (int at = 0; at < samples; at += block)
                        {
                            const int count = std::min(block, samples - at);
                            performer->beginBlock(left.data() + at, right.data() + at, count);
                            for (int stroke = 0; stroke < repeats; ++stroke)
                            {
                                const int on = start + stroke * interval;
                                const int off = on + interval - static_cast<int>(0.008 * rate);
                                if (on >= at && on < at + count)
                                    for (const int note : shape)
                                        performer->noteOn(on - at, 1, note, velocity);
                                if (off >= at && off < at + count)
                                    for (const int note : shape)
                                        performer->noteOff(off - at, 1, note, 64);
                            }
                            performer->endBlock();
                        }
                        const int window = std::min(interval, rate / 4);
                        double previousDb = 0.0;
                        for (int stroke = 0; stroke < repeats; ++stroke)
                        {
                            double energy = 0.0;
                            const int begin = start + stroke * interval;
                            for (int sample = begin; sample < begin + window; ++sample)
                            {
                                const double l = left[static_cast<std::size_t>(sample)];
                                const double r = right[static_cast<std::size_t>(sample)];
                                finite = finite && std::isfinite(l) && std::isfinite(r)
                                    && std::abs(l) <= 1.0 && std::abs(r) <= 1.0;
                                energy += 0.5 * (l * l + r * r);
                            }
                            finite = finite && energy > 1.0e-12;
                            const double levelDb = 10.0 * std::log10(
                                std::max(energy / window, 1.0e-30));
                            if (stroke > 0)
                                increases.push_back(levelDb - previousDb);
                            previousDb = levelDb;
                        }
                    }
    expect(finite, "fixed-velocity repeated strokes were silent, nonfinite or unbounded");
    std::sort(increases.begin(), increases.end());
    const double p95 = increases[static_cast<std::size_t>(
        0.95 * static_cast<double>(increases.size() - 1))];
    expect(p95 < 3.5,
           "fixed-velocity repeated chords acquired excessive automatic RMS accents");
    expect(increases.back() < 6.0,
           "a fixed-velocity repeated chord produced a dramatic RMS jump");
    std::cout << "Fixed-velocity repeated-strum adjacent RMS increase: p95 "
              << p95 << " dB, maximum " << increases.back() << " dB\n";
}

}

int main()
{
    testSweepChangesAudio();
    testFixedVelocityRepeatedStrokeLevels();
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        testContinuousWristSweep(rate);
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
        // two-second rest. The next chord must begin low-to-high again. Each
        // string sounds at its own moment about the sweep (strumScatter), so
        // the stroke's direction is read from the stroke itself.
        performer->reset();
        performer->beginBlock(left.data(), right.data(), 1);
        for (const int note : open)
            performer->noteOn(0, 1, note, 100);
        performer->endBlock();
        expect(Access::strokeSign(performer->engine()) > 0.0f,
               "reset resumed a stale upstroke instead of starting a downstroke");

        // Duplicate owners remain two physical pitches. Their Finger timing
        // must equal the ordinary two-note pinch, not a three-string sweep.
        // MIDI ownership remains balanced independently of that timing.
        auto referenceDyad = std::make_unique<Performer>();
        referenceDyad->prepare(rate, 64);
        referenceDyad->beginBlock(left.data(), right.data(), 1);
        referenceDyad->noteOn(0, 1, 59, 100);
        referenceDyad->noteOn(0, 1, 64, 100);
        referenceDyad->endBlock();
        auto dyad = std::make_unique<Performer>();
        dyad->prepare(rate, 64);
        dyad->beginBlock(left.data(), right.data(), 1);
        for (const int note : { 59, 64, 59 })
            dyad->noteOn(0, 1, note, 100);
        dyad->endBlock();
        const int low = dyad->engine().heldString(59), high = dyad->engine().heldString(64);
        const int referenceLow = referenceDyad->engine().heldString(59);
        const int referenceHigh = referenceDyad->engine().heldString(64);
        expect(low >= 0 && high >= 0 && referenceLow >= 0 && referenceHigh >= 0,
               "duplicate dyad lost a physical string");
        if (low >= 0 && high >= 0 && referenceLow >= 0 && referenceHigh >= 0)
            expect(Access::delay(dyad->engine(), low) == Access::delay(referenceDyad->engine(), referenceLow)
                       && Access::delay(dyad->engine(), high) == Access::delay(referenceDyad->engine(), referenceHigh),
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
