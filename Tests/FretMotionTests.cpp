// A held finger's longitudinal movement is distinct from a fixed-fret
// tension bend. Verify physical motion and its emitted packets separately
// from the sounding string, then exercise the public MIDI render path.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto& voice(AcustraEngine& engine, int string)
    { return engine.voices_[static_cast<std::size_t>(string)]; }
    static const auto& voice(const AcustraEngine& engine, int string)
    { return engine.voices_[static_cast<std::size_t>(string)]; }
    static void prime(AcustraEngine& engine, int string)
    { AcustraEngine::primeFretMotion(voice(engine, string)); }
    static void step(AcustraEngine& engine, int string)
    { engine.renderFretMotion(voice(engine, string), string); }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> openNotes { 40, 45, 50, 55, 59, 64 };
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

void advance(Engine& engine, int samples, int block = 127)
{
    std::array<float, 512> left {}, right {};
    while (samples > 0)
    {
        const int count = std::min({ samples, block, 512 });
        engine.process(left.data(), right.data(), count);
        samples -= count;
    }
}

auto fresh(double rate = 48000.0, int string = 0, int fret = 5,
           float amount = 0.7f, bool member = false)
{
    auto engine = std::make_unique<Engine>();
    acustra::EngineParameters parameters;
    // Exercise normal Rack behavior explicitly; the standalone engine's
    // bare default intentionally keeps this optional sound at zero.
    parameters.releaseNoise = amount;
    engine->setParameters(parameters);
    engine->prepare(rate, 127);
    engine->setStringPerChannelMode(true);
    if (member) engine->setLowerZoneMemberCount(15);
    engine->noteOn(openNotes[static_cast<std::size_t>(string)] + fret,
                   0.7f, string + 1);
    advance(*engine, static_cast<int>(0.08 * rate));
    return engine;
}

auto isolated(double rate = 48000.0, int string = 0, float amount = 0.7f,
              float length = 0.48f)
{
    auto engine = std::make_unique<Engine>();
    acustra::EngineParameters parameters;
    parameters.releaseNoise = amount;
    engine->setParameters(parameters);
    engine->prepare(rate, 127);
    auto& voice = Access::voice(*engine, string);
    voice.played = voice.keyDown = voice.attackFired = true;
    voice.harmonic = 1;
    voice.midiNote = openNotes[static_cast<std::size_t>(string)] + 5;
    voice.speakingLengthMetres = length;
    voice.characteristicImpedance = 0.5f;
    voice.appliedBendImpedanceScale = 1.0f;
    voice.peakLevel = 0.4f;
    voice.loops[0].currentDelay = 384.5f;
    Access::prime(*engine, string);
    return engine;
}

