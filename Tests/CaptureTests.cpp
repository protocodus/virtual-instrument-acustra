// Capture is a read-only observation of the instrument. Verify the microphone
// channel identity, the piezo chain block by block against its analytic
// targets (Docs/decisions.md, 2026-09-29), switching and silence; absolute
// piezo sensitivity is anchored in Tools/CalibratePiezo.py, not here.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    using Design = AcustraEngine::PiezoDesign;
    static float loadedPiezo(AcustraEngine& engine, float volts)
    {
        return engine.renderLoadedPiezo(volts);
    }
    static float saddle(AcustraEngine& engine, float force)
    {
        return engine.renderPiezoSaddle(force);
    }
    static float coupling(AcustraEngine& engine, float buffered)
    {
        return engine.renderPiezoCoupling(buffered);
    }
    static float chain(AcustraEngine& engine, float force)
    {
        return engine.renderPiezo(force);
    }
    static float preamp(float volts) { return AcustraEngine::piezoPreamp(volts); }
    static void setWeights(AcustraEngine& engine,
                           const std::array<float, 6>& weights)
    {
        engine.piezoStringWeights_ = weights;
    }
    static float piezoWave(const AcustraEngine& engine) { return engine.lastPiezoWave_; }
    static std::array<float, 8> captureMix(const AcustraEngine& engine)
    {
        return engine.captureMix_;
    }
    static float piezoForce(const AcustraEngine& engine) { return engine.lastPiezoForce_; }
    static float drivingForce(const AcustraEngine& engine)
    {
        return engine.lastPiezoForce_ + engine.lastLongitudinalForce_;
    }
    static std::vector<float> chainState(const AcustraEngine& engine)
    {
        std::vector<float> state {
            engine.piezoLoadInput_, engine.piezoLoadOutput_,
            engine.piezoSaddleState1_, engine.piezoSaddleState2_,
            engine.piezoBlockerInput_, engine.piezoBlockerOutput_,
            engine.piezoTrim_, engine.lastPiezoWave_, engine.lastPiezoForce_,
            engine.lastPiezoVoltage_ };
        state.insert(state.end(), engine.piezoForceDerivative_.history.begin(),
                     engine.piezoForceDerivative_.history.end());
        state.push_back(static_cast<float>(engine.piezoForceDerivative_.index));
        return state;
    }
};
} // namespace acustra

namespace
{
int failures = 0;
void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Audio
{
    std::vector<float> left, right;
};

Audio render(acustra::EngineParameters parameters, int rate = 48000,
             int block = 64)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->setParameters(parameters);
    engine->prepare(rate, block);
    for (int note : { 40, 45, 50, 55, 59, 64 })
        engine->noteOn(note, 0.7f);
    Audio audio { std::vector<float>(rate), std::vector<float>(rate) };
    for (int i = 0; i < rate; i += block)
        engine->process(audio.left.data() + i, audio.right.data() + i,
                        std::min(block, rate - i));
    return audio;
}

double energy(const Audio& audio)
{
    double sum = 0.0;
    for (float x : audio.left)
        sum += x * x;
    return sum / audio.left.size();
}

void testCaptureObservations()
{
    using acustra::CaptureType;
    for (auto material : { acustra::StringMaterial::Steel,
                           acustra::StringMaterial::Nylon })
    {
        acustra::EngineParameters parameters;
        parameters.stringMaterial = material;
        parameters.stereoWidth = 1.0f;
        const auto stereo = render(parameters);
        expect(stereo.left != stereo.right, "stereo mic lost its measured spatial response");
        parameters.capture = CaptureType::MonoMic;
        const auto mono = render(parameters);
        parameters.stereoWidth = 0.0f;
        expect(mono.left == mono.right && mono.left == render(parameters).left,
               "mono microphone must be bit-identical in both channels and independent of width");
        expect(energy(mono) > 1.0e-10, "mono microphone is silent");
        expect(mono.left != stereo.left && mono.left != stereo.right,
               "mono microphone is not its own measured observation");
        for (auto retired : { CaptureType::TrebleMic, CaptureType::BassMic,
                              CaptureType::UpperMic })
        {
            parameters.capture = retired;
            expect(render(parameters).left == mono.left,
                   "retired microphone did not remap to the supported mono microphone");
        }
        parameters.capture = CaptureType::Piezo;
        const auto piezo = render(parameters);
        parameters.bodyAmount = 0.0f;
        parameters.stereoWidth = 1.0f;
        expect(piezo.left == piezo.right && piezo.left == render(parameters).left,
               "piezo output must be mono and independent of microphone controls");
        expect(energy(piezo) > 1.0e-10 && piezo.left != mono.left,
               "piezo is silent or duplicates a microphone");
        for (auto retired : { CaptureType::SaddlePiezo, CaptureType::Magnetic })
        {
            parameters.capture = retired;
            expect(render(parameters).left == piezo.left,
                   "retired pickup did not remap to the supported loaded piezo");
        }
        parameters.bodyAmount = 0.82f;
        parameters.capture = static_cast<CaptureType>(-123);
        expect(render(parameters).left == stereo.left,
               "invalid capture did not fall back to the default stereo microphone");
    }
}

