// A return stroke reverses only its newly released sideways field. Existing
// waves and packets in flight retain their own direction and energy.
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
    using Loop = AcustraEngine::StringLoop;
    static constexpr int delayCapacity = AcustraEngine::maximumDelaySamples;
    struct Source
    {
        std::array<Loop, 2> loops;
        float sign, excitation, step, mix;
        std::uint32_t pickingRandom, excitationRandom;
    };
    static Source source(const AcustraEngine& e, int string = 0)
    {
        const auto& v = e.voices_[static_cast<std::size_t>(string)];
        return { v.loops, v.pluckParallelSign, v.excitationParallelGain,
                 v.releaseStepRise, v.polarisationMix,
                 v.randomState, v.excitationNoiseState };
    }
    static int delay(const AcustraEngine& e, int string = 0)
    { return e.voices_[static_cast<std::size_t>(string)].pluckDelay; }
    static float sign(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].pluckParallelSign; }
    static float gain(const AcustraEngine& e)
    { return e.voices_[0].excitationParallelGain; }
    static bool tail(const AcustraEngine& e)
    { return e.voices_[0].tailActive; }
    static std::array<Loop, 2> tailLoops(const AcustraEngine& e)
    { return { e.voices_[0].tailLoop, e.voices_[0].tailParallelLoop }; }
    static float tailGain(const AcustraEngine& e)
    { return e.voices_[0].tailExcitationParallelGain; }
    static std::vector<std::array<float, 2>> futureArrivals(const AcustraEngine& e)
    {
        const auto& v = e.voices_[0];
        auto contact = v.contactTravel;
        std::vector<std::array<float, 2>> result;
        while (contact.active)
        {
            std::array<float, 2> packet {};
            const auto paths = contact.process(0.0f);
            const float local = 0.7071067811865475f * (paths[0] - paths[1]);
            packet[0] += 0.76f * local;
            packet[1] += v.excitationParallelGain * local;
            result.push_back(packet);
            if (result.size() >= AcustraEngine::RepluckArrivals::capacity) break;
        }
        return result;
    }
    static std::vector<std::array<float, 2>> retainedArrivals(const AcustraEngine& e,
                                                            std::size_t count)
    {
        auto arrivals = e.voices_[0].repluckArrivals;
        std::vector<std::array<float, 2>> result;
        for (std::size_t i = 0; i < count; ++i) result.push_back(arrivals.process());
        return result;
    }
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
auto fresh(double rate = 48000.0,
           acustra::PickingTechnique picking = acustra::PickingTechnique::Pick,
           acustra::GuitarModel model = acustra::GuitarModel::Original)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters p;
    p.picking = picking;
    p.guitarModel = model;
    if (model == acustra::GuitarModel::Bellido1978)
    { p.shape = acustra::BodyShape::Auditorium; p.bodyMaterial = acustra::BodyMaterial::Mahogany; }
    e->setParameters(p);
    e->prepare(rate, 127);
    e->setStringPerChannelMode(true);
    return e;
}
std::vector<float> advance(acustra::AcustraEngine& e, int samples)
{
    std::vector<float> result(static_cast<std::size_t>(samples * 2));
    std::array<float, 127> left {}, right {};
    for (int at = 0; at < samples;)
    {
        const int count = std::min(127, samples - at);
        e.process(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
        { result[static_cast<std::size_t>(2 * (at + i))] = left[static_cast<std::size_t>(i)];
          result[static_cast<std::size_t>(2 * (at + i) + 1)] = right[static_cast<std::size_t>(i)]; }
        at += count;
    }
    return result;
}
double slopeEnergy(const Access::Loop& loop)
{
    const int length = std::clamp(static_cast<int>(std::round(loop.currentDelay)),
                                  8, Access::delayCapacity - 3);
    const auto sample = [&](int age)
    {
        return loop.delay[static_cast<std::size_t>((loop.writeIndex - age
            + Access::delayCapacity) % Access::delayCapacity)];
    };
    double sum = 0.0;
    float previous = sample(length);
    for (int age = length - 1; age >= 0; --age)
    {
        const float current = sample(age);
        const double difference = static_cast<double>(current) - previous;
        sum += difference * difference;
        previous = current;
    }
    return sum;
}
void testFreshReleaseAndDefaultPaths()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto picking : { acustra::PickingTechnique::Finger,
                                   acustra::PickingTechnique::Pick,
                                   acustra::PickingTechnique::Thumb })
        {
            auto down = fresh(rate, picking, acustra::GuitarModel::Original);
            auto up = fresh(rate, picking, acustra::GuitarModel::Original);
            down->beginStrum(0, 0, false); up->beginStrum(0, 0, true);
            down->noteOn(47, 0.7f, 1, 0, true); up->noteOn(47, 0.7f, 1, 0, true);
            const auto a = Access::source(*down), b = Access::source(*up);
            expect(a.loops[0].delay == b.loops[0].delay,
                   "return stroke changed the fresh normal displacement field");
            bool opposite = true;
            for (std::size_t i = 0; i < a.loops[1].delay.size(); ++i)
                opposite &= a.loops[1].delay[i] == -b.loops[1].delay[i];
            expect(opposite && slopeEnergy(a.loops[1]) > 0.0,
                   "return stroke did not reverse a nonzero fresh sideways field");
            expect(slopeEnergy(a.loops[0]) == slopeEnergy(b.loops[0])
                       && slopeEnergy(a.loops[1]) == slopeEnergy(b.loops[1]),
                   "direction reversal changed fresh cyclic slope energy");
            expect(a.step == b.step && a.mix == b.mix,
                   "direction reversal changed static normal force or polarisation share");
            expect(a.excitation == 0.51f && b.excitation == -0.51f
                       && a.pickingRandom == b.pickingRandom
                       && a.excitationRandom == b.excitationRandom,
                   "stroke direction changed noise draws or burst magnitude");
        }
    auto implicit = fresh(), explicitDown = fresh();
    implicit->beginStrum(); explicitDown->beginStrum(0, 0, false);
    implicit->noteOn(47, 0.7f, 1, 0, true); explicitDown->noteOn(47, 0.7f, 1, 0, true);
    expect(advance(*implicit, 4096) == advance(*explicitDown, 4096),
           "default beginStrum no longer preserves explicit downstroke audio");
    auto single = fresh(), afterUpstroke = fresh();
    afterUpstroke->beginStrum(0, 0, true);
    single->noteOn(47, 0.7f, 1); afterUpstroke->noteOn(47, 0.7f, 1);
    expect(Access::source(*afterUpstroke).sign == 1.0f
               && advance(*single, 4096) == advance(*afterUpstroke, 4096),
           "ordinary single note inherited a preceding strum's direction");
}
void testScheduledDirectionsAndRetainedSources()
{
    auto e = fresh();
    e->beginStrum(0, 0, true);
    e->noteOn(47, 0.7f, 1, 200, true);
    const int wait = Access::delay(*e);
    e->beginStrum(0, 0, false);
    advance(*e, wait + 1);
    expect(Access::source(*e).sign == -1.0f && Access::gain(*e) == -0.51f,
           "later gesture changed an earlier scheduled string's release direction");

    auto continuing = fresh(48000.0, acustra::PickingTechnique::Pick,
                             acustra::GuitarModel::Original);
    continuing->beginStrum(0, 0, true);
    continuing->noteOn(47, 0.7f, 1, 0, true);
    advance(*continuing, 11);
    const auto expected = Access::futureArrivals(*continuing);
    bool nonzero = false;
    for (const auto& packet : expected) nonzero |= packet[1] != 0.0f;
    expect(nonzero, "old-contact fixture contains no sideways packets in flight");
    continuing->beginStrum(0, 0, false);
    continuing->noteOn(47, 0.7f, 1, 0, true);
    expect(Access::retainedArrivals(*continuing, expected.size()) == expected,
           "opposite continuing stroke changed old packets' future arrivals");
    expect(Access::gain(*continuing) == 0.51f && !Access::tail(*continuing),
           "continuing stroke failed to adopt its new direction in the existing string");

    auto scheduled = fresh();
    scheduled->beginStrum(0, 0, true); scheduled->noteOn(47, 0.7f, 1, 0, true);
    advance(*scheduled, 11);
    scheduled->beginStrum(0, 0, false); scheduled->noteOn(47, 0.7f, 1, 97, true);
    expect(Access::source(*scheduled).sign == 1.0f && Access::gain(*scheduled) == -0.51f,
           "pending opposite stroke changed the old source before its physical release");
    const int scheduledWait = Access::delay(*scheduled);
    advance(*scheduled, std::max(0, scheduledWait - 1));
    expect(Access::gain(*scheduled) == -0.51f,
           "old source reversed one sample before the delayed pick arrived");
    advance(*scheduled, 1);
    expect(Access::gain(*scheduled) == 0.51f,
           "scheduled opposite stroke did not adopt direction at release");

    auto refret = fresh(48000.0, acustra::PickingTechnique::Pick,
                        acustra::GuitarModel::Original);
    refret->beginStrum(0, 0, true); refret->noteOn(47, 0.7f, 1, 0, true);
    advance(*refret, 11);
    const auto old = Access::source(*refret);
    refret->beginStrum(0, 0, false); refret->noteOn(48, 0.7f, 1, 0, true);
    const auto tails = Access::tailLoops(*refret);
    expect(Access::tail(*refret) && tails[0].delay == old.loops[0].delay
               && tails[1].delay == old.loops[1].delay,
           "opposite refret stroke reversed or discarded its retained old wave");
    expect(Access::tailGain(*refret) == old.excitation
              ,
           "refret tail inherited the fresh stroke's source direction");
}
void chord(acustra::Performer& p)
{
    std::array<float, 1> left {}, right {};
    p.beginBlock(left.data(), right.data(), 1);
    for (const int note : open) p.noteOn(0, 1, note, 95);
    p.endBlock();
}
void advance(acustra::Performer& p, int samples)
{
    std::array<float, 127> left {}, right {};
    while (samples > 0)
    { const int count = std::min(samples, 127); p.process(left.data(), right.data(), count); samples -= count; }
}
void testPerformerAlternationRestAndReset()
{
    auto p = std::make_unique<acustra::Performer>();
    p->prepare(48000.0, 127); p->setGatherChords(false);
    const auto check = [&](float wanted)
    {
        for (int string = 0; string < 6; ++string)
            expect(Access::sign(p->engine(), string) == wanted,
                   "automatic stroke's string direction disagreed with wrist alternation");
    };
    chord(*p); check(1.0f); advance(*p, 12000);
    chord(*p); check(-1.0f); advance(*p, 12000);
    chord(*p); check(1.0f); advance(*p, 12000);
    chord(*p); check(-1.0f);
    p->reset(); chord(*p); check(1.0f); advance(*p, 12000);
    chord(*p); check(-1.0f); advance(*p, 96001);
    chord(*p); check(1.0f);
}
std::vector<float> phrase(double rate, int block, acustra::GuitarModel model)
{
    auto p = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = acustra::PickingTechnique::Pick;
    parameters.guitarModel = model;
    parameters.room = 0.5f;
    if (model == acustra::GuitarModel::Bellido1978)
    { parameters.shape = acustra::BodyShape::Auditorium; parameters.bodyMaterial = acustra::BodyMaterial::Mahogany; }
    p->setParameters(parameters); p->prepare(rate, block); p->setGatherChords(false);
    const int interval = static_cast<int>(std::lround(0.09 * rate));
    const int samples = 8 * interval + static_cast<int>(0.08 * rate);
    std::vector<float> left(static_cast<std::size_t>(samples)), right(left.size());
    for (int at = 0; at < samples; at += block)
    {
        const int count = std::min(block, samples - at);
        p->beginBlock(left.data() + at, right.data() + at, count);
        for (int stroke = 0; stroke < 8; ++stroke)
        {
            const int on = stroke * interval;
            const int off = on + interval - static_cast<int>(0.008 * rate);
            for (const int note : open)
            {
                if (on >= at && on < at + count) p->noteOn(on - at, 1, note, stroke % 3 == 0 ? 104 : 55);
                if (off >= at && off < at + count) p->noteOff(off - at, 1, note, 64);
            }
        }
        p->endBlock();
    }
    left.insert(left.end(), right.begin(), right.end());
    const bool finite = std::all_of(left.begin(), left.end(), [](float x)
        { return std::isfinite(x) && std::abs(x) <= 1.0f; });
    expect(finite, "repeated alternating strums produced nonfinite or unbounded audio");
    expect(std::any_of(left.begin(), left.end(), [](float x) { return x != 0.0f; }),
           "repeated alternating strums rendered silence");
    return left;
}
}
int main()
{
    testFreshReleaseAndDefaultPaths();
    testScheduledDirectionsAndRetainedSources();
    testPerformerAlternationRestAndReset();
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
        {
            const auto reference = phrase(rate, 17, model);
            expect(phrase(rate, 127, model) == reference,
                   "alternating stroke audio changed with host block partition");
        }
    if (failures == 0)
        std::cout << "Strum direction: fresh energy, scheduled releases, retained arrivals/tails, wrist reset and block invariance passed\n";
    return failures == 0 ? 0 : 1;
}