void testPhysicalTrajectoryAndInactiveCases()
{
    double worstLength = 0.0, worstSpeed = 0.0, worstRoundingFraction = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engine = isolated(rate);
        auto& voice = Access::voice(*engine, 0);
        const double initial = voice.fretMotion.length;
        voice.speakingLengthMetres = 0.37f;
        const double target = voice.speakingLengthMetres;
        const double alpha = 1.0 - std::exp(-1.0 / (0.006 * rate));
        // The engine stores 1-exp(-1/(tau*rate)) as float: rounding exp near
        // one dominates the subtraction. A quarter epsilon bounds its half
        // ulp below one; the additional term covers the float tau/product.
        const double coefficientError = std::numeric_limits<float>::epsilon()
            * (0.25 + 4.0 * alpha);
        const auto attackRandom = voice.randomState;
        const auto releaseRandom = voice.releaseNoiseState;
        const int samples = static_cast<int>(0.036 * rate);
        double previous = initial;
        for (int i = 1; i <= samples; ++i)
        {
            Access::step(*engine, 0);
            const auto& motion = voice.fretMotion;
            const double expected = target + (initial - target)
                * std::exp(-static_cast<double>(i) / (0.006 * rate));
            worstLength = std::max(worstLength, std::abs(motion.length - expected));
            const double roundingBound = std::abs(initial - target) * i
                * std::pow(1.0 - alpha + coefficientError, i - 1) * coefficientError;
            worstRoundingFraction = std::max(worstRoundingFraction,
                std::abs(motion.length - expected) / (roundingBound + 1.0e-10));
            const double expectedSpeed = (motion.length - previous) * rate;
            worstSpeed = std::max(worstSpeed, std::abs(motion.speed - expectedSpeed));
            expect(motion.length <= previous && motion.length >= target,
                   "six-millisecond stop trajectory overshot or moved backwards");
            expect(std::isfinite(motion.emittedForce) && std::isfinite(motion.sourceWave)
                       && !voice.repluckArrivals.overflow,
                   "moving finger produced a nonfinite source or arrival overflow");
            previous = motion.length;
            voice.repluckArrivals.process();
        }
        expect(voice.randomState == attackRandom && voice.releaseNoiseState == releaseRandom,
               "slide friction consumed a picking or key-up random stream");
    }
    expect(worstRoundingFraction <= 1.0 && worstSpeed < 2.0e-6,
           "physical motion differs from the independently integrated six-millisecond path");
    std::cout << "Fret motion trajectory error: length=" << worstLength
              << " m (" << worstRoundingFraction << " of float bound), speed="
              << worstSpeed << " m/s\n";

    for (int condition = 0; condition < 8; ++condition)
    {
        const int string = condition == 0 ? 4 : condition == 1 ? 5 : 0;
        auto engine = isolated(48000.0, string, condition == 7 ? 0.0f : 0.7f);
        auto& voice = Access::voice(*engine, string);
        if (condition == 2) voice.played = false;
        if (condition == 3) voice.attackFired = false;
        if (condition == 4) voice.keyDown = false;
        if (condition == 5) voice.harmonic = 2;
        // A stationary held finger is also exactly silent.
        if (condition != 6) voice.speakingLengthMetres = 0.37f;
        const auto random = voice.fretMotionNoiseState;
        for (int i = 0; i < 256; ++i) Access::step(*engine, string);
        expect(voice.fretMotionNoiseState == random
                   && voice.fretMotion.emittedForce == 0.0f
                   && voice.fretMotion.sourceWave == 0.0f
                   && voice.repluckArrivals.remaining == 0,
               "ineligible or stationary finger emitted friction: case " + std::to_string(condition));
    }
}

void testLocalPacketGeometry()
{
    auto engine = isolated();
    auto& voice = Access::voice(*engine, 0);
    voice.speakingLengthMetres = 0.46f;
    Access::step(*engine, 0);
    const double source = voice.fretMotion.sourceWave;
    expect(source != 0.0, "packet fixture emitted no movement source");
    std::array<double, 2> net {}, direct {}, reflected {}, directMoment {}, reflectedMoment {};
    const auto& queue = voice.repluckArrivals;
    for (int i = 0; i < queue.remaining; ++i)
        for (int plane = 0; plane < 2; ++plane)
        {
            const double value = queue.wave[static_cast<std::size_t>(plane)]
                [static_cast<std::size_t>((queue.readIndex + i) % queue.capacity)] / source;
            net[static_cast<std::size_t>(plane)] += value;
            if (value > 0.0)
            {
                direct[static_cast<std::size_t>(plane)] += value;
                directMoment[static_cast<std::size_t>(plane)] += i * value;
            }
            else
            {
                reflected[static_cast<std::size_t>(plane)] -= value;
                reflectedMoment[static_cast<std::size_t>(plane)] -= i * value;
            }
        }
    // Independent geometry: the effective contact lies 18 mm bridgeward
    // of the stopping finger. Each travelling path keeps its fractional
    // time; the fixed end reverses the displacement, so total DC is zero.
    const double period = voice.loops[0].currentDelay;
    const double position = 1.0 - 0.018 / voice.fretMotion.length;
    const double directTime = 0.5 * position * period;
    const double reflectedTime = period - directTime;
    for (std::size_t plane = 0; plane < 2; ++plane)
    {
        expect(std::abs(net[plane]) < 2.0e-7 && direct[plane] > 0.0,
               "moving contact lost its fixed-end polarity or directional split");
        expect(std::abs(directMoment[plane] / direct[plane] - directTime) < 4.0e-5
                   && std::abs(reflectedMoment[plane] / reflected[plane] - reflectedTime) < 4.0e-5,
               "moving contact packet was quantized or teleported to the bridge");
    }
    expect(std::abs(direct[0] * direct[0] + direct[1] * direct[1] - 1.0) < 2.0e-7,
           "motion source polarization created or removed directional wave energy");
}

