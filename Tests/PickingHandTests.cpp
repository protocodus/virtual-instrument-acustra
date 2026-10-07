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
    static std::array<float, 3> gesture(AcustraEngine& engine, std::uint64_t sample)
    {
        engine.sampleClock_ = sample;
        const auto g = engine.nextPickingGesture();
        return { g.position, g.pressure, g.active ? 1.0f : 0.0f };
    }
    static std::array<float, 3> captured(const AcustraEngine& engine, int string)
    {
        const auto g = engine.voices_[static_cast<std::size_t>(string)].pluckGesture;
        return { g.position, g.pressure, g.active ? 1.0f : 0.0f };
    }
    static float repeatForce(const AcustraEngine& engine, int string)
    { return engine.voices_[static_cast<std::size_t>(string)].repeatedPluckGain; }
};
}

namespace
{
using acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
acustra::PerformanceRealism options(bool hand)
{ return { false, hand, false, false, false }; }
auto fresh(bool hand = true, double rate = 48000.0)
{
    auto engine = std::make_unique<AcustraEngine>();
    engine->setPerformanceRealism(options(hand));
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    return engine;
}
std::vector<float> render(AcustraEngine& engine, int samples)
{
    std::vector<float> result(static_cast<std::size_t>(samples * 2));
    engine.process(result.data(), result.data() + samples, samples);
    for (const auto value : result)
        expect(std::isfinite(value) && std::abs(value) <= 1.0f,
               "gesture produced a nonfinite or unbounded sample");
    return result;
}

void boundedCoherence()
{
    auto engine = fresh();
    auto previous = Access::gesture(*engine, 0);
    expect(previous[2] == 0.0f, "first contact unexpectedly changed gesture");
    std::array<double, 2> sum {}, square {}, product {}, changes {};
    constexpr int count = 12000;
    for (int n = 1; n <= count; ++n)
    {
        const auto at = static_cast<std::uint64_t>(n) * 9600;
        const auto current = Access::gesture(*engine, at);
        expect(current == Access::gesture(*engine, at),
               "simultaneous contacts advanced the shared hand twice");
        expect(current[2] == 1.0f, "recent contact lost its hand state");
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const double v = current[axis];
            expect(std::isfinite(v) && std::abs(v) <= 1.0,
                   "bounded hand diffusion escaped its physical map");
            sum[axis] += v; square[axis] += v * v;
            product[axis] += v * previous[axis];
            changes[axis] += (v - previous[axis]) * (v - previous[axis]);
        }
        previous = current;
    }
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        const double mean = sum[axis] / count;
        const double variance = square[axis] / count - mean * mean;
        const double correlation = (product[axis] / count - mean * mean) / variance;
        expect(std::abs(mean) < 0.15 && variance > 0.23 && variance < 0.43,
               "hand posture collapsed, drifted to a boundary, or developed a large bias");
        expect(correlation > 0.85 && correlation < 0.999,
               "successive gestures are independent or frozen");
        expect(changes[axis] / count < 0.1,
               "contact changes jump more than a restrained moving hand");
        std::cout << "Hand axis " << axis << ": mean=" << mean
                  << " variance=" << variance << " correlation=" << correlation << '\n';
    }
    expect(Access::gesture(*engine, (count + 11ull) * 9600)[2] == 0.0f,
           "a rest longer than the hand-memory window retained active variation");
    engine->reset();
    expect(Access::gesture(*engine, 0)[2] == 0.0f,
           "reset retained an active picking gesture");

    // Identical physical event times yield the same stochastic path at each
    // host rate; the independent random stream is never advanced by rendering.
    auto a = fresh(true, 44100.0), b = fresh(true, 96000.0);
    for (std::uint64_t n = 0; n < 200; ++n)
        expect(Access::gesture(*a, n * 8820) == Access::gesture(*b, n * 19200),
               "hand trajectory depends on host sample rate");
}

void actualContactsAndIntent()
{
    auto a = fresh(), b = fresh(false);
    a->noteOn(64, 0.45f, 6); b->noteOn(64, 0.45f, 6);
    expect(render(*a, 9600) == render(*b, 9600),
           "first note changed before any hand history existed");
    a->noteOn(64, 0.45f, 6); b->noteOn(64, 0.45f, 6);
    expect(Access::repeatForce(*a, 5) == Access::repeatForce(*b, 5),
           "shared hand changed the bounded equal-velocity repeat force");
    expect(render(*a, 9600) != render(*b, 9600),
           "coherent hand never reaches the audible physical contact");
    a->noteOn(64, 0.9f, 6);
    expect(Access::repeatForce(*a, 5) == 1.0f,
           "an explicit velocity accent gained unintended force jitter");
    expect(AcustraEngine::outputLatencySamples() == 7,
           "hand humanization added latency");

    auto queued = fresh();
    queued->beginStrum();
    queued->noteOn(40, 0.4f, 1, 0, true);
    render(*queued, 9600);
    queued->beginStrum();
    queued->noteOn(40, 0.4f, 1, 1800, true);
    const auto captured = Access::captured(*queued, 0);
    expect(captured[2] == 1.0f, "second strum has no shared gesture");
    render(*queued, 128);
    queued->beginStrum();
    queued->noteOn(55, 0.4f, 4, 0, true);
    expect(captured != Access::captured(*queued, 3),
           "later stroke failed to advance the hand");
    render(*queued, 4000);
    expect(captured == Access::captured(*queued, 0),
           "later stroke changed a queued stroke's physical contact");

    a->prepare(48000.0, 128);
    auto restarted = fresh();
    a->noteOn(64, 0.45f, 6); restarted->noteOn(64, 0.45f, 6);
    expect(render(*a, 9600) == render(*restarted, 9600),
           "prepare failed to restart the deterministic performance");
    a->noteOn(64, 0.45f, 6); restarted->noteOn(64, 0.45f, 6);
    expect(render(*a, 9600) == render(*restarted, 9600),
           "prepare left a stale random hand trajectory");
}

std::vector<float> phrase(double rate, int block, bool reverse)
{
    auto performer = std::make_unique<acustra::Performer>();
    performer->engine().setPerformanceRealism(options(true));
    performer->prepare(rate, block);
    const int interval = static_cast<int>(rate / 5.0);
    const int length = interval * 7;
    std::vector<float> result(static_cast<std::size_t>(length * 2));
    constexpr std::array<int, 6> chord { 40, 47, 52, 55, 59, 64 };
    for (int start = 0; start < length; start += block)
    {
        const int count = std::min(block, length - start);
        performer->beginBlock(result.data() + start, result.data() + length + start, count);
        for (int stroke = 0; stroke < 5; ++stroke)
        {
            const int at = stroke * interval + 37;
            if (at >= start && at < start + count)
                for (std::size_t i = 0; i < chord.size(); ++i)
                    performer->noteOn(at - start, 1,
                        chord[reverse ? chord.size() - 1 - i : i], 85);
            const int off = at + interval / 2;
            if (off >= start && off < start + count)
                for (const int note : chord)
                    performer->noteOff(off - start, 1, note);
        }
        performer->endBlock();
    }
    expect(performer->droppedEventCount() == 0, "gesture score dropped a MIDI event");
    return result;
}
}

int main()
{
    boundedCoherence();
    actualContactsAndIntent();
    for (const double rate : { 44100.0, 96000.0 })
        expect(phrase(rate, 1, false) == phrase(rate, 257, true),
               "gesture audio depends on block partition or simultaneous note order");
    if (failures == 0)
        std::cout << "Coherent picking hand, playable intent and scheduling passed.\n";
    return failures == 0 ? 0 : 1;
}
