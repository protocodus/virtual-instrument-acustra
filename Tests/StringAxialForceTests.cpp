// The string's own lengthwise force (AcustraEngine::renderStringAxialForce),
// checked against independent arithmetic: its slow limit is the
// Kirchhoff-Carrier tension of the shape on the line, the longitudinal waves
// keep that slow tension, and their ring sounds at the longitudinal modes of
// the string table with the loss its loss factor gives.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
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
    static void design(AcustraEngine& engine, int string)
    { engine.designStringAxialForce(voice(engine, string), string); }
    static float render(AcustraEngine& engine, int string)
    { return engine.renderStringAxialForce(voice(engine, string), string); }
    static float excursion(AcustraEngine& engine, int string)
    { return engine.excursionMetres(voice(engine, string)); }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
constexpr double rate = 48000.0;
constexpr double pi = 3.141592653589793;
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// The engine's string table: D'Addario EJ16 tensions, the top E's 0.305 mm
// plain steel at 200 GPa, a 648 mm scale.
constexpr double topTension = 104.088;
constexpr double topDiameter = 0.305e-3;
constexpr double steelModulus = 2.0e11;
constexpr double scaleLength = 0.648;
constexpr double topOpenFrequency = 329.6275569128699;

double topRigidity() { return steelModulus * 0.25 * pi * topDiameter * topDiameter; }
double topLinearMass()
{
    const double speed = 2.0 * scaleLength * topOpenFrequency;
    return topTension / (speed * speed);
}

auto played(bool waves, bool axial = true, int fret = 12, float velocity = 0.9f)
{
    auto engine = std::make_unique<Engine>();
    acustra::PerformanceRealism realism {};
    realism.stringAxialForce = axial;
    realism.axialWaves = waves;
    engine->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.picking = acustra::PickingTechnique::Pick;
    engine->setParameters(parameters);
    engine->prepare(rate, 64);
    engine->setStringPerChannelMode(true);
    engine->noteOn(64 + fret, velocity, 6);
    return engine;
}

void step(Engine& engine)
{
    float left = 0.0f, right = 0.0f;
    engine.process(&left, &right, 1);
}

// beta Q / L over both planes, Q the integral of y_x^2 along the string,
// from the line as the engine's own displacement observer reads it, at a
// hundred points per line sample.
double kirchhoffCarrier(Engine& engine)
{
    auto& voice = Access::voice(engine, 5);
    const double length = voice.speakingLengthMetres;
    const double d = Access::excursion(engine, 5);
    const double beta = 0.5 * (topRigidity() - static_cast<double>(voice.tensionNewtons));
    double integral = 0.0;
    for (const auto& loop : voice.loops)
    {
        const int points = static_cast<int>(100.0f * 0.5f * loop.currentDelay);
        double previous = loop.displacementAt(0.0f);
        for (int i = 1; i <= points; ++i)
        {
            const double y = loop.displacementAt(static_cast<float>(i) / points);
            const double dx = length / points;
            const double slope = d * (y - previous) / dx;
            integral += slope * slope * dx;
            previous = y;
        }
    }
    return beta * integral / length;
}

// Without longitudinal waves the stretch is the string's whole elongation at
// once: each evaluation equals beta Q / L of the shape it read. The cells
// average each wave's slope over `stride` line samples, so the pick's
// sharpest corner loses its highest partials' share; a few percent.
void slowLimitIsKirchhoffCarrier()
{
    auto engine = played(false);
    auto& axial = Access::voice(*engine, 5).stringAxialForce;
    double engineSum = 0.0, independentSum = 0.0;
    double engineSquares = 0.0, independentSquares = 0.0, cross = 0.0;
    int count = 0;
    for (int sample = 0; sample < static_cast<int>(0.3 * rate); ++sample)
    {
        // Evaluated on this sample: the line is still the one it read until
        // the junction writes, which the observer below reads one sample on.
        const bool evaluates = axial.countdown <= 0;
        step(*engine);
        if (!evaluates || sample < static_cast<int>(0.05 * rate))
            continue;
        const double ours = axial.lastStretch;
        const double theirs = kirchhoffCarrier(*engine);
        engineSum += ours;
        independentSum += theirs;
        engineSquares += ours * ours;
        independentSquares += theirs * theirs;
        cross += ours * theirs;
        ++count;
    }
    const double ourMean = engineSum / count, theirMean = independentSum / count;
    const double ourVariance = engineSquares / count - ourMean * ourMean;
    const double theirVariance = independentSquares / count - theirMean * theirMean;
    const double correlation = (cross / count - ourMean * theirMean)
        / std::sqrt(ourVariance * theirVariance);
    std::cout << "slow limit: mean " << ourMean << " N against " << theirMean
              << " N, swing " << std::sqrt(ourVariance) << " against "
              << std::sqrt(theirVariance) << ", correlation " << correlation << '\n';
    expect(theirMean > 0.01, "a loud pick stretches the top string measurably");
    expect(std::abs(ourMean / theirMean - 1.0) < 0.05,
           "the mean stretch is the Kirchhoff-Carrier tension");
    expect(std::abs(std::sqrt(ourVariance / theirVariance) - 1.0) < 0.15,
           "its swing is the shape's own");
    expect(correlation > 0.95, "and it follows the shape");
}