void testFixedFretExpressionsAndPriming()
{
    for (int delay : { 0, 480 })
    {
        auto engine = std::make_unique<Engine>();
        acustra::EngineParameters p;
        p.releaseNoise = 0.7f;
        engine->setParameters(p);
        engine->prepare(48000.0, 127);
        engine->setStringPerChannelMode(true);
        const auto random = Access::voice(*engine, 0).fretMotionNoiseState;
        engine->setPitchBend(1.0f, 1);
        engine->noteOn(48, 0.7f, 1, delay);
        advance(*engine, 256);
        if (delay > 0) engine->setPitchBend(2.0f, 1);
        advance(*engine, 512);
        expect(Access::voice(*engine, 0).fretMotionNoiseState == random,
               "first or queued contact scraped from the previous idle stopping point");
    }
    for (bool member : { false, true })
    {
        // Channel 2 fixes this fixture to the A string and is a valid member.
        auto engine = fresh(48000.0, 1, 5, 0.7f, member);
        auto& voice = Access::voice(*engine, 1);
        const auto random = voice.fretMotionNoiseState;
        if (member) engine->setPitchBend(2.0f, 2);
        engine->setVibrato(1.0f);
        engine->setMpePressure(1.0f, 2);
        advance(*engine, 9600);
        expect(voice.fretMotionNoiseState == random && voice.fretMotion.speed == 0.0f,
               "fixed-fret tension bend, pressure or vibrato became a longitudinal scrape");
        engine->setPitchBend(2.0f, member ? 1 : 2);
        advance(*engine, 512);
        expect(voice.fretMotionNoiseState != random && voice.fretMotion.speed < 0.0f,
               "conventional or manager slide did not move a held finger");
    }
    for (int action = 0; action < 5; ++action)
    {
        auto engine = fresh();
        auto& voice = Access::voice(*engine, 0);
        const auto random = voice.fretMotionNoiseState;
        if (action == 0) engine->noteOn(45, 0.7f, 1);
        if (action == 1) engine->noteOn(48, 0.7f, 1);
        if (action == 2)
            expect(engine->transitionNote(45, 48, 0.7f, 1), "connected priming fixture failed");
        if (action >= 3)
        {
            acustra::EngineParameters p;
            p.releaseNoise = 0.7f;
            p.bodyMaterial = acustra::BodyMaterial::Maple;
            if (action == 4) p.tuning = acustra::Tuning::DropD;
            engine->setParameters(p);
        }
        advance(*engine, 512);
        expect(voice.fretMotionNoiseState == random,
               "repluck, refret, connected articulation or construction caused fictitious sliding");
    }
}

void testMotionBoundariesAreContinuous()
{
    // The same speed in either direction has the same roughness force.
    auto left = isolated(48000.0, 0, 0.7f, 0.5f);
    auto right = isolated(48000.0, 0, 0.7f, 0.5f);
    auto& a = Access::voice(*left, 0);
    auto& b = Access::voice(*right, 0);
    a.speakingLengthMetres = 0.48f;
    b.speakingLengthMetres = 0.52f;
    Access::step(*left, 0); Access::step(*right, 0);
    expect(a.fretMotion.speed < 0.0f && b.fretMotion.speed > 0.0f
               && std::abs(a.fretMotion.emittedForce / b.fretMotion.emittedForce - 1.0f) < 1.0e-5f,
           "sliding direction changed the roughness force at the same speed");

    for (float boundary : { 0.648f, 0.648f - 0.018f })
    {
        std::array<double, 2> gap {};
        int index = 0;
        for (float epsilon : { 0.001f, 0.00001f })
        {
            std::array<float, 2> force {};
            for (int side = 0; side < 2; ++side)
            {
                const float length = boundary + (side == 0 ? -epsilon : epsilon);
                auto engine = isolated(48000.0, 0, 0.7f, length);
                auto& voice = Access::voice(*engine, 0);
                voice.speakingLengthMetres = length - 0.0001f;
                Access::step(*engine, 0);
                force[static_cast<std::size_t>(side)] = voice.fretMotion.emittedForce;
            }
            gap[static_cast<std::size_t>(index++)] = std::abs(double(force[1]) - force[0]);
        }
        expect(gap[0] > 0.0 && gap[1] < 0.02 * gap[0],
               "source jumps at the nut or the finite finger-contact boundary");
    }
}

