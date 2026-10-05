// Equal-velocity force variation and passive contact on a ringing string.
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
    using Loop = AcustraEngine::StringLoop;
    static void condition(Loop& loop, float position, bool stopVelocity = false)
    { AcustraEngine::conditionRepluckContact(loop, position, stopVelocity); }
    static std::array<Loop, 2> sounding(const AcustraEngine& engine, int string = 0)
    { return engine.voices_[static_cast<std::size_t>(string)].loops; }
    static std::array<Loop, 2> retained(const AcustraEngine& engine)
    { return { engine.voices_[0].tailLoop, engine.voices_[0].tailParallelLoop }; }
    static float contact(const AcustraEngine& engine)
    { return engine.voices_[0].pluckPoint; }
    static bool hasTail(const AcustraEngine& engine, int string = 0)
    { return engine.voices_[static_cast<std::size_t>(string)].tailActive; }
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
        // The merge path follows the fired attack, rather than a level
        // estimate. Its absence makes this a pure fresh-source reference;
        // the prior note/velocity/sample and RNG history remain intact.
        v.attackFired = false;
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

void testScheduledEqualVelocityRepeatUsesItsReleaseSample()
{
    // Both strokes release at absolute sample D. Scheduling the second one
    // while sample zero is current must not turn it into a same-sample
    // duplicate, or change its physical force and audible wave. In particular,
    // D can lie inside a host block rather than at its start.
    const auto render = [](double rate, int delay, int block, bool scheduled)
    {
        auto engine = fresh(rate);
        const int length = delay + static_cast<int>(0.04 * rate);
        std::array<std::vector<float>, 2> audio {
            std::vector<float>(length), std::vector<float>(length) };
        const auto process = [&](int first, int last)
        {
            while (first < last)
            {
                const int count = std::min(block, last - first);
                engine->process(audio[0].data() + first, audio[1].data() + first, count);
                first += count;
            }
        };
        engine->noteOn(40, 0.6f, 1);
        if (scheduled)
            engine->noteOn(40, 0.6f, 1, delay);
        else
        {
            process(0, delay);
            engine->noteOn(40, 0.6f, 1);
        }
        process(scheduled ? 0 : delay, length);
        return audio;
    };

    for (const double rate : { 48000.0, 96000.0 })
        for (const int delay : { 1, 97 })
        {
            const auto issued = render(rate, delay, 1, false);
            expect(std::any_of(issued[0].begin() + delay, issued[0].end(),
                              [](float sample) { return sample != 0.0f; }),
                   "equal-velocity timing reference rendered silence");
            for (const int block : { 1, 17, 127, 512 })
                if (render(rate, delay, block, true) != issued)
                {
                    ++failures;
                    std::cerr << "FAIL: scheduled equal-velocity repeat differed from "
                              << "the immediate release at " << rate << " Hz, delay "
                              << delay << ", block " << block << '\n';
                }
        }
}

int loopIndex(const Access::Loop& loop, int age)
{
    const int size = static_cast<int>(loop.delay.size());
    return (loop.writeIndex - 1 - age + 2 * size) % size;
}

double slopeEnergy(const Access::Loop& loop)
{
    // Stored string energy here is the sampled cyclic slope norm, including
    // the closing edge. This is not the coupled filters/body energy ledger.
    const int length = static_cast<int>(std::ceil(loop.currentDelay)) + 1;
    double sum = 0.0;
    for (int age = 0; age < length; ++age)
    {
        const double difference = loop.delay[loopIndex(loop, age)]
            - loop.delay[loopIndex(loop, (age + 1) % length)];
        sum += difference * difference;
    }
    return sum;
}

std::array<float, 18> filterMemory(const Access::Loop& loop)
{
    return { loop.allpassY1, loop.allpassY2, loop.thiranFraction,
        loop.thiranFirst, loop.thiranSecond, loop.bendingLossY1,
        loop.bendingLossY2, loop.broadLossFilter.state,
        loop.broadLossFilter.previousInput, loop.lossFilter.state,
        loop.lossFilter.previousInput, loop.dispersion.x1, loop.dispersion.x2,
        loop.dispersion.y1, loop.dispersion.y2, loop.secondDispersion.x1,
        loop.secondDispersion.y1, loop.appliedReleaseGain };
}