void testCaptureLifecycle()
{
    for (int rate : { 44100, 48000, 96000 })
        for (auto type : { acustra::CaptureType::StereoMic, acustra::CaptureType::MonoMic,
                           acustra::CaptureType::Piezo })
        {
            acustra::EngineParameters parameters;
            parameters.capture = type;
            const auto audio = render(parameters, rate);
            expect(audio.left == render(parameters, rate, 127).left,
                   "capture changed with host block size");
            for (float sample : audio.left)
                expect(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                       "capture is not finite and bounded");

            auto engine = std::make_unique<acustra::AcustraEngine>();
            engine->setParameters(parameters);
            engine->prepare(rate, 64);
            std::array<float, 64> left {}, right {};
            engine->noteOn(40, 0.8f);
            engine->process(left.data(), right.data(), 64);
            engine->allSoundOff();
            engine->process(left.data(), right.data(), 64);
            expect(std::all_of(left.begin(), left.end(),
                              [] (float x) { return x == 0.0f; }),
                   "all sound off left a pickup derivative tail");
        }

    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(48000, 64);
    acustra::EngineParameters parameters;
    std::array<float, 64> left {}, right {};
    engine->noteOn(40, 0.8f);
    for (int i = 0; i < 200; ++i)
    {
        parameters.capture = static_cast<acustra::CaptureType>(i % 8);
        engine->setParameters(parameters);
        engine->process(left.data(), right.data(), 64);
        expect(engine->getActiveVoiceCount() == 1,
               "switching capture reset a ringing note");
        for (float sample : left)
            expect(std::isfinite(sample) && std::abs(sample) <= 1.0f,
                   "rapid capture switching is not finite and bounded");
    }
    parameters.capture = acustra::CaptureType::StereoMic;
    engine->setParameters(parameters);
    engine->reset();
    engine->process(left.data(), right.data(), 64);
    expect(std::all_of(left.begin(), left.end(),
                      [] (float x) { return x == 0.0f; }),
           "reset left a capture tail");

    // Observe through a pickup and return to the mic: the physical state must
    // be identical to an engine that never changed capture, sample for sample.
    engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(48000, 64);
    auto reference = std::make_unique<acustra::AcustraEngine>();
    reference->prepare(48000, 64);
    engine->noteOn(45, 0.7f);
    reference->noteOn(45, 0.7f);
    std::array<float, 64> referenceLeft {}, referenceRight {};
    for (int block = 0; block < 600; ++block)
    {
        if (block < 100)
            parameters.capture = static_cast<acustra::CaptureType>(block % 8);
        else
            parameters.capture = acustra::CaptureType::StereoMic;
        engine->setParameters(parameters);
        engine->process(left.data(), right.data(), 64);
        reference->process(referenceLeft.data(), referenceRight.data(), 64);
    }
    expect(left == referenceLeft && right == referenceRight,
           "pickup observation changed the state of the vibrating instrument");
}

void testLoadedPiezoElectricalResponse()
{
    using Access = acustra::AcustraEngineTestAccess;
    constexpr double pi = 3.14159265358979323846;
    // Independent circuit reference: the measured 450 pF source capacitance
    // and 2 MOhm load give H(s)=sRC/(1+sRC). Evaluate that analog transfer at
    // the bilinear-warped frequency, rather than duplicating the recurrence.
    constexpr double tau = 450.0e-12 * 2.0e6;
    double maximumError = 0.0;
    for (int rate : { 8000, 44100, 48000, 96000, 384000 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 64);
        std::vector<float> impulse(static_cast<std::size_t>(rate / 10));
        double impulseEnergy = 0.0;
        for (std::size_t i = 0; i < impulse.size(); ++i)
        {
            impulse[i] = Access::loadedPiezo(*engine, i == 0 ? 1.0f : 0.0f);
            impulseEnergy += static_cast<double>(impulse[i]) * impulse[i];
        }
        expect(std::isfinite(impulseEnergy) && impulseEnergy <= 1.000001,
               "piezo load increased the impulse's squared signal norm");
        for (double frequency : { 0.0, 20.0, 82.406889, 1.0 / (2.0 * pi * tau),
                                   329.627556, 1000.0, 0.2 * rate, 0.45 * rate,
                                   0.5 * rate })
        {
            const auto step = std::polar(1.0, -2.0 * pi * frequency / rate);
            std::complex<double> phase { 1.0, 0.0 }, actual {};
            for (float sample : impulse)
            {
                actual += static_cast<double>(sample) * phase;
                phase *= step;
            }
            const std::complex<double> s { 0.0,
                2.0 * rate * std::tan(pi * frequency / rate) };
            const auto expected = s * tau / (1.0 + s * tau);
            const double error = std::abs(actual - expected);
            maximumError = std::max(maximumError, error);
            expect(error < 5.0e-5,
                   "piezo complex response differs from the measured RC circuit");
            expect(std::abs(actual) <= 1.000001,
                   "piezo electrical loading has gain above unity");
        }
        engine->reset();
        float dc = 0.0f;
        for (int sample = 0; sample < rate / 10; ++sample)
            dc = Access::loadedPiezo(*engine, 1.0f);
        expect(std::abs(dc) < 1.0e-20f, "piezo load passed sustained DC");
        // Dirty both histories. Each public hard reset must clear both: stale
        // input produces a negative impulse even if output alone was cleared.
        for (bool allSoundOff : { false, true })
        {
            Access::loadedPiezo(*engine, 0.37f);
            if (allSoundOff)
                engine->allSoundOff();
            else
                engine->reset();
            expect(Access::loadedPiezo(*engine, 0.0f) == 0.0f,
                   "hard reset retained piezo capacitor history");
        }
    }
    std::cout << "Loaded piezo maximum complex RC error: " << maximumError << '\n';
}

using Access = acustra::AcustraEngineTestAccess;
using Design = Access::Design;
constexpr double pi = 3.14159265358979323846;

// The complex response at frequency of an impulse response.
std::complex<double> responseAt(const std::vector<float>& impulse, double frequency,
                                double rate)
{
    const auto step = std::polar(1.0, -2.0 * pi * frequency / rate);
    std::complex<double> phase { 1.0, 0.0 }, sum {};
    for (float sample : impulse)
    {
        sum += static_cast<double>(sample) * phase;
        phase *= step;
    }
    return sum;
}

double decibels(double magnitude) { return 20.0 * std::log10(magnitude); }

// In-place radix-2 FFT; the sign picks the direction and nothing is scaled.
void fft(std::vector<std::complex<double>>& data, int sign)
{
    const std::size_t size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const auto root = std::polar(1.0, sign * 2.0 * pi / static_cast<double>(length));
        for (std::size_t start = 0; start < size; start += length)
        {
            std::complex<double> twiddle { 1.0, 0.0 };
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * twiddle;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                twiddle *= root;
            }
        }
    }
}

// The analog saddle resonance, H(s) = w0^2 / (s^2 + s w0/Q + w0^2), with
// its corner held below 0.4 fs as the engine holds it.
std::complex<double> analogSaddle(double frequency, double rate)
{
    const double f0 = std::min(Design::saddleHz, 0.4 * rate);
    const std::complex<double> s { 0.0, frequency / f0 };
    return 1.0 / (s * s + s / Design::saddleQ + 1.0);
}