void testAmountContinuityAndRateNormalisation()
{
    std::array<double, 3> forceRms {};
    std::size_t rateIndex = 0;
    constexpr double speed = 0.1;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engine = isolated(rate);
        auto& voice = Access::voice(*engine, 0);
        const double initial = voice.speakingLengthMetres;
        double energy = 0.0;
        int count = 0;
        for (int i = 0; i < static_cast<int>(0.3 * rate); ++i)
        {
            // A uniform physical slide, independent of host sample rate.
            voice.speakingLengthMetres = static_cast<float>(initial - speed * (i + 1) / rate);
            Access::step(*engine, 0);
            voice.repluckArrivals.process();
            if (i >= static_cast<int>(0.06 * rate))
            {
                energy += double(voice.fretMotion.emittedForce) * voice.fretMotion.emittedForce;
                ++count;
            }
        }
        forceRms[rateIndex++] = std::sqrt(energy / count);
        // The specified subtle-force reference is an authored amount, not
        // a measurement of finger force. Check its realised RMS against an
        // independent physical-time white-noise/filter-energy prediction.
        const double reference = 0.00004 * 0.4 * (0.7 / 0.5)
            * (2.0 * speed / (speed + 0.25));
        expect(std::abs(forceRms[rateIndex - 1] / reference - 1.0) < 0.20,
               "constant-speed friction RMS drifted with sample rate or lost its subtle force scale");
        expect(std::abs(voice.fretMotion.speed + speed) < 0.0001,
               "sample-rate-dependent longitudinal speed changed the noise driver");
    }
    std::cout << "Constant 0.1 m/s friction RMS at 44.1/48/96k: "
              << forceRms[0] << ", " << forceRms[1] << ", " << forceRms[2] << '\n';

    auto a = isolated(48000.0, 0, 0.3f);
    auto b = isolated(48000.0, 0, 0.3001f);
    auto& va = Access::voice(*a, 0);
    auto& vb = Access::voice(*b, 0);
    va.speakingLengthMetres = vb.speakingLengthMetres = 0.46f;
    double error = 0.0, reference = 0.0;
    for (int i = 0; i < 256; ++i)
    {
        Access::step(*a, 0); Access::step(*b, 0);
        const double expected = va.fretMotion.emittedForce * (double(0.3001f) / double(0.3f));
        error += std::pow(vb.fretMotion.emittedForce - expected, 2);
        reference += expected * expected;
        va.repluckArrivals.process(); vb.repluckArrivals.process();
    }
    expect(reference > 0.0 && std::sqrt(error / reference) < 2.0e-6,
           "nearby floating-point noise amounts hit a layer or changed random history");
}