void testRingingContactIsPassiveAtTheActualFractionalPoint()
{
    constexpr double twoPi = 6.2831853071795864769;
    for (const float period : { 8.375f, 31.0f, 127.625f, 510.25f })
        for (const int write : { 0, 3, 8191 })
            for (const float position : { 0.13f, 0.33f, 0.71f, 0.94f })
                for (const double phase : { 0.0, 0.71, 2.03 })
                {
                    Access::Loop original;
                    original.currentDelay = original.targetDelay = period;
                    original.writeIndex = write;
                    original.delay.fill(0.375f);
                    const int length = static_cast<int>(std::ceil(period)) + 1;
                    for (int age = 0; age < length; ++age)
                    {
                        const double angle = twoPi * age / length;
                        original.delay[loopIndex(original, age)] = static_cast<float>(
                            0.2 + 0.6 * std::sin(angle + phase)
                            + 0.17 * std::cos(3.0 * angle - phase));
                    }
                    original.allpassY1 = 0.23f; original.allpassY2 = -0.19f;
                    original.bendingLossY1 = -0.11f; original.bendingLossY2 = 0.31f;
                    original.broadLossFilter.state = 0.13f;
                    original.lossFilter.previousInput = -0.27f;
                    original.dispersion.x1 = 0.41f; original.dispersion.y2 = -0.39f;
                    original.secondDispersion.y1 = 0.07f;
                    const double before = slopeEnergy(original);
                    auto conditioned = original;
                    Access::condition(conditioned, position);
                    expect(std::abs(conditioned.displacementAt(position)) < 2.0e-6,
                           "ringing string retained displacement under the new contact");
                    expect(slopeEnergy(conditioned) <= before * (1.0 + 2.0e-6),
                           "landing contact added stored cyclic slope energy");
                    expect(slopeEnergy(conditioned) > 1.0e-4 * before,
                           "landing contact erased the whole incoming wave");
                    expect(filterMemory(conditioned) == filterMemory(original),
                           "landing contact erased the incoming wave's filter memory");
                    expect(conditioned.bridgeDerivative.history == original.bridgeDerivative.history
                               && conditioned.bridgeDerivative.index == original.bridgeDerivative.index,
                           "landing contact erased the incoming bridge velocity history");
                    double oldMean = 0.0, newMean = 0.0;
                    for (int age = 0; age < length; ++age)
                    {
                        oldMean += original.delay[loopIndex(original, age)];
                        newMean += conditioned.delay[loopIndex(conditioned, age)];
                    }
                    expect(std::abs(newMean - oldMean) < 2.0e-6 * length,
                           "landing contact changed the wave's constant component");
                    auto twice = conditioned;
                    Access::condition(twice, position);
                    auto opposite = original;
                    for (auto& sample : opposite.delay) sample = -sample;
                    Access::condition(opposite, position);
                    for (std::size_t i = 0; i < original.delay.size(); ++i)
                    {
                        expect(std::abs(twice.delay[i] - conditioned.delay[i]) < 2.0e-6,
                               "contact constraint changed on a second landing at rest");
                        expect(std::abs(opposite.delay[i] + conditioned.delay[i]) < 2.0e-6,
                               "opposite ringing phase did not receive opposite contact correction");
                    }
                    expect(conditioned.delay[loopIndex(conditioned, length)] == 0.375f,
                           "contact changed samples outside the retained wave period");
                    auto held = original;
                    Access::condition(held, position, true);
                    auto advanced = held;
                    for (int age = 0; age < length; ++age)
                        advanced.delay[loopIndex(advanced, age)]
                            = held.delay[loopIndex(held, (age + length - 1) % length)];
                    expect(std::abs(held.displacementAt(position)) < 2.0e-6
                               && std::abs(advanced.displacementAt(position)
                                           - held.displacementAt(position)) < 2.0e-6,
                           "preceding hold left local displacement or ideal one-frame motion");
                    expect(slopeEnergy(held) <= before * (1.0 + 2.0e-6),
                           "stopping local contact motion added cyclic slope energy");
                }

    Access::Loop constant;
    constant.currentDelay = 31.625f;
    constant.delay.fill(0.25f);
    const auto untouched = constant.delay;
    Access::condition(constant, 0.31f);
    expect(constant.delay == untouched && !constant.derivativeCrossesContact,
           "a wave with zero contact displacement acquired an artificial impact");

    // For an integer round trip, this symmetric travelling-wave field has
    // zero displacement at every string point but carries a nonconstant
    // velocity component. The landing hand must leave that component alone.
    Access::Loop velocity;
    velocity.currentDelay = 32.0f;
    velocity.writeIndex = 7;
    for (int age = 0; age <= 32; ++age)
        velocity.delay[loopIndex(velocity, age)] = static_cast<float>(
            0.4 * std::cos(twoPi * age / 32.0));
    const auto moving = velocity.delay;
    expect(std::abs(velocity.displacementAt(0.31f)) < 1.0e-7,
           "zero-displacement velocity reference was not symmetric");
    Access::condition(velocity, 0.31f);
    for (std::size_t i = 0; i < moving.size(); ++i)
        expect(std::abs(velocity.delay[i] - moving[i]) < 1.0e-7,
               "landing contact removed motion already compatible with its displacement constraint");
}