// One hard strum as the piezo's pre-preamp voltage (getLastPiezoVoltage) and
// its driving force, sample by sample: an open E major downstroke, low E
// first, 9 ms apart, as Tools/CalibratePiezo.py plays it. `strokes` > 0
// instead plays that many of the player's own strums, one every 1.2 s, each
// string at its own drawn level (strumMember) - the hottest a stroke gets.
struct Strum
{
    std::vector<float> volts, force;
};

Strum hardStrum(double rate, float velocity, float touch,
                acustra::StringMaterial material = acustra::StringMaterial::Steel,
                acustra::PickingTechnique picking = acustra::PickingTechnique::Finger,
                double seconds = 2.5, int strokes = 0)
{
    acustra::EngineParameters parameters;
    parameters.touch = touch;
    parameters.stringMaterial = material;
    parameters.picking = picking;
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->setParameters(parameters);
    engine->prepare(rate, 64);
    engine->setStringPerChannelMode(true);
    constexpr std::array notes { 40, 47, 52, 56, 59, 64 };
    Strum strum;
    const auto period = std::lround(rate * 1.2);
    const auto length = strokes > 0 ? period * strokes : static_cast<long>(rate * seconds);
    for (long sample = 0; sample < length; ++sample)
    {
        if (strokes > 0 && sample % period == std::lround(rate * 0.05))
        {
            engine->beginStrum();
            for (int string = 0; string < 6; ++string)
                engine->noteOn(notes[static_cast<std::size_t>(string)], velocity,
                               string + 1, static_cast<int>(std::lround(rate * 0.009 * string)),
                               true);
        }
        for (int string = 0; strokes == 0 && string < 6; ++string)
            if (sample == std::lround(rate * (0.1 + 0.009 * string)))
                engine->noteOn(notes[static_cast<std::size_t>(string)], velocity,
                               string + 1);
        float left = 0.0f, right = 0.0f;
        engine->process(&left, &right, 1);
        strum.volts.push_back(engine->getLastPiezoVoltage());
        strum.force.push_back(Access::drivingForce(*engine));
    }
    return strum;
}

