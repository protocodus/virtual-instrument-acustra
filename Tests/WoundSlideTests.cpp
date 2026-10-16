// Independent witnesses for winding-rate structure and axial contact colour.
// The two switches are offline ablations, not gain controls for the audition.
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
    static auto& voice(AcustraEngine& engine, int string = 0)
    { return engine.voices_[static_cast<std::size_t>(string)]; }
    static void prime(AcustraEngine& engine, int string = 0)
    { AcustraEngine::primeFretMotion(voice(engine, string)); }
    static void step(AcustraEngine& engine, int string = 0)
    { engine.renderFretMotion(voice(engine, string), string); }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr double tau = 6.2831853071795864769;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

acustra::PerformanceRealism options(bool ridges, bool axial)
{
    acustra::PerformanceRealism result;
    result.woundRidges = ridges;
    result.longitudinalSqueak = axial;
    return result;
}

auto isolated(double rate, bool ridges, bool axial, int string = 0)
{
    auto engine = std::make_unique<Engine>();
    engine->setPerformanceRealism(options(ridges, axial));
    acustra::EngineParameters parameters;
    parameters.releaseNoise = 0.1f;
    engine->setParameters(parameters);
    engine->prepare(rate, 127);
    auto& voice = Access::voice(*engine, string);
    voice.played = voice.keyDown = voice.attackFired = true;
    voice.harmonic = 1;
    voice.speakingLengthMetres = 0.55f;
    voice.characteristicImpedance = 0.5f;
    voice.appliedBendImpedanceScale = 1.0f;
    voice.peakLevel = 0.4f;
    voice.loops[0].currentDelay = 384.5f;
    Access::prime(*engine, string);
    return engine;
}

struct Source
{
    std::vector<float> force, ridge, axial;
    std::array<float, 3> axialFrequency {};
    double rms {};
};

Source source(double rate, double speed, bool ridges, bool axial,
              double seconds = 0.35, int string = 0)
{
    auto engine = isolated(rate, ridges, axial, string);
    auto& voice = Access::voice(*engine, string);
    Source result;
    const int warmup = static_cast<int>(0.05 * rate);
    const int samples = static_cast<int>(seconds * rate);
    const double start = voice.speakingLengthMetres;
    for (int i = 0; i < warmup + samples; ++i)
    {
        voice.speakingLengthMetres = static_cast<float>(start - speed * (i + 1) / rate);
        Access::step(*engine, string);
        voice.repluckArrivals.process();
        voice.axialContactArrivals.process();
        expect(std::isfinite(voice.fretMotion.emittedForce)
                   && std::isfinite(voice.fretMotion.sourceWave)
                   && !voice.repluckArrivals.overflow,
               "contact left finite bounded source/packet state");
        if (i >= warmup)
        {
            result.force.push_back(voice.fretMotion.emittedForce);
            result.ridge.push_back(voice.fretMotion.ridgeForce);
            result.axial.push_back(voice.fretMotion.axialForce);
            result.rms += double(voice.fretMotion.emittedForce) * voice.fretMotion.emittedForce;
        }
    }
    result.rms = std::sqrt(result.rms / samples);
    result.axialFrequency = voice.fretMotion.axialFrequency;
    return result;
}

double power(const std::vector<float>& values, double rate, double frequency)
{
    double real = 0.0, imaginary = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const double angle = tau * frequency * static_cast<double>(i) / rate;
        const double window = 0.5 - 0.5 * std::cos(tau * i / (values.size() - 1));
        real += window * values[i] * std::cos(angle);
        imaginary += window * values[i] * std::sin(angle);
    }
    return (real * real + imaginary * imaginary) / (values.size() * values.size());
}

double band(const std::vector<float>& values, double rate, double centre)
{
    double sum = 0.0;
    for (int i = -3; i <= 3; ++i) sum += power(values, rate, centre + 10.0 * i);
    return sum;
}