void testContactActsOnTheReleasedRepeatAndPreservesOtherCaptures()
{
    for (const int elapsed : { 0, 97, 509 })
        for (const int nextNote : { 40, 41 })
        {
            auto engine = fresh();
            engine->noteOn(40, 0.6f, 1);
            expect(!Access::hasTail(*engine), "first pluck created an old-wave contact");
            advance(*engine, elapsed);
            const auto old = Access::sounding(*engine);
            engine->noteOn(nextNote, 0.6f, 1);
            const auto retained = Access::retained(*engine);
            for (int plane = 0; plane < 2; ++plane)
            {
                if (elapsed > 0 && nextNote == 40)
                {
                    const auto merged = Access::sounding(*engine);
                    expect(!Access::hasTail(*engine),
                           "actual repeated note opened a separate old-wave bridge port");
                    expect(filterMemory(merged[plane]) == filterMemory(old[plane]),
                           "actual repeated note erased incoming filter memory");
                }
                else
                {
                    expect(Access::hasTail(*engine), "refret discarded the existing old-pitch wave");
                    expect(retained[plane].delay == old[plane].delay,
                           "fresh pitch or same-sample duplicate altered its captured wave");
                }
            }
        }
}

void testMergedWaveContainsTheOldMotionAndItsBudgetedNewRelease()
{
    for (const double rate : { 44100.0, 96000.0 })
        for (const int note : { 40, 60 })
            for (const int elapsed : { 97, 509, 4093 })
            {
                auto ringing = fresh(rate), sourceOnly = fresh(rate);
                ringing->noteOn(note, 0.6f, 1); sourceOnly->noteOn(note, 0.6f, 1);
                advance(*ringing, elapsed); advance(*sourceOnly, elapsed);
                const auto old = Access::sounding(*ringing);
                // Keep the same stroke history/RNG/bridge state. Only the
                // incoming string wave is absent from this release reference.
                Access::forgetOldWave(*sourceOnly);
                ringing->noteOn(note, 0.6f, 1); sourceOnly->noteOn(note, 0.6f, 1);
                const auto merged = Access::sounding(*ringing);
                const auto release = Access::sounding(*sourceOnly);
                expect(Access::contact(*ringing) == Access::contact(*sourceOnly)
                           && Access::gain(*ringing) == Access::gain(*sourceOnly),
                       "merging the old wave changed the new stroke's random draws");
                double oldEnergy = 0.0, freshEnergy = 0.0, mergedEnergy = 0.0;
                std::array<double, 2> fittedGain {};
                for (int plane = 0; plane < 2; ++plane)
                {
                    auto compatible = old[plane];
                    Access::condition(compatible, Access::contact(*ringing), true);
                    expect(slopeEnergy(compatible) <= slopeEnergy(old[plane]) * (1.0 + 2.0e-6),
                           "the preceding hold added stored string energy before merging");
                    auto increment = old[plane]; increment.delay.fill(0.0f);
                    const int length = static_cast<int>(std::ceil(old[plane].currentDelay)) + 1;
                    double sourceNorm = 0.0, sourceCross = 0.0, peak = 0.0;
                    for (int age = 0; age < length; ++age)
                    {
                        // Compare the same physical position along the old
                        // string, allowing the fresh source's fractional grid
                        // to differ. This is independent of the gain solver.
                        const double freshAge = age * static_cast<double>(release[plane].currentDelay)
                                                / old[plane].currentDelay;
                        const int whole = static_cast<int>(freshAge);
                        const double part = freshAge - whole;
                        const double a = release[plane].delay[loopIndex(release[plane], whole)];
                        const double b = release[plane].delay[loopIndex(release[plane], whole + 1)];
                        const double source = a + part * (b - a);
                        increment.delay[loopIndex(increment, age)] = static_cast<float>(source);
                        const double actual = merged[plane].delay[loopIndex(merged[plane], age)];
                        const double previous = compatible.delay[loopIndex(compatible, age)];
                        sourceNorm += source * source;
                        sourceCross += source * (actual - previous);
                        peak = std::max({ peak, std::abs(actual), std::abs(previous), std::abs(source) });
                    }
                    expect(sourceNorm > 1.0e-12 && slopeEnergy(compatible) > 1.0e-12,
                           "merged-wave fixture contained no new release or incoming motion");
                    const double gain = sourceCross / std::max(sourceNorm, 1.0e-30);
                    fittedGain[plane] = gain;
                    expect(gain > 0.0 && gain <= 1.0 + 2.0e-5,
                           "merged source gain vanished or amplified the nominal fresh release");
                    double error = 0.0;
                    for (int age = 0; age < length; ++age)
                    {
                        const double expected = compatible.delay[loopIndex(compatible, age)]
                            + gain * increment.delay[loopIndex(increment, age)];
                        error = std::max(error, std::abs(
                            merged[plane].delay[loopIndex(merged[plane], age)] - expected));
                    }
                    expect(error < 2.0e-5 * peak + 2.0e-8,
                           "merged string lost or misaligned its preceding compatible wave");
                    oldEnergy += slopeEnergy(compatible);
                    freshEnergy += slopeEnergy(increment);
                    mergedEnergy += slopeEnergy(merged[plane]);
                    expect(merged[plane].currentDelay == old[plane].currentDelay
                               && merged[plane].writeIndex == old[plane].writeIndex
                               && filterMemory(merged[plane]) == filterMemory(old[plane])
                               && merged[plane].bridgeDerivative.history == old[plane].bridgeDerivative.history,
                           "merging restarted the incoming string's clock or filter history");
                }
                expect(std::abs(fittedGain[0] - fittedGain[1]) < 2.0e-5,
                       "one physical stroke acquired different force gains in its two planes");
                expect(mergedEnergy <= (oldEnergy + freshEnergy) * (1.0 + 2.0e-5),
                       "merged string exceeded the old-wave plus fresh-release energy budget");
            }
}

