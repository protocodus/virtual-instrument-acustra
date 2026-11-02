// The authored position-shift lift is four times the former force, not a
// change to key-up noise or continuously sliding fingers. Isolate that source
// after genuine note-on allocation, then witness it through the capture path
// at the Rack presets' 10% Release Noise. The force ratio is independent of
// the instrument's absolute handling-noise level (which is not measured).
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto& voice(AcustraEngine& engine, int string)
    { return engine.voices_[static_cast<std::size_t>(string)]; }
    static void step(AcustraEngine& engine, int string)
    { engine.renderFretMotion(voice(engine, string), string); }
    static void omitNewArrival(AcustraEngine& engine, int string)
    {
        auto& motion = voice(engine, string).fretMotion;
        // The fresh pluck has not rendered any arrival yet. Keep every
        // other source, retained wave, and random stream in this control.
        motion.length = voice(engine, string).speakingLengthMetres;
        motion.arriving = false;
    }
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

auto fresh(double rate, int block, float amount, bool audible)
{
    auto engine = std::make_unique<Engine>();
    acustra::PerformanceRealism options;
    options.audibleShiftArrivals = audible;
    engine->setPerformanceRealism(options);
    acustra::EngineParameters parameters;
    parameters.releaseNoise = amount;
    engine->setParameters(parameters);
    engine->prepare(rate, block);
    engine->setStringPerChannelMode(true);
    return engine;
}

void advance(Engine& engine, int samples)
{
    std::array<float, 127> left {}, right {};
    while (samples > 0)
    {
        const int count = std::min(samples, 127);
        engine.process(left.data(), right.data(), count);
        samples -= count;
    }
}

struct Source
{
    std::vector<float> force;
    std::vector<float> wave;
    std::uint32_t random {};
};

Source arrivalSource(double rate, int string, float amount, bool audible)
{
    auto engine = fresh(rate, 127, amount, audible);
    const int open = openNotes[static_cast<std::size_t>(string)];
    engine->noteOn(open + 5, 100.0f / 127.0f, string + 1);
    advance(*engine, static_cast<int>(0.1 * rate));
    engine->noteOn(open + 8, 100.0f / 127.0f, string + 1);
    auto& voice = Access::voice(*engine, string);
    expect(voice.fretMotion.arriving == (amount > 0.0f),
           "a genuine same-string wound shift did not schedule its arrival");
    Source result;
    const int samples = static_cast<int>(0.06 * rate);
    result.force.reserve(static_cast<std::size_t>(samples));
    result.wave.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        Access::step(*engine, string);
        result.force.push_back(voice.fretMotion.emittedForce);
        result.wave.push_back(voice.fretMotion.sourceWave);
        expect(std::isfinite(voice.fretMotion.emittedForce)
                   && std::isfinite(voice.fretMotion.sourceWave)
                   && !voice.repluckArrivals.overflow,
               "arrival left finite source/packet state");
        voice.repluckArrivals.process();
    }
    result.random = voice.fretMotionNoiseState;
    return result;
}

double energy(const std::vector<float>& x)
{
    double sum = 0.0;
    for (float sample : x) sum += double(sample) * sample;
    return sum;
}

double relativeScaleError(const std::vector<float>& actual,
                          const std::vector<float>& reference, double scale)
{
    double error = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        const double difference = actual[i] - scale * reference[i];
        error += difference * difference;
    }
    return std::sqrt(error / std::max(scale * scale * energy(reference), 1.0e-30));
}

void testSourceGainAndContinuousAmount()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int string : { 0, 2 })
        {
            const auto legacy = arrivalSource(rate, string, 0.1f, false);
            const auto audible = arrivalSource(rate, string, 0.1f, true);
            const auto half = arrivalSource(rate, string, 0.05f, true);
            const auto zero = arrivalSource(rate, string, 0.0f, true);
            expect(energy(legacy.force) > 1.0e-15,
                   "arrival gain fixture had no winding-friction force");
            expect(relativeScaleError(audible.force, legacy.force, 4.0) < 2.0e-6
                       && relativeScaleError(audible.wave, legacy.wave, 4.0) < 2.0e-6,
                   "the default arrival did not gain exactly four in force and travelling wave");
            expect(relativeScaleError(half.force, audible.force, 0.5) < 2.0e-6
                       && half.random == audible.random && audible.random == legacy.random,
                   "arrival gain quantized Release Noise or changed random draws");
            expect(energy(zero.force) == 0.0 && energy(zero.wave) == 0.0,
                   "Release Noise zero emitted a shift source");
            std::cout << "Arrival force gain at " << rate << " Hz, string " << string
                      << ": " << 10.0 * std::log10(energy(audible.force)
                                                    / energy(legacy.force)) << " dB\n";
        }
}

enum class Scenario { Shift, Stationary, Repeated, Open, Plain, MemberBend, ContinuousSlide };

