// A conservative force variation for equal-velocity single-note repeats.
// Real-reference spread is evidence for the listening hypothesis, not a claim
// that the unlabelled Eastman takes were played at identical force or frets.
#include "DSP/AcustraEngine.h"

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
    static float gain(const AcustraEngine& engine)
    { return engine.voices_[0].repeatedPluckGain; }
    static double displacement(const AcustraEngine& engine)
    {
        const auto& voice = engine.voices_[0];
        const int length = static_cast<int>(std::round(voice.loops[0].currentDelay));
        return voice.releaseStepRise * voice.releaseShapePosition[0] * length
            / (0.8 * std::sqrt(voice.polarisationMix));
    }
    static void forgetOldWave(AcustraEngine& engine)
    {
        // Test the new release without the previously played note's tail.
        // Pluck history is intentionally kept, like the hand's memory.
        auto& v = engine.voices_[0];
        v.level = 0.0f;
        v.contactTravel.active = false;
        v.contactNoiseTravel.active = false;
        v.tailActive = false;
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{ if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

auto fresh(double rate = 48000.0)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    return engine;
}
void advance(acustra::AcustraEngine& engine, int samples, int block = 128)
{
    std::array<float, 128> left {}, right {};
    while (samples > 0)
    {
        const int count = std::min({ samples, block, 128 });
        engine.process(left.data(), right.data(), count);
        expect(std::all_of(left.begin(), left.begin() + count, [] (float x)
            { return std::isfinite(x) && std::abs(x) <= 1.0f; }),
            "repeated pluck render became nonfinite or unbounded");
        samples -= count;
    }
}

void testBoundsMeanAndForceTracking()
{
    auto engine = fresh();
    engine->noteOn(40, 0.5f, 1);
    expect(Access::gain(*engine) == 1.0f, "first pluck changed its nominal force");
    const double nominal = Access::displacement(*engine);
    double sum = 0.0, square = 0.0;
    float minimum = 2.0f, maximum = 0.0f;
    for (int n = 0; n < 512; ++n)
    {
        advance(*engine, 64);
        Access::forgetOldWave(*engine);
        engine->noteOn(40, 0.5f, 1);
        const float gain = Access::gain(*engine);
        minimum = std::min(minimum, gain);
        maximum = std::max(maximum, gain);
        sum += gain; square += gain * gain;
        // This is the physical released displacement, independently read
        // from the force step, so a cosmetic gain observer cannot pass it.
        expect(std::abs(Access::displacement(*engine) / nominal - gain) < 3.0e-6,
               "repeat force did not reach the held string's displacement");
        expect(gain > 0.888f && gain < 1.121f,
               "repeat force exceeded its bounded one-dB candidate range");
    }
    const double mean = sum / 512;
    const double sd = std::sqrt(square / 512 - mean * mean);
    expect(std::abs(mean - 1.0) < 0.012,
           "repeat variation biased the mean linear force");
    expect(minimum < 0.91f && maximum > 1.10f && sd > 0.04 && sd < 0.08,
           "repeated equal-velocity strokes did not produce modest force variety");
    std::cout << "Repeat force gain mean=" << mean << " SD=" << sd
              << " range=" << minimum << ".." << maximum << '\n';
}

void testExplicitPerformanceAndResetBoundaries()
{
    auto engine = fresh();
    engine->noteOn(40, 0.5f, 1);
    engine->noteOn(40, 0.5f, 1);
    expect(Access::gain(*engine) == 1.0f,
           "duplicate note-on on one sample acquired a second force draw");
    advance(*engine, 128);
    engine->noteOn(40, 0.85f, 1);
    expect(Access::gain(*engine) == 1.0f, "explicit accent force changed");
    advance(*engine, 128);
    engine->noteOn(41, 0.85f, 1);
    expect(Access::gain(*engine) == 1.0f, "new pitch acquired repeat variation");
    advance(*engine, 128);
    engine->beginStrum();
    engine->noteOn(41, 0.85f, 1, 0, true);
    expect(Access::gain(*engine) == 1.0f,
           "strum acquired a second variation on top of its measured draw");
    advance(*engine, 96001);
    engine->noteOn(41, 0.85f, 1);
    expect(Access::gain(*engine) == 1.0f, "rest beyond hand memory retained repeat variation");
    advance(*engine, 128);
    engine->noteOn(41, 0.85f, 1);
    expect(Access::gain(*engine) != 1.0f, "recent single-note repetition stayed identical");
    engine->reset();
    engine->noteOn(41, 0.85f, 1);
    expect(Access::gain(*engine) == 1.0f, "reset retained stale pluck history");
}

void testIndependentRateAndBlockInvariantSequence()
{
    auto a = fresh(48000.0), b = fresh(96000.0), c = fresh(48000.0);
    for (int n = 0; n < 24; ++n)
    {
        a->noteOn(40, 0.60f, 1);
        b->noteOn(40, 0.60f, 1);
        c->noteOn(40, 0.60f, 1);
        expect(Access::gain(*a) == Access::gain(*b)
                   && Access::gain(*a) == Access::gain(*c),
               "repeat force stream depended on host rate or block partition");
        advance(*a, 2048, 128);
        advance(*b, 4096, 128);
        advance(*c, 2048, 17);
    }
    a->prepare(48000.0, 128);
    auto d = fresh();
    a->noteOn(40, 0.6f, 1); d->noteOn(40, 0.6f, 1);
    advance(*a, 128); advance(*d, 128);
    a->noteOn(40, 0.6f, 1); d->noteOn(40, 0.6f, 1);
    expect(Access::gain(*a) == Access::gain(*d),
           "prepare did not restart deterministic per-string repeat draws");
    expect(acustra::AcustraEngine::outputLatencySamples() == 7,
           "repeat variation changed output latency");
}
}

int main()
{
    testBoundsMeanAndForceTracking();
    testExplicitPerformanceAndResetBoundaries();
    testIndependentRateAndBlockInvariantSequence();
    return failures == 0 ? 0 : 1;
}
