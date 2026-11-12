// The string's own lengthwise force (AcustraEngine::renderStringAxialForce),
// checked against independent arithmetic: its slow limit is the
// Kirchhoff-Carrier tension of the shape on the line, the longitudinal waves
// keep that slow tension, and their ring sounds at the longitudinal modes of
// the string table with the loss its loss factor gives.
#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
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
    static void evaluate(AcustraEngine& engine, int string)
    { engine.evaluateStringAxialStretch(voice(engine, string), string); }
    static void configure(AcustraEngine& engine, int string, int fret, int channel = 1)
    {
        auto& v = voice(engine, string);
        v.midiChannel = channel;
        v.midiNote = v.openMidi + fret;
        engine.configureVoice(v, string, v.midiNote, false);
        design(engine, string);
    }
    static auto bend(const AcustraEngine& engine, int string)
    { return engine.voiceBend(engine.voices_[static_cast<std::size_t>(string)]); }
    static float excursion(AcustraEngine& engine, int string)
    { return engine.excursionMetres(voice(engine, string)); }
    // Supply a known force in Newtons without transverse motion, isolating
    // the physical-to-shared-unit conversion and both radiation coordinates.
    static void steadyForce(AcustraEngine& engine, int string, float newtons)
    {
        auto& v = voice(engine, string);
        design(engine, string);
        v.played = false;
        auto& a = v.stringAxialForce;
        a.force = newtons;
        a.ringing = true;
        a.countdown = 0;
        a.lossPole = 1.0f;
        a.banded = false;
    }
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
    // The plectrum near the bridge, 77 mm from the saddle. At the default
    // Pluck Position the Pick's hand (141 mm since 1.9.42, 60 mm before)
    // meets the 12th-fret string at a third of its length, where the third
    // partial is not excited and y_x^2 at 4 f0 is no longer mostly each
    // partial's own square (wavesKeepTheUniformTension read -2.0 dB there).
    parameters.pluckPosition = 0.0f;
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

void uniformStretchIntegratesFractionalSlopeCrossings()
{
    auto engine = played(false);
    auto& voice = Access::voice(*engine, 5);
    constexpr std::array<float, 5> wave { 0.0f, 2.0f, -1.0f, 1.0f, 4.0f };
    // Integrals of the squared outgoing plus returning slopes over the
    // folded period. At D=3.25 the four intervals contribute 18.25,
    // 27.25, 12.25 and 6.25, including the fractional final interval.
    constexpr std::array<double, 5> expected { 68.0, 64.0, 60.0, 56.0, 52.0 };
    for (int write : { 0, 1, 127 })
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            for (auto& loop : voice.loops)
                loop.delay.fill(0.0f);
            auto& loop = voice.loops[0];
            loop.writeIndex = write;
            const float period = 3.0f + static_cast<float>(i) * 0.25f;
            loop.setCurrentDelay(period);
            const auto slot = [&] (std::size_t age)
            {
                return (static_cast<std::size_t>(write - 1) - age)
                    & (loop.delay.size() - 1);
            };
            for (std::size_t age = 0; age < wave.size(); ++age)
                loop.delay[slot(age)] = wave[age];
            loop.delay[slot(wave.size())] = std::numeric_limits<float>::quiet_NaN();
            Access::evaluate(*engine, 5);
            const auto& axial = voice.stringAxialForce;
            const double measured = axial.lastStretch
                / (static_cast<double>(axial.stretchScale) * period);
            expect(std::abs(measured - expected[i]) < 1.0e-10,
                   "the uniform stretch missed a fractional slope crossing");
        }
}

// Without longitudinal waves the stretch is the string's whole elongation at
// once. Compare its exact spatial integral to a dense numerical integration
// after each sample writes a new wave to the line.
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