void testElapsedRepeatUsesOnePhysicalStringPort()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const int note : { 40, 60 })
            for (const bool released : { false, true })
            {
                auto engine = fresh(rate);
                engine->setSympatheticStringsEnabled(false);
                engine->noteOn(note, 0.6f, 1);
                expect(!Access::hasTail(*engine), "fresh pluck acquired a second string port");
                for (int stroke = 0; stroke < 3; ++stroke)
                {
                    advance(*engine, static_cast<int>(0.08 * rate));
                    if (released) engine->noteOff(note, 1);
                    engine->noteOn(note, stroke == 1 ? 0.55f : 0.6f, 1);
                    expect(!Access::hasTail(*engine),
                           "same-string repeat created a separately damped bridge tail port");
                }
            }
}

void testLongPhaseLockedTremoloDoesNotAccumulateNearNodeEnergy()
{
    constexpr double rate = 48000.0;
    for (const int note : { 43, 64 })
        for (const bool detuned : { false, true })
        {
            const int channel = note == 64 ? 6 : 1;
            const double period = rate / (440.0 * std::exp2((note - 69) / 12.0));
            const int cycles = static_cast<int>(std::round(0.03 * rate / period));
            const int interval = static_cast<int>(std::round(
                (cycles + (detuned ? 0.13 : 0.0)) * period));
            auto engine = fresh(rate);
            engine->setSympatheticStringsEnabled(false);
            std::array<double, 128> stored {}, radiated {};
            std::array<float, 128> left {}, right {};
            for (int stroke = 0; stroke < 128; ++stroke)
            {
                if (stroke > 0) engine->noteOff(note, channel);
                engine->noteOn(note, 0.6f, channel);
                for (int offset = 0; offset < interval;)
                {
                    const int count = std::min(128, interval - offset);
                    engine->process(left.data(), right.data(), count);
                    for (int i = 0; i < count; ++i)
                    {
                        expect(std::isfinite(left[i]) && std::isfinite(right[i])
                                   && std::abs(left[i]) < 0.9f && std::abs(right[i]) < 0.9f,
                               "phase-locked tremolo became nonfinite or hit the output rail");
                        radiated[stroke] += left[i] * left[i] + right[i] * right[i];
                    }
                    offset += count;
                }
                const auto loops = Access::sounding(*engine, channel - 1);
                stored[stroke] = slopeEnergy(loops[0]) + slopeEnergy(loops[1]);
            }
            double earlyStored = 0.0, lateStored = 0.0;
            double earlyRadiated = 0.0, lateRadiated = 0.0;
            for (int stroke = 16; stroke < 48; ++stroke)
            {
                earlyStored += stored[stroke]; earlyRadiated += radiated[stroke];
            }
            for (int stroke = 96; stroke < 128; ++stroke)
            {
                lateStored += stored[stroke]; lateRadiated += radiated[stroke];
            }
            expect(earlyStored > 0.0 && earlyRadiated > 1.0e-6,
                   "phase-locked tremolo energy reference was silent");
            expect(lateStored < 2.0 * earlyStored,
                   "near-node stored string energy kept accumulating across128 strokes");
            expect(lateRadiated < 1.5 * earlyRadiated,
                   "phase-locked tremolo kept growing after its warmup");
            std::cout << "Phase-locked note=" << note << " detuned=" << detuned
                      << " stored late/early=" << lateStored / earlyStored
                      << " radiated late/early=" << lateRadiated / earlyRadiated << '\n';
        }
}