std::vector<float> performance(double rate, int block, float amount, bool audible,
                               Scenario scenario, bool omitArrival = false)
{
    auto engine = fresh(rate, block, amount, audible);
    const int string = scenario == Scenario::Plain ? 4 : 1;
    const int channel = string + 1;
    const int from = openNotes[static_cast<std::size_t>(string)] + 5;
    if (scenario == Scenario::MemberBend) engine->setLowerZoneMemberCount(15);
    constexpr float velocity = 100.0f / 127.0f;
    engine->noteOn(from, velocity, channel);
    const int shift = static_cast<int>(0.16 * rate);
    const int release = static_cast<int>(0.34 * rate);
    const int samples = static_cast<int>(0.52 * rate);
    int held = from;
    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(2 * samples));
    std::array<float, 512> left {}, right {};
    for (int first = 0; first < samples;)
    {
        if (first == shift)
        {
            if (scenario == Scenario::MemberBend || scenario == Scenario::ContinuousSlide)
                engine->setPitchBend(2.0f, channel);
            else if (scenario != Scenario::Stationary)
            {
                held = scenario == Scenario::Open ? openNotes[static_cast<std::size_t>(string)]
                    : scenario == Scenario::Repeated ? from : from + 3;
                engine->noteOn(held, velocity, channel);
                if (omitArrival) Access::omitNewArrival(*engine, string);
            }
        }
        if (first == release) engine->noteOff(held, channel);
        int count = std::min({ block, samples - first, 512 });
        if (first < shift) count = std::min(count, shift - first);
        else if (first < release) count = std::min(count, release - first);
        engine->process(left.data(), right.data(), count);
        for (int sample = 0; sample < count; ++sample)
        {
            result.push_back(left[static_cast<std::size_t>(sample)]);
            result.push_back(right[static_cast<std::size_t>(sample)]);
        }
        first += count;
    }
    return result;
}

void testCaptureContributionAndUnchangedControls()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto audible = performance(rate, 64, 0.1f, true, Scenario::Shift);
        const auto legacy = performance(rate, 64, 0.1f, false, Scenario::Shift);
        const auto control = performance(rate, 64, 0.1f, true, Scenario::Shift, true);
        const int first = 2 * static_cast<int>(0.16 * rate);
        const int last = 2 * static_cast<int>(0.28 * rate);
        double loudEnergy = 0.0, oldEnergy = 0.0, noteEnergy = 0.0;
        for (int sample = first; sample < last; ++sample)
        {
            const auto i = static_cast<std::size_t>(sample);
            const double loud = audible[i] - control[i];
            const double old = legacy[i] - control[i];
            loudEnergy += loud * loud;
            oldEnergy += old * old;
            noteEnergy += double(control[i]) * control[i];
        }
        const double rise = 10.0 * std::log10(loudEnergy / std::max(oldEnergy, 1.0e-30));
        const double relative = 10.0 * std::log10(loudEnergy / std::max(noteEnergy, 1.0e-30));
        expect(rise > 11.5 && rise < 12.5,
               "arrival did not reach the capture path with its authored 12 dB lift");
        expect(relative > -70.0 && relative < -10.0,
               "default arrival was numerically absent or overwhelmed the note");
        expect(std::all_of(audible.begin(), audible.end(), [](float sample)
            { return std::isfinite(sample) && std::abs(sample) <= 0.891251f; }),
            "ordinary 10% arrival left finite capture headroom");
        std::cout << "Shift capture at " << rate << " Hz: +" << rise
                  << " dB, contribution " << relative << " dB relative to note\n";

        for (const auto scenario : { Scenario::Stationary, Scenario::Repeated,
                                     Scenario::Open, Scenario::Plain, Scenario::MemberBend,
                                     Scenario::ContinuousSlide })
            expect(performance(rate, 127, 0.1f, true, scenario)
                       == performance(rate, 127, 0.1f, false, scenario),
                   "arrival lift changed an ineligible or continuously sliding control");
        expect(performance(rate, 127, 0.0f, true, Scenario::Shift)
                   == performance(rate, 127, 0.0f, false, Scenario::Shift),
               "Release Noise zero no longer bypasses the arrival lift bit exactly");
    }
}

void testResetAndPartition()
{
    const auto reference = performance(48000.0, 64, 0.1f, true, Scenario::Shift);
    for (int block : { 1, 17, 127, 512 })
        expect(performance(48000.0, block, 0.1f, true, Scenario::Shift) == reference,
               "arrival changed with host block partition");
    auto engine = fresh(48000.0, 64, 0.1f, true);
    engine->noteOn(50, 100.0f / 127.0f, 2);
    advance(*engine, 4800);
    engine->noteOn(53, 100.0f / 127.0f, 2);
    advance(*engine, 128);
    expect(Access::voice(*engine, 1).fretMotion.sourceWave != 0.0f,
           "reset fixture had no active arrival");
    engine->reset();
    for (int string = 0; string < 6; ++string)
    {
        const auto& voice = Access::voice(*engine, string);
        expect(!voice.fretMotion.arriving && voice.fretMotion.sourceWave == 0.0f
                   && voice.repluckArrivals.remaining == 0
                   && voice.tailRepluckArrivals.remaining == 0,
               "reset retained arrival state or travelling packets");
    }
    std::array<float, 64> left {}, right {};
    engine->process(left.data(), right.data(), 64);
    expect(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0f; })
               && std::all_of(right.begin(), right.end(), [](float x) { return x == 0.0f; }),
           "reset left an audible arrival");
    engine->setStringPerChannelMode(true);
    engine->noteOn(50, 100.0f / 127.0f, 2);
    advance(*engine, 512);
    expect(!Access::voice(*engine, 1).fretMotion.arriving
               && Access::voice(*engine, 1).fretMotion.sourceWave == 0.0f,
           "the first contact after reset slid from a forgotten position");
}
}

int main()
{
    testSourceGainAndContinuousAmount();
    testCaptureContributionAndUnchangedControls();
    testResetAndPartition();
    return failures == 0 ? 0 : 1;
}