// A prescribed spatially uniform squared-slope source q(t) gives the
// closed-form retarded integral of q(t - x/c) over the whole round trip.
// Use unit peak beta*q (one Newton). Drive the production longitudinal
// recurrence with the sampled difference of that analytic integral, while
// the analytical uniform target is simply beta*q itself.
void uniformRetardedSourceKeepsUniformTension()
{
    auto engine = played(true);
    auto& voice = Access::voice(*engine, 5);
    Access::design(*engine, 5);
    const double physicalRoundTrip = 2.0 * voice.speakingLengthMetres
        / std::sqrt(topRigidity() / topLinearMass());
    const double f0 = 2.0 * topOpenFrequency;
    voice.played = false;
    for (const int multiple : { 2, 4 })
    {
        auto& axial = voice.stringAxialForce;
        axial.clear();
        Access::design(*engine, 5);
        axial.ringing = true;
        const double omega = 2.0 * pi * multiple * f0;
        const auto frame = std::polar(1.0, omega / rate);
        const auto previous = std::conj(frame);
        const auto delayed = std::polar(1.0, -omega * physicalRoundTrip);
        const auto integral = (1.0 - delayed) / std::complex<double>(0.0, omega);
        const auto increment = rate * (1.0 - previous) * integral;
        std::complex<double> phase(1.0), observed(0.0), uniform(0.0);
        constexpr int first = static_cast<int>(0.05 * rate);
        constexpr int count = static_cast<int>(0.30 * rate);
        for (int sample = 0; sample < first + count; ++sample)
        {
            axial.increment = static_cast<float>((increment * phase).real());
            axial.countdown = 1;
            Access::render(*engine, 5);
            if (sample >= first)
            {
                const double w = 0.5 - 0.5 * std::cos(
                    2.0 * pi * (sample - first + 0.5) / count);
                const auto demod = w * std::conj(phase);
                observed += static_cast<double>(axial.force) * demod;
                uniform += phase.real() * demod;
            }
            phase *= frame;
        }
        const auto ratio = observed / uniform;
        const double gainDb = 20.0 * std::log10(std::abs(ratio));
        const double phaseDegrees = std::abs(std::arg(ratio)) * 180.0 / pi;
        std::cout << "prescribed uniform source at " << multiple << " f0: "
                  << gainDb << " dB, " << phaseDegrees << " degrees\n";
        expect(std::abs(gainDb) < 1.0 && phaseDegrees < 15.0,
               "a spatially uniform retarded source retains its uniform tension");
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

void newtonsHaveOneSharedUnit()
{
    constexpr double projection = 0.20791169081775931; // sin(12 degrees)
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
        for (const auto technique : { acustra::PickingTechnique::Finger,
              acustra::PickingTechnique::Thumb, acustra::PickingTechnique::Pick })
            for (const float displacement : { 0.0f, 0.00005f, 0.00773577847f, 0.04f })
            {
                auto engine = std::make_unique<Engine>();
                auto calibration = acustra::fittedPhysicalCalibration;
                calibration.steelDisplacementScaleMetres = displacement;
                engine->setPhysicalCalibration(calibration);
                acustra::PerformanceRealism realism;
                realism.axialWaves = false;
                engine->setPerformanceRealism(realism);
                acustra::EngineParameters parameters;
                parameters.picking = technique;
                engine->setParameters(parameters);
                engine->prepare(fs, 64);
                // Two known axial sources at different saddle arms. Their
                // Newton forces must add before the common pressure/piezo
                // observation, regardless of the inferred pluck excursion.
                Access::steadyForce(*engine, 0, 1.0f);
                Access::steadyForce(*engine, 5, 0.25f);
                step(*engine);
                const auto probe = engine->getLastBodyForceProbe();
                const double unit = engine->getPiezoNewtonsPerUnit();
                expect(std::abs(probe.axialNormal * unit + 1.25 * projection) < 1.0e-7,
                       "axial Newton force changed with technique, calibration or rate");
                expect(std::abs(probe.axialMoment * unit - 0.9375 * projection) < 1.0e-7,
                       "axial saddle moment lost its force unit or lever arm");
                expect(probe.transverseNormal == 0.0f && probe.transverseMoment == 0.0f
                           && engine->getLastBridgePower() == 0.0f,
                       "one-way axial source unexpectedly entered transverse power");
                expect(probe.totalNormal == probe.axialNormal
                           && probe.totalMoment == probe.axialMoment,
                       "radiation did not receive the axial force exactly once");
                engine->reset();
                expect(engine->getLastBodyForceProbe().totalNormal == 0.0f,
                       "reset retained a stale radiation force observer");
            }
}
// A pure sinusoidal wave has only DC and twice its frequency in the
// squared-slope source. The old spatial-stride sample clock folded that
// second harmonic into a spurious low-frequency force.
void sourceDoesNotFoldAtSpatialStride()
{
    for (const double hostRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto engine = std::make_unique<Engine>();
        acustra::PerformanceRealism realism;
        realism.axialWaves = false;
        engine->setPerformanceRealism(realism);
        engine->prepare(hostRate, 64);
        engine->setStringPerChannelMode(true);
        engine->noteOn(64, 0.9f, 6);
        auto& voice = Access::voice(*engine, 5);
        Access::design(*engine, 5);
        auto& axial = voice.stringAxialForce;
        const int stride = axial.stride;
        const double sourceRate = hostRate / stride;
        constexpr double waveHz = 6000.0;
        constexpr double squareHz = 2.0 * waveHz;
        const double aliasHz = std::abs(squareHz - sourceRate * std::round(squareHz / sourceRate));
        const double period = hostRate / 400.0;
        for (auto& loop : voice.loops)
        {
            loop.currentDelay = static_cast<float>(period);
            loop.writeIndex = 0;
            loop.delay.fill(0.0f);
        }
        const int count = static_cast<int>(hostRate / 10.0);
        std::complex<double> wanted {}, folded {};
        for (int sample = 0; sample < 2 * count; ++sample)
        {
            auto& loop = voice.loops[0];
            for (int age = 0; age < static_cast<int>(std::ceil(period)) + 4; ++age)
            {
                const unsigned at = (static_cast<unsigned>(loop.writeIndex - 1)
                    - static_cast<unsigned>(age)) & (static_cast<unsigned>(loop.delay.size()) - 1);
                loop.delay[at] = static_cast<float>(0.2 * std::cos(
                    2.0 * pi * (waveHz * sample / hostRate + 15.0 * age / period)));
            }
            Access::render(*engine, 5);
            if (sample < count) continue;
            const double source = axial.increment;
            wanted += source * std::polar(1.0, -2.0 * pi * squareHz * sample / hostRate);
            folded += source * std::polar(1.0, -2.0 * pi * aliasHz * sample / hostRate);
        }
        const double ratio = std::abs(folded) / std::max(std::abs(wanted), 1.0e-30);
        std::cout << "source " << hostRate << " Hz spatial stride " << stride
                  << ": folded " << aliasHz << " Hz / 12 kHz "
                  << 20.0 * std::log10(std::max(ratio, 1.0e-30)) << " dB\n";
        expect(stride > 1 && std::abs(wanted) > 1.0e-8,
               "the nonzero quadratic source crosses the old decimation boundary");
        expect(ratio < std::pow(10.0, -75.0 / 20.0),
               "the nonlinear source has no reduced-rate fold at " + std::to_string(hostRate));
    }
}
} // namespace


// A stable loss-filter pole alone does not make a feedback waveguide passive.
// For b/(1-p z^-1), the EXACT whole-unit-circle maximum is b/(1-|p|).
// Evaluate the stored float coefficients in double: float addition can round
// a small excess to unity and hide the growing DC mode that failed at11025Hz.
void longitudinalLossIsPassiveAcrossControls()
{
    constexpr double rates[] {8000,11025,12000,16000,22050,24000,32000,44100,
        48000,88200,96000,176400,192000,384000,768000};
    int cases = 0, active = 0, upper = 0, bad = 0, magnitudeBad = 0, phaseBad = 0;
    double maximum = 0.0, largestPhase = 0.0, largestMagnitudeError = 0.0,
        largestMagnitudeTolerance = 0.0, largestUpperPhase = 0.0;
    const auto check = [&] (Engine& engine, double fs, int string) {
        const auto& v = Access::voice(engine, string);
        const auto& a = v.stringAxialForce;
        const double b = a.lossGain, p = a.lossPole, t = a.thiran;
        const double supremum = b / (1.0 - std::abs(p));
        ++cases;
        maximum = std::max(maximum, supremum);
        if (!(std::isfinite(supremum) && b >= 0 && std::abs(p) < 1
              && std::abs(t) < 1 && supremum < 1))
        {
            ++bad;
            if (bad <= 3) std::cerr << "nonpassive axial loss at " << fs << " Hz string "
                << string << " length " << v.speakingLengthMetres << ": " << supremum << '\n';
        }
        const double omega = 2.0*pi/a.roundTrip;
        if (omega >= pi) return; // No resolvable first longitudinal mode.
        const auto q = std::polar(1.0, -omega);
        const auto loss = b/(1.0-p*q);
        const double target = std::exp(-pi*double(v.axialLossFactor));
        // The design uses double coefficients/frequency, then stores a, b
        // and roundTrip as floats. With round-to-nearest unit roundoff u,
        // |delta a| <= u*|a_f|/(1-u), |delta omega| <= u*omega_f/(1-u).
        // |delta denominator| <= |delta a| + |a|*|delta omega|, while b's
        // relative error is <=u. Thus relative |H| error is bounded by
        // u+(1+u)*|delta denominator|/|1-a_f*exp(-j*omega_f)|. This matters
        // at actual768k with long, lowered strings: cancellation near a=1
        // permits ~2.2e-6 relative error without changing the physical loss.
        const double u = 0.5*std::numeric_limits<float>::epsilon();
        const double poleError = u*std::abs(p)/(1.0-u);
        const double phaseError = u*omega/(1.0-u);
        const double denominatorError = poleError + (std::abs(p)+poleError)*phaseError;
        const double representationBound = u + (1.0+u)*denominatorError/std::abs(1.0-p*q);
        const double magnitudeTolerance = std::max(2.0e-6,representationBound);
        const double magnitudeError = std::abs(std::abs(loss)/target-1.0);
        largestMagnitudeError = std::max(largestMagnitudeError,magnitudeError);
        largestMagnitudeTolerance = std::max(largestMagnitudeTolerance,magnitudeTolerance);
        if (!(magnitudeError < magnitudeTolerance)) {
            ++magnitudeBad;
            if(magnitudeBad<=3)std::cerr<<"magnitude at "<<fs<<" actual "<<engine.sampleRate()
              <<" string "<<string<<" length "<<v.speakingLengthMetres<<" eta "<<v.axialLossFactor
              <<" pole "<<p<<" gain "<<b<<" omega "<<omega
              <<" relative error "<<(std::abs(loss)/target-1.0)<<'\n';
        }
        // The low-frequency approximation retains its existing bounded
        // domain. Above one quarter of the sample rate the second mode
        // cannot be represented, so require the scalar-loss ring to close
        // on the first mode to within the stored floats' precision.
        const bool upperOnly = omega > 0.5 * pi;
        const bool interior = omega < .48*pi && a.thiran > -3.f/11.f+1e-6f
            && a.thiran < .6f-1e-6f && a.roundTrip < a.ringSize-3;
        if (upperOnly || interior)
        {
            // Integer delay + allpass + accepted loss must still close at
            // the designed fundamental. An old loss phase retained after a
            // scalar fallback would shift this resonance.
            const auto loop = std::pow(q, a.ringDelay)*(t+q)/(1.0+t*q)*loss;
            const double phase = std::abs(std::arg(loop));
            if (upperOnly)
            {
                largestUpperPhase = std::max(largestUpperPhase, phase);
                ++upper;
            }
            else
                largestPhase = std::max(largestPhase, phase);
            if (!(phase < (upperOnly ? 1e-5 : 0.035))) {++phaseBad;
                if(phaseBad<=3)std::cerr<<"phase at "<<fs<<" string "<<string<<" trip "<<a.roundTrip
                 <<" loss "<<a.lossPole<<" delay "<<a.ringDelay<<" Thiran "<<a.thiran<<": "<<phase<<'\n';} // <0.56% of one round trip.
            ++active;
        }
    };
    for (double fs : rates)
    {
        auto engine = std::make_unique<Engine>();
        acustra::EngineParameters p;
        p.picking = acustra::PickingTechnique::Pick;
        engine->setParameters(p);
        engine->prepare(fs,64);
        expect(engine->sampleRate()==std::min(fs,Engine::maximumSupportedSampleRate),
               "passivity sweep records the actual prepared rate and native clamp");
        // Every playable fret and tuning, all six physical string tables.
        for (int tuning=0; tuning<4; ++tuning)
        {
            p.tuning=static_cast<acustra::Tuning>(tuning);
            engine->setParameters(p);
            for(int fret=0; fret<=Engine::fretCount; ++fret)
                for(int string=0; string<Engine::stringCount; ++string)
                { Access::configure(*engine,string,fret); check(*engine,fs,string); }
        }
        // Construction and loss-age endpoints at low and nominal rates.
        if(fs==11025 || fs==48000)
            for(int shape=0;shape<4;++shape) for(int wood=0;wood<3;++wood)
                for(float age:{0.f,.15f,1.f})
                {
                    p.shape=static_cast<acustra::BodyShape>(shape);
                    p.bodyMaterial=static_cast<acustra::BodyMaterial>(wood);
                    p.stringAge=age;engine->setParameters(p);
                    for(int fret:{0,12,Engine::fretCount}) for(int string=0;string<6;++string)
                    { Access::configure(*engine,string,fret); check(*engine,fs,string); }
                }
        // A manager wheel moves the speaking point; an MPE member changes
        // tension at that point. Include the legal large-wheel saturation.
        p=acustra::EngineParameters{};engine->setParameters(p);
        for(bool member:{false,true})
        {
            engine->setLowerZoneMemberCount(member?1:0);
            const int channel=member?2:1;
            for(float cents:{-100.f,0.f,100.f}) for(float bend:{-96.f,-2.f,0.f,2.f,96.f})
            {
                engine->setMasterTuneCents(cents);engine->setPitchBend(bend,channel);
                for(int fret:{0,Engine::fretCount}) for(int string=0;string<6;++string)
                {
                    auto& voice=Access::voice(*engine,string);
                    voice.played=true;
                    voice.keyDown=true;
                    voice.mpeMember=member;
                    voice.memberPitchBendFrozen=false;
                    Access::configure(*engine,string,fret,channel);
                    const auto actual=Access::bend(*engine,string);
                    expect(actual.performed==bend && actual.member==(member?bend:0.f),
                           "passivity fixture reaches the intended live manager/member bend");
                    check(*engine,fs,string);
                }
            }
            engine->setPitchBend(0,channel);
        }
    }
    std::cout << "axial passive-loss sweep " << cases << " designs, sup |H| " << maximum
              << ", " << active << " phase checks, maximum error " << largestPhase << " rad"
              << ", " << upper << " first-mode-only cases with maximum phase error "
              << largestUpperPhase << " rad"
              << ", magnitude error " << largestMagnitudeError << " within float bound "
              << largestMagnitudeTolerance << ", prepared maximum " << Engine::maximumSupportedSampleRate << " Hz\n";
    expect(bad==0,"stored axial loss is strictly passive over the whole frequency band");
    expect(magnitudeBad==0,"accepted loss retains the physical first-mode attenuation");
    expect(phaseBad==0,"accepted loss and Thiran retain longitudinal round-trip phase");
    expect(upper>0,"phase sweep exercises first modes above one quarter of the sample rate");
}

void lowRateAxialRingDecaysWithoutInput()
{
    auto engine=std::make_unique<Engine>();
    engine->setParameters(acustra::engineParameters(acustra::factoryPresets[6]));
    engine->prepare(11025,64);
    Access::configure(*engine,3,0);
    auto& v=Access::voice(*engine,3);v.played=false;
    auto& a=v.stringAxialForce;
    // G-string longitudinal mode at ~2551Hz: the unconstrained two-mode fit
    // has DC gain1.004616, although its own pole lies inside the unit circle.
    expect(a.lossPole==0.f,"known nonpassive 11025Hz fit selects scalar attenuation");
    expect(std::abs(double(a.lossGain)-std::exp(-pi*v.axialLossFactor))<6e-8,
           "scalar fallback retains the first-mode loss rather than inventing damping");
    a.ringing=true;a.increment=1.f;a.countdown=1;
    bool finite=true;double early=0,late=0;
    for(int n=0;n<110250;++n)
    {
        Access::render(*engine,3);
        finite=finite&&std::isfinite(a.force)&&std::isfinite(a.lossY1)&&std::isfinite(a.thiranY1);
        if(n<11025)early+=double(a.force)*a.force;
        if(n>=99225)late+=double(a.force)*a.force;
    }
    expect(finite,"zero-input longitudinal ring must not overflow after the note has ended");
    expect(early>.1&&late<early*1e-12&&!a.ringing,
           "low-rate axial impulse decays and clears within ten seconds");
    // Zero loss is an intentional physical edge, not a reason to invent a
    // damping margin. Its stored filter must be exactly lossless, not active.
    v.axialLossFactor=0.f;
    a.designed=false;
    Access::design(*engine,3);
    expect(a.lossPole==0.f && a.lossGain==1.f,
           "intentional zero axial loss remains exactly lossless");
    // Frozen previously-passive reference from the same physical fixture.
    Access::configure(*engine,0,0);
    const auto& unchanged=Access::voice(*engine,0).stringAxialForce;
    expect(unchanged.lossGain==0.8059226274490356f
           &&unchanged.lossPole==0.14545254409313202f,
           "already-passive low-rate coefficients remain bit-identical");
}

void invalidStretchDoesNotEnterAxialFeedback()
{
    auto engine = played(true);
    auto& voice = Access::voice(*engine, 5);
    auto& axial = voice.stringAxialForce;
    Access::design(*engine, 5);
    // Start with a healthy ringing impulse so the observer's fault handling
    // must retain the existing tail as well as reject the bad source frame.
    const auto impulseSlot = static_cast<std::size_t>(axial.writeIndex);
    axial.countdown = 1;
    axial.increment = 0.125f;
    Access::render(*engine, 5);
    auto& loop = voice.loops[0];
    const auto newest = static_cast<std::size_t>(loop.writeIndex - 1)
        & (loop.delay.size() - 1);
    const float validSample = loop.delay[newest];
    loop.delay[newest] = std::numeric_limits<float>::quiet_NaN();
    const float afterFault = Access::render(*engine, 5);
    expect(std::isfinite(afterFault) && std::isfinite(axial.force)
               && std::isfinite(axial.lastStretch) && axial.increment == 0.0f,
           "nonfinite axial stretch was written into the feedback state");
    expect(axial.rereference && axial.ring[impulseSlot] == 0.125f,
           "invalid stretch did not preserve the healthy ring and request a new reference");

    loop.delay[newest] = validSample;
    const float afterRecovery = Access::render(*engine, 5);
    expect(std::isfinite(afterRecovery) && !axial.rereference && axial.increment == 0.0f,
           "the first valid stretch after an invalid frame emitted a spurious force impulse");
    bool finiteTail = true;
    for (int sample = 0; sample < 64; ++sample)
        finiteTail = std::isfinite(Access::render(*engine, 5)) && finiteTail;
    expect(finiteTail, "the recovered axial ring retained a nonfinite state");
}

int main(int argc, char** argv)
{
    longitudinalLossIsPassiveAcrossControls();
    lowRateAxialRingDecaysWithoutInput();
    invalidStretchDoesNotEnterAxialFeedback();
    if(argc>1 && std::string(argv[1])=="--passivity-only") return failures==0?0:1;
    uniformStretchIntegratesFractionalSlopeCrossings();
    slowLimitIsKirchhoffCarrier();
    wavesKeepTheUniformTension();
    uniformRetardedSourceKeepsUniformTension();
    ringIsTheLongitudinalString();
    ringsOutAndSwitchesOff();
    newtonsHaveOneSharedUnit();
    sourceDoesNotFoldAtSpatialStride();
    if (failures == 0)
        std::cout << "String axial force tests passed\n";
    return failures == 0 ? 0 : 1;
}