// A source uniform along the string reaches the saddle through the waves as
// exactly beta times it at every frequency, as the uniform tension does, and
// the rest of y_x^2 drives the longitudinal modes: a source shaped like mode
// j at angular frequency w gives -beta q r^2 / (1 - r^2), r = w / w_j. On the
// 12th-fret top E (f0 659 Hz, first mode 7.81 kHz) y_x^2 at 2 f0 and 4 f0 is
// mostly uniform (each partial's own square), so the two forces agree there,
// while the sum tones beside the first mode, 11 f0 and 12 f0, ring up.
void wavesKeepTheUniformTension()
{
    auto uniform = played(false);
    auto waves = played(true);
    constexpr int samples = static_cast<int>(0.5 * rate);
    std::vector<double> a(samples), b(samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        step(*uniform);
        step(*waves);
        a[static_cast<std::size_t>(sample)] = Access::voice(*uniform, 5).stringAxialForce.force;
        b[static_cast<std::size_t>(sample)] = Access::voice(*waves, 5).stringAxialForce.force;
    }
    const double f0 = 2.0 * topOpenFrequency;
    // The strongest line within 60 Hz of a multiple of f0, 0.05-0.45 s,
    // through a Hann window.
    const auto line = [&] (const std::vector<double>& x, double near, double& at)
    {
        const int first = static_cast<int>(0.05 * rate);
        const int count = static_cast<int>(0.4 * rate);
        double best = 0.0;
        std::complex<double> result = 0.0;
        for (double frequency = near - 60.0; frequency <= near + 60.0; frequency += 0.5)
        {
            std::complex<double> sum = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double window = 0.5 - 0.5 * std::cos(2.0 * pi * (i + 0.5) / count);
                sum += window * x[static_cast<std::size_t>(first + i)]
                    * std::polar(1.0, -2.0 * pi * frequency * i / rate);
            }
            if (std::abs(sum) > best)
            {
                best = std::abs(sum);
                result = sum;
                at = frequency;
            }
        }
        return result;
    };
    for (const int multiple : { 2, 4, 11, 12 })
    {
        double at = 0.0;
        const auto ours = line(b, multiple * f0, at);
        const auto theirs = line(a, at, at);
        const double gainDb = 20.0 * std::log10(std::abs(ours) / std::abs(theirs));
        const double phase = std::abs(std::arg(ours / theirs)) * 180.0 / pi;
        std::cout << "waves against uniform at " << multiple << " f0 (" << at << " Hz): "
                  << gainDb << " dB, " << phase << " degrees\n";
        if (multiple <= 4)
            expect(std::abs(gainDb) < 1.0 && phase < 15.0,
                   "where y_x^2 is uniform the waves give the uniform tension");
        else
            expect(gainDb > 10.0, "beside the first longitudinal mode the waves ring up");
    }
}