void testAgeAutomationPreservesTheMergedString()
{
    for (const int delay : { 16, 64 })
    {
        auto changed = fresh(), steady = fresh();
        for (auto* engine : { changed.get(), steady.get() })
        {
            engine->noteOn(43, 0.6f, 1);
            advance(*engine, 12000);
            engine->noteOn(43, 0.6f, 1);
        }
        std::array<float, 64> beforeLeft {}, beforeRight {}, steadyBeforeLeft {}, steadyBeforeRight {};
        changed->process(beforeLeft.data(), beforeRight.data(), delay);
        steady->process(steadyBeforeLeft.data(), steadyBeforeRight.data(), delay);
        const auto before = Access::sounding(*changed);
        acustra::EngineParameters parameters;
        parameters.stringAge += 0.001f;
        changed->setParameters(parameters);
        const auto afterControl = Access::sounding(*changed);
        for (int plane = 0; plane < 2; ++plane)
            expect(afterControl[plane].delay == before[plane].delay
                       && filterMemory(afterControl[plane]) == filterMemory(before[plane])
                       && afterControl[plane].bridgeDerivative.history
                           == before[plane].bridgeDerivative.history,
                   "Age automation reset the merged string's wave or filter memory");
        expect(!Access::hasTail(*changed),
               "Age automation split the continuing string into another bridge port");

        std::array<float, 256> left {}, right {}, referenceLeft {}, referenceRight {};
        changed->process(left.data(), right.data(), 256);
        steady->process(referenceLeft.data(), referenceRight.data(), 256);
        double peak = std::max(std::abs(static_cast<double>(steadyBeforeLeft[delay - 1])),
                               std::abs(static_cast<double>(steadyBeforeRight[delay - 1])));
        double stepDifference = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            expect(std::isfinite(left[i]) && std::isfinite(right[i]),
                   "Age automation made the continuing string nonfinite");
            peak = std::max({ peak, std::abs(static_cast<double>(referenceLeft[i])),
                             std::abs(static_cast<double>(referenceRight[i])) });
            const double leftStep = left[i] - (i == 0 ? beforeLeft[delay - 1] : left[i - 1]);
            const double rightStep = right[i] - (i == 0 ? beforeRight[delay - 1] : right[i - 1]);
            const double referenceLeftStep = referenceLeft[i]
                - (i == 0 ? steadyBeforeLeft[delay - 1] : referenceLeft[i - 1]);
            const double referenceRightStep = referenceRight[i]
                - (i == 0 ? steadyBeforeRight[delay - 1] : referenceRight[i - 1]);
            stepDifference = std::max({ stepDifference,
                std::abs(leftStep - referenceLeftStep),
                std::abs(rightStep - referenceRightStep) });
        }
        expect(peak > 1.0e-4 && stepDifference <= 2.0e-4 * peak,
               "a small Age step introduced a large adjacent-sample transient after a same-pitch merge");
        std::cout << "Merged Age delay=" << delay
                  << " step difference/peak=" << stepDifference / peak << '\n';
        const auto actual = Access::sounding(*changed), reference = Access::sounding(*steady);
        const double actualEnergy = slopeEnergy(actual[0]) + slopeEnergy(actual[1]);
        const double referenceEnergy = slopeEnergy(reference[0]) + slopeEnergy(reference[1]);
        expect(actualEnergy > 0.99 * referenceEnergy && actualEnergy < 1.01 * referenceEnergy,
               "a small Age step erased or increased the merged string's stored wave");
    }
}