// 1. The weighted saddle force. Unit weights must reproduce the junction's
// own reaction force bit for bit through everything that moves the port:
// bends, re-plucks that leave a tail at another impedance, releases, the
// idle strings, a note-on across a release, and the uncoupled bridge. The
// weighted force is linear in the weights, so six engines each hearing one
// string give the per-string forces F_i, and the shipped weights must read
// sum w_i F_i.
void testPiezoStringWeights()
{
    double sum = 0.0;
    for (float weight : Design::stringWeights)
        sum += weight;
    expect(std::abs(sum - 6.0) < 1.0e-5, "piezo string weights do not sum to six");
    constexpr std::array patternDb { -0.8, 0.4, 0.9, -0.3, 0.6, -1.0 };
    for (std::size_t string = 0; string < 6; ++string)
    {
        const double db = decibels(Design::stringWeights[string]);
        expect(std::abs(db - patternDb[string]) < 0.01 && std::abs(db) <= 1.0,
               "piezo string weight left its chosen +-1 dB pattern");
    }

    const auto play = [] (std::vector<std::unique_ptr<acustra::AcustraEngine>>& engines,
                          const auto& onSample)
    {
        std::array<float, 1> left {}, right {};
        for (int sample = 0; sample < 48000 * 3; ++sample)
        {
            for (auto& engine : engines)
            {
                switch (sample)
                {
                case 4800: for (int n : { 40, 47, 52, 56, 59, 64 }) engine->noteOn(n, 0.9f); break;
                case 24000: engine->setPitchBend(1.0f); break;
                case 36000: engine->noteOn(52, 0.8f); engine->noteOn(59, 0.7f); break;
                case 60000: engine->noteOff(40); engine->noteOff(47); break;
                case 72000: engine->setPitchBend(0.0f); engine->noteOn(47, 1.0f); break;
                case 96000: for (int n : { 40, 47, 52, 56, 59, 64 }) engine->noteOff(n); break;
                case 100000: engine->noteOn(45, 0.6f); break;
                default: break;
                }
                engine->process(left.data(), right.data(), 1);
            }
            onSample(sample);
        }
    };

    for (bool coupled : { true, false })
    {
        std::vector<std::unique_ptr<acustra::AcustraEngine>> engines;
        engines.push_back(std::make_unique<acustra::AcustraEngine>());
        engines[0]->prepare(48000, 64);
        engines[0]->setBridgeCouplingEnabled(coupled);
        Access::setWeights(*engines[0], { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
        long mismatches = 0;
        play(engines, [&] (int)
        {
            if (Access::piezoForce(*engines[0]) != engines[0]->getLastBridgeReactionForce())
                ++mismatches;
        });
        expect(mismatches == 0, "unit piezo weights did not reproduce the reaction force");
    }

    std::vector<std::unique_ptr<acustra::AcustraEngine>> engines;
    for (int index = 0; index < 7; ++index)
    {
        engines.push_back(std::make_unique<acustra::AcustraEngine>());
        engines.back()->prepare(48000, 64);
        if (index > 0)
        {
            std::array<float, 6> unit {};
            unit[static_cast<std::size_t>(index - 1)] = 1.0f;
            Access::setWeights(*engines.back(), unit);
        }
    }
    double worst = 0.0;
    play(engines, [&] (int)
    {
        double expected = 0.0, scale = 1.0e-12;
        for (std::size_t string = 0; string < 6; ++string)
        {
            const double part = Design::stringWeights[string]
                * static_cast<double>(Access::piezoWave(*engines[string + 1]));
            expected += part;
            scale += std::abs(part);
        }
        worst = std::max(worst,
            std::abs(Access::piezoWave(*engines[0]) - expected) / scale);
    });
    expect(worst < 1.0e-6, "weighted saddle force is not the weighted sum of the strings' forces");
    std::cout << "Piezo weighted force: worst relative error " << worst << '\n';
}

// 2. The saddle resonance against the analog second-order low-pass.
void testPiezoSaddleResonance()
{
    const double analogPeakDb = decibels(Design::saddleQ
        / std::sqrt(1.0 - 1.0 / (4.0 * Design::saddleQ * Design::saddleQ)));
    const double analogPeakRatio
        = std::sqrt(1.0 - 1.0 / (2.0 * Design::saddleQ * Design::saddleQ));
    for (int rate : { 8000, 44100, 48000, 96000, 192000, 384000 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 64);
        std::vector<float> impulse(static_cast<std::size_t>(rate / 20));
        for (std::size_t i = 0; i < impulse.size(); ++i)
            impulse[i] = Access::saddle(*engine, i == 0 ? 1.0f : 0.0f);
        const double f0 = std::min(Design::saddleHz, 0.4 * rate);
        expect(std::abs(std::abs(responseAt(impulse, 0.0, rate)) - 1.0) < 1.0e-5,
               "saddle resonance DC gain is not 1 at " + std::to_string(rate));
        expect(std::abs(decibels(std::abs(responseAt(impulse, f0, rate)))
                        - decibels(Design::saddleQ)) < 0.02,
               "saddle resonance gain at f0 is not Q at " + std::to_string(rate));
        double peak = 0.0, peakFrequency = 0.0;
        for (double f = 0.5 * f0; f <= std::min(1.5 * f0, 0.49 * rate); f += f0 / 4000.0)
        {
            const double magnitude = std::abs(responseAt(impulse, f, rate));
            if (magnitude > peak)
            {
                peak = magnitude;
                peakFrequency = f;
            }
        }
        expect(std::abs(decibels(peak) - analogPeakDb) < 0.2,
               "saddle peak gain misses the analog peak at " + std::to_string(rate));
        expect(std::abs(peakFrequency / (analogPeakRatio * f0) - 1.0) < 0.01,
               "saddle peak frequency misses the analog peak at " + std::to_string(rate));
        // Across the band a 44.1 kHz host hears, the magnitude follows the
        // analog response: a bilinear design is 9 dB low at 15 kHz there.
        double worst = 0.0;
        for (double f = 20.0; f <= std::min(15000.0, 0.34 * rate); f *= 1.02)
            worst = std::max(worst, std::abs(decibels(std::abs(responseAt(impulse, f, rate)))
                                             - decibels(std::abs(analogSaddle(f, rate)))));
        expect(worst < 0.35, "saddle resonance departs from the analog magnitude at "
                                 + std::to_string(rate) + " by " + std::to_string(worst) + " dB");
        if (4.0 * f0 <= 0.34 * rate)
        {
            const double slope = decibels(std::abs(responseAt(impulse, 4.0 * f0, rate))
                                          / std::abs(responseAt(impulse, 2.0 * f0, rate)));
            const double analogSlope = decibels(std::abs(analogSaddle(4.0 * f0, rate))
                                                / std::abs(analogSaddle(2.0 * f0, rate)));
            expect(std::abs(slope - analogSlope) < 0.5,
                   "saddle resonance roll-off above f0 is not second order");
        }
        std::cout << "Saddle resonance at " << rate << " Hz: peak "
                  << decibels(peak) << " dB at " << peakFrequency
                  << " Hz, worst analog error " << worst << " dB\n";
        for (bool allSoundOff : { false, true })
        {
            Access::saddle(*engine, 0.37f);
            if (allSoundOff)
                engine->allSoundOff();
            else
                engine->reset();
            expect(Access::saddle(*engine, 0.0f) == 0.0f,
                   "hard reset retained the saddle resonance's state");
        }
    }
}

// 4. The preamp's buffer: the implementation against the curve it is
// written from, evaluated in double, and the curve's own properties.
double referencePreamp(double volts)
{
    const double rail = volts >= 0.0 ? Design::positiveRail : Design::negativeRail;
    const double knee = static_cast<double>(Design::kneeShare) * rail;
    double clipped = volts;
    if (std::abs(volts) > knee)
    {
        const double excess = std::abs(volts) - knee;
        clipped = std::copysign(knee + excess / (1.0 + excess / (rail - knee)), volts);
    }
    return clipped + Design::evenOrder * clipped * clipped;
}

void testPiezoPreampCurve()
{
    expect(Access::preamp(0.0f) == 0.0f, "preamp does not map 0 to 0");
    float previous = Access::preamp(-10.0f);
    bool monotonic = true, bounded = true, faithful = true, exactBelowKnee = true;
    for (int step = -200000; step <= 200000; ++step)
    {
        const float volts = 5.0e-5f * static_cast<float>(step);
        const float out = Access::preamp(volts);
        monotonic = monotonic && out >= previous;
        previous = out;
        const double rail = volts >= 0.0f ? Design::positiveRail : Design::negativeRail;
        bounded = bounded && std::abs(out) <= rail + Design::evenOrder * rail * rail;
        faithful = faithful && std::abs(out - referencePreamp(volts)) <= 4.0e-7;
        const float knee = Design::kneeShare
            * (volts >= 0.0f ? Design::positiveRail : Design::negativeRail);
        if (std::abs(volts) <= knee)
            exactBelowKnee = exactBelowKnee
                && std::abs(out - (volts + Design::evenOrder * volts * volts))
                       <= 1.2e-7f * std::abs(volts);
    }
    expect(monotonic, "preamp curve is not monotonic on +-10 V");
    expect(bounded, "preamp curve leaves its rails");
    expect(faithful, "preamp implementation departs from its curve");
    expect(exactBelowKnee, "preamp is not c + a c^2 below the knee");
    // Below 1 uV the even-order term is under half an ulp: skipping it there
    // must change nothing.
    bool tinyExact = true;
    for (float volts = 1.0e-30f; volts < 1.0e-6f; volts *= 1.01f)
        for (float sign : { -1.0f, 1.0f })
        {
            const float x = sign * volts;
            tinyExact = tinyExact && Access::preamp(x) == x + Design::evenOrder * x * x;
        }
    expect(tinyExact, "skipping the preamp's square below 1 uV changed a sample");
    // C1 at both knees: the curve's one-sided derivatives, in double.
    for (double rail : { static_cast<double>(Design::positiveRail),
                         -static_cast<double>(Design::negativeRail) })
    {
        const double knee = Design::kneeShare * rail;
        constexpr double h = 1.0e-7;
        const double outer = (referencePreamp(knee + std::copysign(h, rail))
                              - referencePreamp(knee)) / std::copysign(h, rail);
        const double inner = (referencePreamp(knee)
                              - referencePreamp(knee - std::copysign(h, rail)))
            / std::copysign(h, rail);
        expect(std::abs(outer - inner) < 1.0e-4, "preamp curve has a slope step at a knee");
    }
    // The even-order term's second harmonic is a A / 2 of a sine of peak A
    // below the knee: the chosen 0.5% at 1 V. A 1 V sine itself would reach
    // the lower knee (0.808 V), whose asymmetry adds its own, so the term is
    // measured at 0.5 V, where it gives 0.25%, -52.04 dB.
    expect(std::abs(Design::evenOrder / 2.0 - 0.005) < 1.0e-9,
           "the preamp's even-order term is not 0.5% at 1 V");
    constexpr int rate = 48000, length = 4800;
    std::vector<float> out(length);
    for (int i = 0; i < length; ++i)
        out[static_cast<std::size_t>(i)] = Access::preamp(static_cast<float>(
            0.5 * std::sin(2.0 * pi * 1000.0 * i / rate)));
    const double hd2 = decibels(std::abs(responseAt(out, 2000.0, rate))
                                / std::abs(responseAt(out, 1000.0, rate)));
    expect(std::abs(hd2 - decibels(0.0025)) < 0.1, "preamp HD2 at 0.5 V is not -52 dB");
    std::cout << "Piezo preamp: HD2 at 0.5 V " << hd2 << " dB\n";
}

// 5. The output coupling against its analog high-pass s / (s + wc).
void testPiezoCoupling()
{
    for (int rate : { 8000, 44100, 48000, 96000, 384000 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 64);
        std::vector<float> impulse(static_cast<std::size_t>(rate) * 2);
        for (std::size_t i = 0; i < impulse.size(); ++i)
            impulse[i] = Access::coupling(*engine, i == 0 ? 1.0f : 0.0f);
        const double pole = std::exp(-2.0 * pi * Design::couplingHz / rate);
        double worst = 0.0;
        for (double f = 20.0; f <= 0.45 * rate; f *= 1.05)
        {
            const auto z = std::polar(1.0, -2.0 * pi * f / rate);
            const auto digital = (1.0 - z) / (1.0 - pole * z);
            const std::complex<double> s { 0.0, f / Design::couplingHz };
            const auto analog = s / (s + 1.0);
            const auto actual = responseAt(impulse, f, rate);
            expect(std::abs(actual - digital) < 1.0e-4,
                   "output coupling is not its one-pole high-pass");
            worst = std::max(worst, std::abs(decibels(std::abs(actual))
                                             - decibels(std::abs(analog))));
        }
        expect(worst < 0.02, "output coupling departs from the analog 5 Hz high-pass");
        engine->reset();
        float held = 0.0f;
        for (int i = 0; i < 2 * rate; ++i)
            held = Access::coupling(*engine, 0.5f);
        expect(held == 0.0f, "output coupling passed sustained DC");
    }
}

// 5b. Aliasing: the preamp runs at the host rate without oversampling. Its
// output for the hardest strums - velocity 127 at full Touch, Finger and
// the hotter Pick, on both string materials - is compared with the same
// curve run 8x oversampled (band-limited interpolation in double) and
// brought back to the host rate; both then pass the host-rate output
// coupling. What differs below 0.45 fs is what the curve folded back.
void testPiezoAliasing()
{
    for (int rate : { 44100, 48000 })
    for (auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    for (auto picking : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick })
    {
        auto strum = hardStrum(rate, 1.0f, 1.0f, material, picking);
        auto& volts = strum.volts;
        const std::size_t fade = static_cast<std::size_t>(rate / 10);
        for (std::size_t i = 0; i < fade; ++i)
            volts[volts.size() - fade + i] *= static_cast<float>(
                0.5 * (1.0 + std::cos(pi * static_cast<double>(i) / fade)));
        std::size_t size = 1;
        while (size < volts.size())
            size <<= 1;
        constexpr std::size_t factor = 8;
        std::vector<std::complex<double>> direct(size), input(size);
        for (std::size_t i = 0; i < volts.size(); ++i)
        {
            input[i] = volts[i];
            direct[i] = Access::preamp(volts[i]);
        }
        fft(input, -1);
        std::vector<std::complex<double>> upsampled(size * factor);
        for (std::size_t k = 0; k < size / 2; ++k)
        {
            upsampled[k] = input[k];
            if (k > 0)
                upsampled[size * factor - k] = input[size - k];
        }
        upsampled[size / 2] = 0.5 * input[size / 2];
        upsampled[size * factor - size / 2] = 0.5 * input[size / 2];
        fft(upsampled, 1);
        for (auto& sample : upsampled)
            sample = referencePreamp(sample.real() / static_cast<double>(size));
        fft(upsampled, -1);
        fft(direct, -1);
        const double pole = std::exp(-2.0 * pi * Design::couplingHz / rate);
        double error = 0.0, signal = 0.0;
        for (std::size_t k = 0; k <= static_cast<std::size_t>(0.45 * size); ++k)
        {
            const auto z = std::polar(1.0, -2.0 * pi * static_cast<double>(k) / size);
            const auto coupling = (1.0 - z) / (1.0 - pole * z);
            const auto reference = coupling * upsampled[k] / static_cast<double>(factor);
            error += std::norm(coupling * direct[k] - reference);
            signal += std::norm(reference);
        }
        const double aliasDb = 10.0 * std::log10(error / signal);
        const std::string name = std::string(
            material == acustra::StringMaterial::Steel ? "steel " : "nylon ")
            + (picking == acustra::PickingTechnique::Pick ? "Pick" : "Finger");
        expect(aliasDb < -70.0, "piezo preamp aliasing is above -70 dB on the "
                                    + name + " strum at " + std::to_string(rate));
        std::cout << "Piezo preamp aliasing on the hardest " << name << " strum at "
                  << rate << " Hz: " << aliasDb << " dB\n";
    }
}

// 6. Silence. A never-played engine is exact zero from its first sample on
// Main and on the Piezo output; after the saddle force stops, the whole
// chain is exact zero within 1.5 s and stays there.
void testPiezoSilence()
{
    for (int rate : { 44100, 48000, 96000 })
    {
        acustra::EngineParameters parameters;
        parameters.capture = acustra::CaptureType::Piezo;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(rate, 64);
        std::array<float, 64> left {}, right {}, piezo {};
        bool silent = true;
        for (int block = 0; block < rate / 64; ++block)
        {
            engine->process(left.data(), right.data(),
                            acustra::AcustraEngine::OutputBuses { piezo.data() }, 64);
            for (std::size_t i = 0; i < 64; ++i)
                silent = silent && left[i] == 0.0f && right[i] == 0.0f
                    && piezo[i] == 0.0f;
        }
        expect(silent, "a never-played engine was not exact silence");
    }
    for (int rate : { 44100, 48000, 96000, 192000 })
    {
        const auto strum = hardStrum(rate, 1.0f, 1.0f, acustra::StringMaterial::Steel,
                                     acustra::PickingTechnique::Pick, 0.3);
        // Cut where the chain is driven hardest, and leave it to ring down.
        std::size_t cut = 0;
        for (std::size_t i = 0; i < strum.volts.size(); ++i)
            if (std::abs(strum.volts[i]) > std::abs(strum.volts[cut]))
                cut = i;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rate, 64);
        for (std::size_t i = 0; i <= cut; ++i)
            Access::chain(*engine, strum.force[i]);
        long lastNonzero = -1;
        for (long i = 0; i < 11L * rate; ++i)
            if (Access::chain(*engine, 0.0f) != 0.0f)
                lastNonzero = i;
        const double seconds = static_cast<double>(lastNonzero + 1) / rate;
        expect(seconds <= 1.5, "piezo chain took " + std::to_string(seconds)
                                   + " s to reach exact silence at " + std::to_string(rate));
        const auto state = Access::chainState(*engine);
        for (std::size_t index = 0; index < 6; ++index)
            expect(state[index] == 0.0f, "piezo chain kept state after reaching silence");
        std::cout << "Piezo chain silent " << seconds << " s after its hardest moment at "
                  << rate << " Hz\n";
    }
}

// 7. Switching. The unheard chain carries exactly the state of one heard all
// along, so a switch crossfades to the ringing instrument's voltage; a mid-
// note switch rises no more than the renders either side of it; and a
// Piezo output cabled mid-note gives what one cabled from the start does.
void testLoadedPiezoStaysWarmWhileUnheard()
{
    for (auto material : { acustra::StringMaterial::Steel,
                           acustra::StringMaterial::Nylon })
        for (int rate : { 44100, 48000, 96000 })
        {
            acustra::EngineParameters parameters;
            parameters.stringMaterial = material;
            auto switched = std::make_unique<acustra::AcustraEngine>();
            auto reference = std::make_unique<acustra::AcustraEngine>();
            switched->setParameters(parameters);
            parameters.capture = acustra::CaptureType::Piezo;
            reference->setParameters(parameters);
            for (auto* engine : { switched.get(), reference.get() })
            {
                engine->prepare(rate, 64);
                engine->noteOn(45, 0.7f);
            }
            std::array<float, 64> left {}, right {}, referenceLeft {}, referenceRight {};
            for (int block = 0; block < 600; ++block)
            {
                if (block == 40)
                    switched->setParameters(parameters);
                switched->process(left.data(), right.data(), 64);
                reference->process(referenceLeft.data(), referenceRight.data(), 64);
                expect(Access::chainState(*switched) == Access::chainState(*reference),
                       "the unheard piezo chain lost its state");
                expect(switched->getLastBridgeBodyForce()
                           == reference->getLastBridgeBodyForce(),
                       "piezo electrical observation changed the mechanical bridge");
            }
            expect(left == right && left == referenceLeft && right == referenceRight,
                   "switching to warmed piezo did not reach the continuously observed output");
        }
}

// The largest rise of the band above 200 Hz from one 5 ms frame to the
// frame two 2.5 ms hops before it, over [from, to) seconds.
double largestRise(const std::vector<float>& signal, int rate, double from, double to)
{
    // Second-order Butterworth high-pass at 200 Hz, bilinear, in double.
    const double k = std::tan(pi * 200.0 / rate);
    const double norm = 1.0 / (1.0 + std::sqrt(2.0) * k + k * k);
    const double b0 = norm, b1 = -2.0 * norm, b2 = norm;
    const double a1 = 2.0 * (k * k - 1.0) * norm;
    const double a2 = (1.0 - std::sqrt(2.0) * k + k * k) * norm;
    std::vector<double> band(signal.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        const double x = signal[i];
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        band[i] = y;
    }
    std::vector<double> frames;
    for (double at = from - 0.005; at + 0.005 <= to; at += 0.0025)
    {
        const auto start = static_cast<std::size_t>(at * rate);
        const auto count = static_cast<std::size_t>(0.005 * rate);
        double energy = 0.0;
        for (std::size_t i = start; i < start + count; ++i)
            energy += band[i] * band[i];
        frames.push_back(std::sqrt(energy / static_cast<double>(count)));
    }
    double worst = 0.0;
    for (std::size_t i = 2; i < frames.size(); ++i)
        worst = std::max(worst, frames[i] / std::max(frames[i - 2], 1.0e-30));
    return worst;
}

void testPiezoSwitchingIsClickFree()
{
    // A mid-note switch between the microphones and the piezo is the 20 ms
    // Capture crossfade between two continuous signals and nothing else: the
    // switched render equals the crossfade of the two unswitched renders,
    // sample for sample, and its largest frame rise is that crossfade's. A
    // rise measured against the unswitched renders alone also counts the
    // crossfade's own level change where the two sensors differ in level
    // for that note (up to 1.26x here), so it is reported, not gated.
    double worstRatio = 0.0, worstRawRatio = 0.0, worstError = 0.0;
    using acustra::CaptureType;
    for (auto material : { acustra::StringMaterial::Steel,
                           acustra::StringMaterial::Nylon })
        for (int rate : { 44100, 48000, 96000 })
            for (int note : { 45, 52, 64 })
                for (bool toPiezo : { true, false })
                {
                    const CaptureType from = toPiezo ? CaptureType::StereoMic : CaptureType::Piezo;
                    const CaptureType to = toPiezo ? CaptureType::Piezo : CaptureType::StereoMic;
                    std::array<std::unique_ptr<acustra::AcustraEngine>, 3> engines;
                    const std::array<CaptureType, 3> starts { from, CaptureType::StereoMic,
                                                              CaptureType::Piezo };
                    for (std::size_t index = 0; index < 3; ++index)
                    {
                        acustra::EngineParameters parameters;
                        parameters.stringMaterial = material;
                        parameters.capture = starts[index];
                        engines[index] = std::make_unique<acustra::AcustraEngine>();
                        engines[index]->setParameters(parameters);
                        engines[index]->prepare(rate, 64);
                        engines[index]->noteOn(note, 0.8f);
                    }
                    std::array<std::vector<float>, 3> outputs;
                    std::vector<float> crossfade;
                    for (int sample = 0; sample < rate * 7 / 10; ++sample)
                    {
                        if (sample == rate / 2)
                        {
                            acustra::EngineParameters parameters;
                            parameters.stringMaterial = material;
                            parameters.capture = to;
                            engines[0]->setParameters(parameters);
                        }
                        for (std::size_t index = 0; index < 3; ++index)
                        {
                            float left = 0.0f, right = 0.0f;
                            engines[index]->process(&left, &right, 1);
                            outputs[index].push_back(left);
                        }
                        const auto mix = Access::captureMix(*engines[0]);
                        crossfade.push_back(mix[0] * outputs[1].back()
                                            + mix[6] * outputs[2].back());
                        worstError = std::max(worstError, static_cast<double>(
                            std::abs(outputs[0].back() - crossfade.back())));
                    }
                    const double switched = largestRise(outputs[0], rate, 0.45, 0.65);
                    const double expected = largestRise(crossfade, rate, 0.45, 0.65);
                    const double stayed = std::max(largestRise(outputs[1], rate, 0.45, 0.65),
                                                   largestRise(outputs[2], rate, 0.45, 0.65));
                    worstRatio = std::max(worstRatio, switched / expected);
                    worstRawRatio = std::max(worstRawRatio, switched / stayed);
                    expect(switched <= 1.1 * expected,
                           "switching Capture mid-note rose more than its crossfade at "
                               + std::to_string(rate));
                }
    expect(worstError < 1.0e-6, "a Capture switch is not the crossfade of the two sensors");
    std::cout << "Piezo capture switch: largest rise " << worstRatio
              << "x its crossfade's (" << worstRawRatio
              << "x the unswitched renders'), largest departure from the crossfade "
              << worstError << '\n';

    // Cabling the Piezo output mid-note.
    for (int rate : { 44100, 96000 })
    {
        auto always = std::make_unique<acustra::AcustraEngine>();
        auto later = std::make_unique<acustra::AcustraEngine>();
        for (auto* engine : { always.get(), later.get() })
        {
            engine->prepare(rate, 64);
            engine->noteOn(40, 0.9f);
            engine->noteOn(52, 0.7f);
        }
        std::array<float, 64> left {}, right {}, piezo {}, laterLeft {}, laterRight {}, laterPiezo {};
        bool same = true;
        for (int block = 0; block < 800; ++block)
        {
            always->process(left.data(), right.data(),
                            acustra::AcustraEngine::OutputBuses { piezo.data() }, 64);
            if (block < 300)
                later->process(laterLeft.data(), laterRight.data(), 64);
            else
            {
                later->process(laterLeft.data(), laterRight.data(),
                               acustra::AcustraEngine::OutputBuses { laterPiezo.data() }, 64);
                same = same && laterPiezo == piezo;
            }
            same = same && laterLeft == left;
        }
        expect(same, "a Piezo output cabled mid-note differs from one cabled all along");
    }
}

// 8. The microphones never hear the piezo: whatever its weights, Main on
// the microphones is the same samples.
void testPiezoDoesNotReachTheMicrophones()
{
    for (auto capture : { acustra::CaptureType::StereoMic, acustra::CaptureType::MonoMic })
    {
        std::vector<std::vector<float>> renders;
        for (int variant = 0; variant < 3; ++variant)
        {
            acustra::EngineParameters parameters;
            parameters.capture = capture;
            auto engine = std::make_unique<acustra::AcustraEngine>();
            engine->setParameters(parameters);
            engine->prepare(48000, 64);
            if (variant == 1)
                Access::setWeights(*engine, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
            if (variant == 2)
                Access::setWeights(*engine, { 3.0f, 0.0f, -2.0f, 0.5f, 7.0f, 0.0f });
            for (int note : { 40, 47, 52, 56, 59, 64 })
                engine->noteOn(note, 1.0f);
            std::vector<float> out;
            std::array<float, 64> left {}, right {};
            for (int block = 0; block < 750; ++block)
            {
                engine->process(left.data(), right.data(), 64);
                out.insert(out.end(), left.begin(), left.end());
                out.insert(out.end(), right.begin(), right.end());
            }
            renders.push_back(out);
        }
        expect(renders[0] == renders[1] && renders[0] == renders[2],
               "the piezo's weights reached the microphones");
    }
}

// 9. Where the preamp's knee sits: velocity-100 reference strums stay on its
// exactly linear part with Finger and Pick, both string materials and any
// Touch; the hardest ones - velocity 127, and the player's strums with each
// string at its own drawn level - lose no more than 1.5 dB of their peak to
// it; and the hottest Pick strum is the sensitivity's 1 V anchor.
void testPiezoClipPlacement()
{
    const auto clipLossDb = [] (const std::vector<float>& volts)
    {
        float peak = 0.0f;
        double worst = 0.0;
        for (float value : volts)
        {
            if (std::abs(value) > std::abs(peak))
                peak = value;
            const double rail = value >= 0.0f ? Design::positiveRail : Design::negativeRail;
            const double knee = Design::kneeShare * rail;
            const double magnitude = std::abs(static_cast<double>(value));
            if (magnitude > knee)
            {
                const double excess = magnitude - knee;
                worst = std::min(worst, decibels(
                    (knee + excess / (1.0 + excess / (rail - knee))) / magnitude));
            }
        }
        return std::pair<double, double> { std::abs(static_cast<double>(peak)), worst };
    };
    const auto name = [] (acustra::StringMaterial material,
                          acustra::PickingTechnique picking)
    {
        return std::string(material == acustra::StringMaterial::Steel ? "steel " : "nylon ")
            + (picking == acustra::PickingTechnique::Pick ? "Pick" : "Finger");
    };
    double hottest = 0.0;
    for (auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    for (auto picking : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick })
    {
        for (float touch : { 0.0f, 0.58f, 1.0f })
        {
            const auto soft = hardStrum(48000, 100.0f / 127.0f, touch, material, picking);
            bool linear = true;
            for (float value : soft.volts)
                linear = linear && std::abs(value) <= Design::kneeShare
                    * (value >= 0.0f ? Design::positiveRail : Design::negativeRail);
            expect(linear, "a velocity-100 " + name(material, picking)
                               + " strum reached the piezo preamp's knee");
            const auto hard = clipLossDb(hardStrum(48000, 1.0f, touch, material, picking).volts);
            expect(hard.second >= -1.5, "the preamp's knee took more than 1.5 dB off the "
                                            + name(material, picking) + " strum");
            hottest = std::max(hottest, hard.first);
            if (touch == 1.0f)
                std::cout << "Piezo " << name(material, picking) << " strum peak, velocity 100: "
                          << clipLossDb(soft.volts).first << " V; velocity 127: "
                          << hard.first << " V, knee loss " << hard.second << " dB\n";
        }
        if (picking == acustra::PickingTechnique::Pick)
        {
            const auto strummed = clipLossDb(hardStrum(48000, 1.0f, 0.58f, material, picking,
                                                       0.0, 12).volts);
            expect(strummed.second >= -1.5, "the preamp's knee took more than 1.5 dB off "
                                                "the player's " + name(material, picking) + " strums");
            std::cout << "Piezo player's " << name(material, picking) << " strums, velocity 127: peak "
                      << strummed.first << " V, knee loss " << strummed.second << " dB\n";
        }
    }
    // The anchor is the hottest of every preset (Tools/CalibratePiezo.py);
    // the default construction's own hottest Pick strum sits just under it.
    expect(hottest <= 1.0 && hottest > 0.9, "the hottest Pick strum is not near the 1 V anchor: "
                                                + std::to_string(hottest));
}

// 6b. An idle instrument after playing: a chord released, then left alone,
// reaches exact zero on Main (Capture = Piezo) and on the Piezo output and
// stays there, although the saddle force's rounding never quite does.
void testPiezoSilentAfterPlaying()
{
    for (int rate : { 44100, 48000 })
    for (auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    {
        acustra::EngineParameters parameters;
        parameters.capture = acustra::CaptureType::Piezo;
        parameters.stringMaterial = material;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(rate, 64);
        engine->setStringPerChannelMode(true);
        constexpr std::array notes { 40, 47, 52, 56, 59, 64 };
        for (int string = 0; string < 6; ++string)
            engine->noteOn(notes[static_cast<std::size_t>(string)], 1.0f, string + 1);
        std::array<float, 64> left {}, right {}, piezo {};
        const long blocks = 30L * rate / 64;
        long lastNonzero = -1;
        for (long block = 0; block < blocks; ++block)
        {
            if (block == rate / 2 / 64)
                for (int string = 0; string < 6; ++string)
                    engine->noteOff(notes[static_cast<std::size_t>(string)], string + 1);
            engine->process(left.data(), right.data(),
                            acustra::AcustraEngine::OutputBuses { piezo.data() }, 64);
            for (std::size_t i = 0; i < 64; ++i)
                if (left[i] != 0.0f || right[i] != 0.0f || piezo[i] != 0.0f)
                    lastNonzero = block;
        }
        const double seconds = static_cast<double>((lastNonzero + 1) * 64) / rate;
        expect(seconds < 25.0, "the piezo was not exact silence after playing at "
                                   + std::to_string(rate)
                                   + (material == acustra::StringMaterial::Steel
                                          ? " (steel)" : " (nylon)"));
        std::cout << "Piezo exact silence " << seconds << " s after a chord at " << rate << " Hz ("
                  << (material == acustra::StringMaterial::Steel ? "steel" : "nylon") << ")\n";
    }
}

// 10. The chain's small-signal response - saddle resonance and electrical
// load - agrees between 44.1 and 96 kHz from 50 Hz to 15 kHz.
void testPiezoRateConsistency()
{
    std::array<std::vector<float>, 2> impulses;
    const std::array rates { 44100, 96000 };
    for (std::size_t index = 0; index < 2; ++index)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rates[index], 64);
        impulses[index].resize(static_cast<std::size_t>(rates[index] / 10));
        for (std::size_t i = 0; i < impulses[index].size(); ++i)
            impulses[index][i] = Access::loadedPiezo(*engine,
                Access::saddle(*engine, i == 0 ? 1.0f : 0.0f));
    }
    double worst = 0.0;
    for (double f = 50.0; f <= 15000.0; f *= 1.02)
        worst = std::max(worst, std::abs(
            decibels(std::abs(responseAt(impulses[0], f, rates[0])))
            - decibels(std::abs(responseAt(impulses[1], f, rates[1])))));
    expect(worst < 0.5, "the piezo chain's response differs between 44.1 and 96 kHz");
    std::cout << "Piezo chain 44.1 vs 96 kHz: worst " << worst << " dB, 50 Hz - 15 kHz\n";
}

} // namespace

int main()
{
    testCaptureObservations();
    testCaptureLifecycle();
    testLoadedPiezoElectricalResponse();
    testPiezoStringWeights();
    testPiezoSaddleResonance();
    testPiezoPreampCurve();
    testPiezoCoupling();
    testPiezoAliasing();
    testPiezoSilence();
    testPiezoSilentAfterPlaying();
    testLoadedPiezoStaysWarmWhileUnheard();
    testPiezoSwitchingIsClickFree();
    testPiezoDoesNotReachTheMicrophones();
    testPiezoClipPlacement();
    testPiezoRateConsistency();
    if (failures == 0)
        std::cout << "All Acustra capture tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