void testPacketsAndSourceLifetimes()
{
    for (bool refret : { false, true })
    {
        auto engine = fresh();
        auto& voice = Access::voice(*engine, 0);
        voice.contactTravel.active = false;
        voice.legatoContactTravel.active = false;
        voice.legatoContactSamples = 0;
        voice.repluckArrivals.clear();
        voice.fretMotion.length += 0.001;
        Access::step(*engine, 0);
        const auto& beforeMotion = voice.fretMotion;
        const double priorLength = beforeMotion.length;
        using Queue = std::remove_reference_t<decltype(voice.repluckArrivals)>;
        auto packets = std::make_unique<Queue>(voice.repluckArrivals);
        expect(packets->remaining > 0, "packet retention fixture contained no arrivals");
        engine->noteOn(refret ? 48 : 45, 0.7f, 1);
        const auto& retained = refret ? voice.tailRepluckArrivals : voice.repluckArrivals;
        expect(retained.readIndex == packets->readIndex && retained.remaining == packets->remaining
                   && retained.wave == packets->wave && !retained.overflow,
               "repluck/refret dropped, duplicated or retimed emitted movement packets");
        if (refret)
            expect(voice.repluckArrivals.remaining == 0
                       && voice.fretMotion.length == voice.speakingLengthMetres
                       && voice.fretMotion.sourceWave == 0.0f,
                   "refret kept the old source or duplicated its arrivals in the new string");
        else
            expect(voice.fretMotion.length == priorLength,
                   "same-pitch continuing repluck restarted the sliding hand");
    }

    auto engine = isolated();
    auto& voice = Access::voice(*engine, 0);
    voice.speakingLengthMetres = 0.46f;
    for (int i = 0; i < 40; ++i)
    {
        Access::step(*engine, 0);
        voice.repluckArrivals.process();
    }
    const auto random = voice.fretMotionNoiseState;
    const double contactAtKeyUp = voice.fretMotion.length;
    voice.keyDown = false;
    voice.speakingLengthMetres = 0.43f;
    for (int i = 0; i < 6400; ++i)
    {
        Access::step(*engine, 0);
        if (i < 256)
            expect(voice.fretMotion.length == contactAtKeyUp,
                   "key-up teleported the stored friction tail to another contact point");
        voice.repluckArrivals.process();
    }
    expect(voice.fretMotionNoiseState == random,
           "key-up continued drawing fresh friction instead of draining stored contact");
    expect(voice.fretMotion.remaining == 0 && voice.fretMotion.sourceWave == 0.0f
               && voice.repluckArrivals.remaining == 0,
           "released movement source or emitted packets failed to finish");

    auto zero = isolated();
    auto& vz = Access::voice(*zero, 0);
    vz.speakingLengthMetres = 0.46f;
    Access::step(*zero, 0);
    const auto zeroRandom = vz.fretMotionNoiseState;
    using Queue = std::remove_reference_t<decltype(vz.repluckArrivals)>;
    auto pending = std::make_unique<Queue>(vz.repluckArrivals);
    acustra::EngineParameters bypass;
    bypass.releaseNoise = 0.0f;
    zero->setParameters(bypass);
    Access::step(*zero, 0);
    expect(vz.fretMotion.sourceWave == 0.0f && vz.fretMotion.emittedForce == 0.0f
               && vz.fretMotionNoiseState == zeroRandom,
           "zero amount did not stop the movement source immediately");
    expect(vz.repluckArrivals.wave == pending->wave
               && vz.repluckArrivals.remaining == pending->remaining,
           "zero amount recalled packets already travelling through the string");
}

void testResetAndScopedPanic()
{
    auto engine = fresh();
    engine->noteOn(52, 0.6f, 2);
    engine->setPitchBend(2.0f, 1);
    advance(*engine, 512);
    auto& voice = Access::voice(*engine, 0);
    expect(voice.fretMotion.remaining > 0 && voice.repluckArrivals.remaining > 0,
           "panic fixture had no moving source or pending arrivals");
    engine->allSoundOff(1);
    expect(engine->heldString(52, 2) == 1, "channel panic stopped another string");
    expect(voice.fretMotion.remaining == 0 && voice.fretMotion.sourceWave == 0.0f
               && voice.repluckArrivals.remaining == 0 && voice.tailRepluckArrivals.remaining == 0,
           "channel panic left a source or movement packet while another string sounded");
    engine->reset();
    engine->setStringPerChannelMode(true);
    engine->noteOn(45, 0.7f, 1);
    advance(*engine, 3840);
    const auto random = voice.fretMotionNoiseState;
    expect(voice.fretMotion.sourceWave == 0.0f && voice.fretMotion.speed == 0.0f,
           "reset made the next first contact a scrape");
    engine->setPitchBend(2.0f, 1);
    advance(*engine, 512);
    expect(voice.fretMotionNoiseState != random, "reset prevented subsequent valid motion");
    engine->setLowerZoneMemberCount(15);
    expect(voice.fretMotion.remaining == 0 && voice.repluckArrivals.remaining == 0,
           "MPE layout reset retained a movement source or packet");
}