void testRapidTremoloAudioRemainsBoundedAndBlockExact()
{
    using Audio = std::array<std::vector<float>, 2>;
    const auto render = [](double rate, int note, int block, bool pick,
                           double seconds, bool repeated)
    {
        auto engine = fresh(rate);
        acustra::EngineParameters parameters;
        parameters.picking = pick ? acustra::PickingTechnique::Pick
                                  : acustra::PickingTechnique::Finger;
        engine->setParameters(parameters);
        engine->setSympatheticStringsEnabled(false);
        const int interval = static_cast<int>(seconds * rate);
        constexpr int strokes = 32;
        Audio audio { std::vector<float>(strokes * interval),
                      std::vector<float>(strokes * interval) };
        for (int stroke = 0; stroke < strokes; ++stroke)
        {
            if (repeated || stroke == 0)
                engine->noteOn(note, 0.6f, 1);
            for (int offset = 0; offset < interval;)
            {
                const int count = std::min(block, interval - offset);
                const int first = stroke * interval + offset;
                engine->process(audio[0].data() + first, audio[1].data() + first, count);
                offset += count;
            }
        }
        return audio;
    };
    // Low and high stopped notes stress both long wave periods and the
    // fractional-delay/filter memory that matters at short periods.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const int note : { 40, 60 })
            for (const double seconds : { 0.03, 1.0 / 12.0 })
        {
            const bool pick = note == 60;
            const auto audio = render(rate, note, 127, pick, seconds, true);
            const auto isolated = render(rate, note, 127, pick, seconds, false);
            const int interval = static_cast<int>(seconds * rate);
            std::array<double, 32> peaks {}, energies {}, onsetSteps {}, backgroundSteps {};
            std::array<double, 32> isolatedEnergies {};
            double isolatedPeak = 0.0;
            for (const auto& channel : isolated)
                for (std::size_t i = 0; i < channel.size(); ++i)
                {
                    isolatedPeak = std::max(isolatedPeak,
                        std::abs(static_cast<double>(channel[i])));
                    isolatedEnergies[i / interval] += channel[i] * channel[i];
                }
            const double isolatedEnergy = *std::max_element(
                isolatedEnergies.begin(), isolatedEnergies.end());
            const int latency = acustra::AcustraEngine::outputLatencySamples();
            const int contactWindow = static_cast<int>(0.002 * rate);
            for (const auto& channel : audio)
                for (std::size_t i = 0; i < channel.size(); ++i)
                {
                    const float value = channel[i];
                    expect(std::isfinite(value) && std::abs(value) < 0.9f,
                           "rapid tremolo became nonfinite or hit the output rail");
                    const int stroke = static_cast<int>(i) / interval;
                    const int local = static_cast<int>(i) % interval;
                    peaks[stroke] = std::max(peaks[stroke], std::abs(static_cast<double>(value)));
                    energies[stroke] += value * value;
                    const double step = i == 0 ? std::abs(value)
                        : std::abs(static_cast<double>(value) - channel[i - 1]);
                    if (local >= latency && local < latency + contactWindow)
                        onsetSteps[stroke] = std::max(onsetSteps[stroke], step);
                    if (local >= interval - contactWindow)
                        backgroundSteps[stroke] = std::max(backgroundSteps[stroke], step);
                }
            expect(peaks[0] > 1.0e-4 && energies[0] > 1.0e-6,
                   "rapid tremolo reference stroke was silent");
            // Compare onset steps at the actual output latency. Repeated
            // forcing can feed resonant body modes above an isolated note's
            // level, so passivity is checked on stored string slopes above;
            // this audio fixture checks rails and the settled train instead.
            for (int stroke = 1; stroke < 32; ++stroke)
            {
                expect(onsetSteps[stroke] < 3.0 * std::max(
                           onsetSteps[0], backgroundSteps[stroke - 1]),
                       "ringing contact introduced a large latency-aligned onset step");
            }
            double settledEnergy = 0.0, lateEnergy = 0.0;
            for (int stroke = 8; stroke < 16; ++stroke) settledEnergy += energies[stroke];
            for (int stroke = 24; stroke < 32; ++stroke) lateEnergy += energies[stroke];
            expect(lateEnergy < 1.5 * settledEnergy,
                   "rapid tremolo continued accumulating energy after its warmup");
            // Exact event positions are kept while only process partitions
            // differ; compare both actual audio channels, not gain observers.
            if (rate == 48000.0 && seconds == 0.03)
                for (const int block : { 1, 17, 512 })
                    expect(render(rate, note, block, pick, seconds, true) == audio,
                           "rapid tremolo audio depended on host block partition");
            std::cout << "Tremolo " << rate << " Hz note=" << note
                      << " stroke Hz=" << 1.0 / seconds
                      << " isolated peak ratio=" << *std::max_element(peaks.begin(), peaks.end()) / isolatedPeak
                      << " isolated energy ratio=" << *std::max_element(energies.begin(), energies.end()) / isolatedEnergy
                      << " late/settled energy=" << lateEnergy / settledEnergy
                      << '\n';
        }
}
}

int main()
{
    testBoundsMeanAndForceTracking();
    testExplicitPerformanceAndResetBoundaries();
    testIndependentRateAndBlockInvariantSequence();
    testScheduledEqualVelocityRepeatUsesItsReleaseSample();
    testRingingContactIsPassiveAtTheActualFractionalPoint();
    testContactActsOnTheReleasedRepeatAndPreservesOtherCaptures();
    testElapsedRepeatUsesOnePhysicalStringPort();
    testMergedWaveContainsTheOldMotionAndItsBudgetedNewRelease();
    testLongPhaseLockedTremoloDoesNotAccumulateNearNodeEnergy();
    testAgeAutomationPreservesTheMergedString();
    testRapidTremoloAudioRemainsBoundedAndBlockExact();
    return failures == 0 ? 0 : 1;
}