// The ring alone, struck once: its first mode sits at sqrt(EA / mu) / 2L for
// the string table's mass and rigidity, and decays at pi eta f1 for the loss
// factor of the string's Young's modulus.
void ringIsTheLongitudinalString()
{
    for (const int fret : { 0, 5, 12 })
    {
        auto engine = played(true, true, fret);
        for (int sample = 0; sample < 64; ++sample)
            step(*engine);
        auto& voice = Access::voice(*engine, 5);
        voice.played = false;
        auto& axial = voice.stringAxialForce;
        axial = {};
        Access::design(*engine, 5);
        axial.ringing = true;
        axial.increment = 1.0f;
        axial.countdown = 1;
        constexpr int samples = 1 << 14;
        std::vector<std::complex<double>> response(samples);
        for (int sample = 0; sample < samples && axial.ringing; ++sample)
        {
            Access::render(*engine, 5);
            response[static_cast<std::size_t>(sample)] = axial.force;
        }
        const double length = voice.speakingLengthMetres;
        const double expected = std::sqrt(topRigidity() / topLinearMass()) / (2.0 * length);
        // The spectral peak nearest the expected mode, refined on a fine grid.
        const auto magnitudeAt = [&] (double frequency)
        {
            std::complex<double> sum = 0.0;
            for (int sample = 0; sample < samples; ++sample)
                sum += response[static_cast<std::size_t>(sample)]
                    * std::polar(1.0, -2.0 * pi * frequency * sample / rate);
            return std::abs(sum);
        };
        double best = expected, bestMagnitude = 0.0;
        for (double frequency = 0.9 * expected; frequency < 1.1 * expected;
             frequency += 0.0005 * expected)
        {
            const double magnitude = magnitudeAt(frequency);
            if (magnitude > bestMagnitude)
            {
                bestMagnitude = magnitude;
                best = frequency;
            }
        }
        // Decay: the first mode's level through eight-trip Hann windows ten
        // and sixty trips in, so the ring's slower DC mode cannot leak in.
        // Its loss per trip is pi eta.
        const double eta = voice.axialLossFactor;
        const double trip = rate / expected;
        const auto modeLevel = [&] (double startTrips)
        {
            const int first = static_cast<int>(startTrips * trip);
            const int count = static_cast<int>(8.0 * trip);
            std::complex<double> sum = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const int sample = first + i;
                const double window = 0.5 - 0.5 * std::cos(2.0 * pi * (i + 0.5) / count);
                sum += window * response[static_cast<std::size_t>(sample)]
                    * std::polar(1.0, -2.0 * pi * best * sample / rate);
            }
            return std::abs(sum);
        };
        const double perTrip = std::log(modeLevel(10.0) / modeLevel(60.0)) / 50.0;
        std::cout << "ring at fret " << fret << ": first mode " << best << " Hz against "
                  << expected << ", loss per trip " << perTrip << " against pi eta "
                  << pi * eta << '\n';
        expect(std::abs(best / expected - 1.0) < 0.005,
               "the ring's first mode is the string's longitudinal fundamental");
        expect(std::abs(perTrip / (pi * eta) - 1.0) < 0.1,
               "its first mode loses pi eta per round trip");
    }
}

// Off, nothing changes; on, a released string's ring runs out and clears,
// so the instrument can still fall idle.
void ringsOutAndSwitchesOff()
{
    {
        auto engine = played(true, false);
        for (int sample = 0; sample < 4800; ++sample)
            step(*engine);
        const auto& axial = Access::voice(*engine, 5).stringAxialForce;
        expect(!axial.ringing && axial.force == 0.0f, "switched off, no force runs");
    }
    auto engine = played(true);
    auto& voice = Access::voice(*engine, 5);
    for (int sample = 0; sample < 4800; ++sample)
        step(*engine);
    expect(voice.stringAxialForce.ringing, "a played string's force runs");
    engine->noteOff(76, 6);
    std::array<float, 256> left {}, right {};
    for (int block = 0; block < static_cast<int>(6.0 * rate / 256) && voice.played; ++block)
        engine->process(left.data(), right.data(), 256);
    expect(!voice.played, "the released string is handed back");
    for (int block = 0; block < static_cast<int>(1.0 * rate / 256); ++block)
        engine->process(left.data(), right.data(), 256);
    expect(!voice.stringAxialForce.ringing, "its ring runs out and clears");
}
} // namespace

int main()
{
    slowLimitIsKirchhoffCarrier();
    wavesKeepTheUniformTension();
    ringIsTheLongitudinalString();
    ringsOutAndSwitchesOff();
    if (failures == 0)
        std::cout << "String axial force tests passed\n";
    return failures == 0 ? 0 : 1;
}