struct Event
{
    int sample;
    std::array<std::uint8_t, 3> bytes;
};

std::vector<float> performance(int block, bool duplicates, bool gather,
                               float amount = 0.7f)
{
    constexpr int rate = 48000, length = 38400;
    auto player = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.releaseNoise = amount;
    player->setParameters(parameters);
    player->prepare(rate, block);
    player->setGatherChords(gather);
    std::vector<Event> events {
        { 0, { 0xb0, 126, 6 } }, { 0, { 0x90, 45, 96 } },
        { 28800, { 0x80, 45, 64 } }, { 33600, { 0xb0, 120, 0 } }
    };
    for (int step = 0; step <= 24; ++step)
    {
        const int raw = 8192 + (step <= 12 ? step : 24 - step) * 384;
        const Event event { 4800 + step * 640,
            { 0xe0, static_cast<std::uint8_t>(raw & 127),
              static_cast<std::uint8_t>(raw >> 7) } };
        events.push_back(event);
        if (duplicates)
        {
            events.push_back(event);
            auto repeat = event;
            repeat.sample += 173;
            events.push_back(repeat);
        }
    }
    std::stable_sort(events.begin(), events.end(),
        [](const Event& a, const Event& b) { return a.sample < b.sample; });
    std::vector<float> result;
    result.reserve(2 * length);
    std::array<float, 512> left {}, right {};
    std::size_t next = 0;
    for (int first = 0; first < length; first += block)
    {
        const int count = std::min(block, length - first);
        player->beginBlock(left.data(), right.data(), count);
        while (next < events.size() && events[next].sample < first + count)
        {
            const auto& event = events[next++];
            player->handleMidi(event.sample - first, event.bytes.data(), 3);
        }
        player->endBlock();
        for (int sample = 0; sample < count; ++sample)
        {
            result.push_back(left[static_cast<std::size_t>(sample)]);
            result.push_back(right[static_cast<std::size_t>(sample)]);
        }
    }
    return result;
}

void testSampleTimedMidi()
{
    for (bool gather : { false, true })
    {
        const auto reference = performance(64, false, gather);
        const auto bypass = performance(64, false, gather, 0.0f);
        expect(std::all_of(reference.begin(), reference.end(), [](float x)
            { return std::isfinite(x) && std::abs(x) < 0.89125094f; }),
            "ordinary moving-hand performance left finite capture headroom");
        // Compare only the held slide, before the old key-up noise can
        // distinguish enabled and bypassed performances by itself.
        double motionDifference = 0.0;
        for (int sample = 2 * 6000; sample < 2 * 22000; ++sample)
        {
            const double difference = reference[static_cast<std::size_t>(sample)]
                - bypass[static_cast<std::size_t>(sample)];
            motionDifference += difference * difference;
        }
        expect(motionDifference > 1.0e-15,
               "held longitudinal slide produced no audible-path contribution");
        for (int block : { 1, 17, 127, 512 })
            expect(performance(block, false, gather) == reference,
                   "moving-hand MIDI changed with host block partition");
        expect(performance(127, true, gather) == reference,
               "unchanged duplicate pitch wheels changed friction or RNG");
    }
}
}

int main()
{
    testPhysicalTrajectoryAndInactiveCases();
    testLocalPacketGeometry();
    testFixedFretExpressionsAndPriming();
    testMotionBoundariesAreContinuous();
    testAmountContinuityAndRateNormalisation();
    testPacketsAndSourceLifetimes();
    testResetAndScopedPanic();
    testSampleTimedMidi();
    return failures == 0 ? 0 : 1;
}