void testSeparateSpectralWitnesses()
{
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0 })
    {
        for (double speed : { 0.1, 0.2 })
        {
            const auto plain = source(rate, speed, false, false);
            const auto ridge = source(rate, speed, true, false);
            const double frequency = 4000.0 * speed;
            const double line = power(ridge.force, rate, frequency);
            expect(line > 5.0 * power(plain.force, rate, frequency),
                   "winding structure did not add a speed-tracking spectral line");
            expect(power(ridge.ridge, rate, frequency)
                       > 100.0 * power(ridge.ridge, rate, frequency + 50.0),
                   "ridge branch is diffuse noise rather than winding structure");
            expect(ridge.axial == plain.axial,
                   "ridge-only ablation activated the axial branch");
            expect(ridge.rms / plain.rms > 0.80 && ridge.rms / plain.rms < 1.20,
                   "winding texture raised the overall force instead of reallocating it");
        }
    }

    const auto slow = source(48000.0, 0.1, false, true);
    const auto fast = source(48000.0, 0.2, false, true);
    expect(slow.axialFrequency == fast.axialFrequency,
           "axial frequency followed finger speed");
    expect(slow.ridge == source(48000.0, 0.1, false, false).ridge,
           "axial-only ablation activated winding harmonics");
    // Independent EA/mu prediction from the shipped physical inputs.
    const double transverse = 2.0 * 0.648 * 440.0 * std::pow(2.0, (40.0 - 69.0) / 12.0);
    const double mass = 110.759 / (transverse * transverse);
    const double rigidity = 2.0e11 * (tau / 8.0) * 0.000477159 * 0.000477159;
    const double fundamental = std::sqrt(rigidity / mass) / (2.0 * 0.648);
    expect(std::abs(slow.axialFrequency[0] / fundamental - 1.0) < 1.0e-5,
           "axial colour lost its tension-independent material frequency");
    const double modal = band(slow.axial, 48000.0, fundamental);
    const double beside = 0.5 * (band(slow.axial, 48000.0, fundamental - 180.0)
                             + band(slow.axial, 48000.0, fundamental + 180.0));
    expect(modal > 8.0 * beside, "axial branch has no separate narrow modal witness");
    std::cout << "Axial fundamental " << slow.axialFrequency[0]
              << " Hz, modal/adjacent power " << modal / beside << '\n';
}

void testAliasingAndRate()
{
    // Even impossible controller speed must not fold a 12 kHz ridge into
    // an 8 kHz engine. Inspect the branch, not a pluck's unrelated spectrum.
    const auto fast = source(8000.0, 3.0, true, false, 0.05);
    expect(std::all_of(fast.ridge.begin(), fast.ridge.end(), [](float x) { return x == 0.0f; }),
           "above-band winding rate folded into audible output");
    const auto partial = source(8000.0, 0.7, true, false, 0.15);
    expect(power(partial.ridge, 8000.0, 2800.0)
               > 1.0e5 * power(partial.ridge, 8000.0, 2400.0),
           "second winding harmonic folded back at low sample rate");
    std::array<double, 3> rms {};
    int i = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        rms[static_cast<std::size_t>(i++)] = source(rate, 0.1, true, true).rms;
    const auto limits = std::minmax_element(rms.begin(), rms.end());
    expect(*limits.second / *limits.first < 1.20,
           "combined contact RMS depends materially on sample rate");
    std::cout << "Combined default-amount force RMS 44.1/48/96k: "
              << rms[0] << ", " << rms[1] << ", " << rms[2] << '\n';
}

void testLifecycleAndPosition()
{
    auto engine = isolated(48000.0, true, true);
    auto& voice = Access::voice(*engine);
    voice.speakingLengthMetres = 0.50f;
    for (int i = 0; i < 1000; ++i)
    { Access::step(*engine); voice.repluckArrivals.process(); voice.axialContactArrivals.process(); }
    const auto frequency = voice.fretMotion.axialFrequency;
    voice.appliedBendImpedanceScale = 1.4f;
    voice.pitchGeometry.value.frequency *= 2.0f;
    Access::step(*engine);
    expect(voice.fretMotion.axialFrequency == frequency,
           "transverse tension/pitch changed axial mode frequencies");
    voice.keyDown = false;
    const auto random = voice.fretMotionNoiseState;
    for (int i = 0; i < 7000; ++i)
    { Access::step(*engine); voice.repluckArrivals.process(); voice.axialContactArrivals.process(); }
    expect(voice.fretMotionNoiseState == random && voice.fretMotion.remaining == 0
               && voice.fretMotion.ridgeForce == 0.0f && voice.fretMotion.axialForce == 0.0f
               && voice.fretMotion.axialQ == std::array<float, 3>{}
               && voice.axialContactArrivals.remaining == 0
               && voice.repluckArrivals.remaining == 0,
           "key-up failed to drain both source branches and their packets");

    voice.keyDown = true;
    voice.speakingLengthMetres = 0.45f;
    Access::step(*engine);
    const auto packets = voice.repluckArrivals.remaining;
    acustra::EngineParameters p;
    p.releaseNoise = 0.0f;
    engine->setParameters(p);
    Access::step(*engine);
    expect(voice.fretMotion.emittedForce == 0.0f && voice.fretMotion.ridgeForce == 0.0f
               && voice.fretMotion.axialQ == std::array<float, 3>{}
               && voice.fretMotion.axialP == std::array<float, 3>{}
               && voice.repluckArrivals.remaining == packets,
           "zero amount did not stop new colour or recalled in-flight packets");
    Access::prime(*engine);
    expect(!voice.fretMotion.colourPrepared && voice.fretMotion.ridgePhase == 0.0,
           "new contact retained old oscillator history");

    // A fixed axial boundary has a node for even modes at its midpoint.
    auto midpoint = isolated(48000.0, false, true);
    auto& middle = Access::voice(*midpoint);
    middle.fretMotion.length = 0.342000001;
    middle.speakingLengthMetres = 0.342f;
    Access::step(*midpoint);
    expect(std::abs(middle.fretMotion.axialWeight[1]) < 1.0e-6f
               && std::abs(middle.fretMotion.axialWeight[0]) > 0.1f,
           "contact position did not suppress the axial midpoint's even mode");
}

void testAxialForcePropagation()
{
    for (double rate : { 8000.0, 48000.0, Engine::maximumSupportedSampleRate })
    {
        auto engine = isolated(rate, false, true);
        auto& voice = Access::voice(*engine);
        voice.speakingLengthMetres = 0.50f;
        Access::step(*engine);
        const float force = voice.fretMotion.axialForce;
        expect(force != 0.0f, "axial travel fixture emitted no force");
        const double arrival = (voice.fretMotion.length - 0.018)
            * rate / voice.fretMotion.axialSpeed;
        double sum = 0.0, moment = 0.0;
        const auto queued = voice.axialContactArrivals.force;
        for (int sample = 0; sample < voice.axialContactArrivals.remaining; ++sample)
        {
            const double packet = queued[static_cast<std::size_t>(sample)] / force;
            sum += packet;
            moment += sample * packet;
            if (sample < static_cast<int>(arrival))
                expect(packet == 0.0, "axial force reached the bridge before its flight time");
        }
        expect(std::abs(sum - 1.0) < 2.0e-7 && std::abs(moment / sum - arrival) < 8.0e-5,
               "axial force lost amplitude or fractional propagation time");
        const auto remaining = voice.axialContactArrivals.remaining;
        Access::prime(*engine);
        expect(voice.axialContactArrivals.force == queued
                   && voice.axialContactArrivals.remaining == remaining,
               "new contact recalled an already emitted axial packet");
        engine->allSoundOff(1);
        expect(voice.axialContactArrivals.remaining == 0,
               "scoped panic retained an axial arrival");
    }
}

std::vector<float> performance(int block, bool ridges, bool axial,
                               bool slide = true, float amount = 0.1f, int string = 0)
{
    constexpr int rate = 48000, count = 28800;
    constexpr std::array<int, 6> notes { 45, 50, 55, 60, 64, 69 };
    auto engine = std::make_unique<Engine>();
    engine->setPerformanceRealism(options(ridges, axial));
    acustra::EngineParameters p;
    p.releaseNoise = amount;
    engine->setParameters(p);
    engine->prepare(rate, block);
    engine->setStringPerChannelMode(true);
    engine->noteOn(notes[static_cast<std::size_t>(string)], 0.7f, string + 1);
    const std::array<int, 4> events { 4800, 14400, 19200, count };
    std::vector<float> result;
    std::array<float, 512> left {}, right {};
    int now = 0;
    for (int event : events)
    {
        while (now < event)
        {
            const int n = std::min({ block, 512, event - now });
            engine->process(left.data(), right.data(), n);
            for (int sample = 0; sample < n; ++sample)
            { result.push_back(left[sample]); result.push_back(right[sample]); }
            now += n;
        }
        if (event == 4800 && slide) engine->setPitchBend(3.0f, string + 1);
        if (event == 14400 && slide) engine->setPitchBend(-2.0f, string + 1);
        if (event == 19200) engine->noteOff(notes[static_cast<std::size_t>(string)], string + 1);
    }
    return result;
}

void testPublicAudioAndPartitions()
{
    const auto old = performance(64, false, false);
    for (const auto flags : { std::array<bool, 2>{ true, false },
                              std::array<bool, 2>{ false, true },
                              std::array<bool, 2>{ true, true } })
    {
        const auto reference = performance(64, flags[0], flags[1]);
        for (int block : { 1, 17, 127, 512 })
            expect(reference == performance(block, flags[0], flags[1]),
                   "contact colour depends on host block partition");
        expect(reference != old, "independent contact candidate did not reach Main audio");
        double delta = 0.0, energy = 0.0;
        for (std::size_t i = 9600; i < 38400; ++i)
        {
            const double error = double(reference[i]) - old[i];
            delta += error * error;
            energy += double(old[i]) * old[i];
            expect(std::isfinite(reference[i]), "contact candidate made nonfinite Main audio");
        }
        std::cout << "ReleaseNoise=.1 full-audio difference, ridges=" << flags[0]
                  << " axial=" << flags[1] << ": " << 10.0 * std::log10(delta / energy)
                  << " dB relative to phrase (audibility requires listening)\n";
    }
    expect(performance(64, false, false, false) == performance(64, true, true, false),
           "stationary notes changed with contact flags");
    expect(performance(64, false, false, true, 0.0f) == performance(64, true, true, true, 0.0f),
           "zero ReleaseNoise changed with contact flags");
    expect(performance(64, false, false, true, 0.1f, 4)
               == performance(64, true, true, true, 0.1f, 4),
           "plain-string slide changed with wound-contact flags");
}
}

int main()
{
    testSeparateSpectralWitnesses();
    testAliasingAndRate();
    testLifecycleAndPosition();
    testAxialForcePropagation();
    testPublicAudioAndPartitions();
    return failures == 0 ? 0 : 1;
}
