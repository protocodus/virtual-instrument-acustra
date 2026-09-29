// Capture is a read-only observation of the instrument. Verify the microphone
// channel identity, the piezo chain in the running instrument - its string
// weights, headroom on real strums, the clip's aliasing on the hottest ones,
// switching and silence (Docs/decisions.md, 2026-09-29). The chain against
// its circuit simulation is Tests/PiezoCircuitTests.cpp; the level match to
// the microphones is Tools/CalibratePiezo.py.
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
    static float chain(AcustraEngine& engine, float force)
    {
        return engine.renderPiezo(force);
    }
    // U1B's clipped output, seven samples behind the drive the probe shows.
    static double gainStage(const AcustraEngine& engine) { return engine.piezoLastStage_; }
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
        return engine.lastPiezoForce_
            + AcustraEngine::PiezoDesign::axialShare * engine.lastLongitudinalForce_;
    }
    static std::vector<double> chainState(const AcustraEngine& engine)
    {
        std::vector<double> state { engine.piezoFrontW_, engine.piezoFrontC2_,
            engine.piezoLastOpen_, engine.piezoLastClamp_, engine.piezoC3_,
            engine.piezoC4_, engine.piezoC5_, engine.piezoLastBuffer_,
            engine.piezoLastStage_, engine.piezoOutputVolts_,
            engine.piezoTrim_, engine.lastPiezoWave_, engine.lastPiezoForce_,
            engine.lastPiezoVoltage_ };
        for (float value : engine.piezoSaddleInput_)
            state.push_back(value);
        for (float value : engine.piezoSaddleOutput_)
            state.push_back(value);
        state.insert(state.end(), engine.piezoDrive_.begin(), engine.piezoDrive_.end());
        state.insert(state.end(), engine.piezoStage_.begin(), engine.piezoStage_.end());
        state.insert(state.end(), engine.piezoForceDerivative_.history.begin(),
                     engine.piezoForceDerivative_.history.end());
        state.push_back(static_cast<double>(engine.piezoForceDerivative_.index));
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
    {
        acustra::EngineParameters parameters;
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

// One hard strum through the piezo, sample by sample: its driving force and
// the chain's probe (getLastPiezoProbe) - the element's open-circuit
// voltage, the jack, U1A's input and U1B's drive - and U1B's clipped output
// seven samples later. An open E major downstroke, low E first, 9 ms apart,
// as Tools/CalibratePiezo.py plays it. `strokes` > 0 instead plays that many
// of the player's own strums, one every 1.2 s, each string at its own drawn
// level (strumMember) - the hottest a stroke gets.
struct Strum
{
    std::vector<float> force, open, volts, input, drive;
    std::vector<double> stage;
};

Strum hardStrum(double rate, float velocity, float touch,
                acustra::PickingTechnique picking = acustra::PickingTechnique::Finger,
                double seconds = 2.5, int strokes = 0, float pluckPosition = 0.28f,
                float driveScale = 1.0f)
{
    acustra::EngineParameters parameters;
    parameters.touch = touch;
    parameters.picking = picking;
    parameters.pluckPosition = pluckPosition;
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->setParameters(parameters);
    engine->prepare(rate, 64);
    engine->setStringPerChannelMode(true);
    if (driveScale != 1.0f)
    {
        // The saddle driven harder than any playing does, through heavier
        // string weights (the chain is linear in them up to its clip).
        auto weights = Design::stringWeights;
        for (auto& weight : weights)
            weight *= driveScale;
        Access::setWeights(*engine, weights);
    }
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
        const auto probe = engine->getLastPiezoProbe();
        strum.force.push_back(Access::drivingForce(*engine));
        strum.open.push_back(probe.openCircuit);
        strum.volts.push_back(probe.jack);
        strum.input.push_back(probe.bufferInput);
        strum.drive.push_back(probe.gainStageDrive);
        strum.stage.push_back(Access::gainStage(*engine));
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

// 6. Silence. A never-played engine is exact zero from its first sample on
// Main and on the Piezo output; after the saddle force stops, the whole
// chain is exact zero within 5 s and stays there. The circuit cannot settle
// faster: its input network's 1 Hz pair and C4's loop decay over 0.34 s,
// and the chain flushes at 10 nV.
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
        const auto strum = hardStrum(rate, 1.0f, 1.0f, acustra::PickingTechnique::Pick, 0.3);
        // Cut where the chain is driven hardest, and leave it to ring down.
        std::size_t cut = 0;
        for (std::size_t i = 0; i < strum.open.size(); ++i)
            if (std::abs(strum.open[i]) > std::abs(strum.open[cut]))
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
        expect(seconds <= 5.0, "piezo chain took " + std::to_string(seconds)
                                   + " s to reach exact silence at " + std::to_string(rate));
        const auto state = Access::chainState(*engine);
        for (std::size_t index = 0; index < 10; ++index)
            expect(state[index] == 0.0, "piezo chain kept state after reaching silence");
        for (std::size_t index = 14; index < state.size() - 11; ++index)
            expect(state[index] == 0.0, "piezo chain kept state after reaching silence");
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
    for (int rate : { 44100, 48000, 96000 })
    {
        acustra::EngineParameters parameters;
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

// 6b. An idle instrument after playing: a chord released, then left alone,
// reaches exact zero on Main (Capture = Piezo) and on the Piezo output and
// stays there, although the saddle force's rounding never quite does.
void testPiezoSilentAfterPlaying()
{
    for (int rate : { 44100, 48000 })
    {
        acustra::EngineParameters parameters;
        parameters.capture = acustra::CaptureType::Piezo;
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
                                   + std::to_string(rate));
        std::cout << "Piezo exact silence " << seconds << " s after a chord at " << rate
                  << " Hz\n";
    }
}

// 10. The chain's small-signal response - saddle, element, cable and
// preamp - agrees between 44.1 and 96 kHz from 50 Hz to 15 kHz.
void testPiezoRateConsistency()
{
    std::array<std::vector<float>, 2> impulses;
    const std::array rates { 44100, 96000 };
    for (std::size_t index = 0; index < 2; ++index)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->prepare(rates[index], 64);
        impulses[index].resize(static_cast<std::size_t>(rates[index]) * 6);
        // 10 N for one sample: well inside the preamp's range, far above its
        // 10 nV flush.
        const auto impulse = static_cast<float>(10.0 / Design::newtonsPerUnit);
        for (std::size_t i = 0; i < impulses[index].size(); ++i)
            impulses[index][i] = Access::chain(*engine, i == 0 ? impulse : 0.0f);
    }
    double worst = 0.0;
    for (double f = 50.0; f <= 15000.0; f *= 1.02)
        worst = std::max(worst, std::abs(
            decibels(std::abs(responseAt(impulses[0], f, rates[0])))
            - decibels(std::abs(responseAt(impulses[1], f, rates[1])))));
    expect(worst < 0.5, "the piezo chain's response differs between 44.1 and 96 kHz");
    std::cout << "Piezo chain 44.1 vs 96 kHz: worst " << worst << " dB, 50 Hz - 15 kHz\n";
}

// 9. Headroom on real playing. Reference strums at velocity 100 and 127, with
// Finger and Pick and any Touch, and the player's own
// strums, never reach U1B's output swing or U1A's input range: the circuit
// only clips when played far past normal (the Pick at velocity 127 at the
// bridge, Pluck Position 0). The hottest strum's open-circuit voltage stays
// within 0.7-2.5 V, Zollner's 1-2 V for a piezo played loudly with margin
// for the Ovation element standing in for a strip (Tools/CalibratePiezo.py
// checks every preset).
void testPiezoHeadroom()
{
    const auto name = [] (acustra::PickingTechnique picking)
    {
        return std::string(picking == acustra::PickingTechnique::Pick ? "Pick" : "Finger");
    };
    // Headroom in dB to the nearer limit of U1B's swing and U1A's range.
    const auto headroom = [] (const Strum& strum)
    {
        double worst = 300.0;
        for (std::size_t i = 0; i < strum.drive.size(); ++i)
        {
            const double drive = strum.drive[i], input = strum.input[i];
            if (drive > 0.0)
                worst = std::min(worst, decibels(Design::railHigh / drive));
            if (drive < 0.0)
                worst = std::min(worst, decibels(Design::railLow / drive));
            if (input != 0.0)
                worst = std::min(worst, decibels(Design::commonModeLimit / std::abs(input)));
        }
        return worst;
    };
    const auto peak = [] (const std::vector<float>& values)
    {
        double largest = 0.0;
        for (float value : values)
            largest = std::max(largest, static_cast<double>(std::abs(value)));
        return largest;
    };
    double hottest = 0.0, least = 300.0;
    for (auto picking : { acustra::PickingTechnique::Finger, acustra::PickingTechnique::Pick })
    {
        for (float touch : { 0.0f, 0.58f, 1.0f })
            for (float velocity : { 100.0f / 127.0f, 1.0f })
            {
                const auto strum = hardStrum(48000, velocity, touch, picking);
                const double room = headroom(strum);
                least = std::min(least, room);
                hottest = std::max(hottest, peak(strum.open));
                expect(room > 0.0, "a reference " + name(picking)
                                       + " strum clipped the piezo preamp");
                if (touch == 1.0f && velocity == 1.0f)
                    std::cout << "Piezo " << name(picking) << " strum, velocity 127: "
                              << "open-circuit peak " << peak(strum.open) << " V, jack "
                              << peak(strum.volts) << " V, headroom " << room << " dB\n";
            }
        if (picking == acustra::PickingTechnique::Pick)
        {
            const auto strummed = hardStrum(48000, 1.0f, 0.58f, picking, 0.0, 12);
            const double room = headroom(strummed);
            least = std::min(least, room);
            expect(room > 0.0, "the player's " + name(picking)
                                   + " strums clipped the piezo preamp");
            std::cout << "Piezo player's " << name(picking) << " strums, velocity 127: "
                      << "open-circuit peak " << peak(strummed.open) << " V, headroom "
                      << room << " dB\n";
            const auto bridge = hardStrum(48000, 1.0f, 1.0f, picking, 2.5, 0, 0.0f);
            std::cout << "Piezo " << name(picking)
                      << " at velocity 127, Pluck Position 0: open-circuit peak "
                      << peak(bridge.open) << " V, headroom " << headroom(bridge) << " dB\n";
        }
    }
    expect(hottest >= 0.7 && hottest <= 2.5, "the hottest strum's open-circuit voltage "
                                                 + std::to_string(hottest) + " V is outside 0.7-2.5 V");
    std::cout << "Piezo: hottest reference strum " << hottest << " V open-circuit; least headroom "
              << least << " dB\n";
}

// 5b. The clip's aliasing: the Pick at velocity 127 at the bridge (Pluck
// Position 0), the hottest playing, keeps 2.2 dB of U1B's swing, so it is
// driven 4.4 dB harder here (string weights 1.66 times) to pass the swing by
// 2.2 dB. U1B's drive, band-limited and run through the same clip 8x
// oversampled, then brought back to the host rate, is the reference for the chain's clipped output;
// what differs below 0.45 fs is what the host-rate clip folded back, less
// what the BLAMP took out - and what the BLAMP's own kernel does to the band,
// which a bare clip, reported beside it, does not.
void testPiezoClipAliasing()
{
    for (int rate : { 44100, 48000 })
    {
        auto strum = hardStrum(rate, 1.0f, 1.0f, acustra::PickingTechnique::Pick,
                               1.0, 0, 0.0f, 1.66f);
        auto& drive = strum.drive;
        std::vector<double> stage(strum.stage.begin() + 7, strum.stage.end());
        drive.resize(stage.size());
        const std::size_t fade = static_cast<std::size_t>(rate / 20);
        std::size_t size = 1;
        while (size < drive.size())
            size <<= 1;
        constexpr std::size_t factor = 8;
        std::vector<std::complex<double>> direct(size), input(size), naive(size);
        for (std::size_t i = 0; i < drive.size(); ++i)
        {
            const double window = i + fade >= drive.size()
                ? 0.5 * (1.0 + std::cos(pi * static_cast<double>(i + fade - drive.size()) / fade))
                : 1.0;
            input[i] = window * drive[i];
            direct[i] = window * stage[i];
            naive[i] = window * std::clamp(static_cast<double>(drive[i]), Design::railLow, Design::railHigh);
        }
        fft(naive, -1);
        fft(input, -1);
        std::vector<std::complex<double>> upsampled(size * factor);
        for (std::size_t k = 0; k < size / 2; ++k)
        {
            upsampled[k] = input[k];
            if (k > 0)
                upsampled[size * factor - k] = input[size - k];
        }
        fft(upsampled, 1);
        long clipped = 0;
        for (auto& sample : upsampled)
        {
            const double value = sample.real() / static_cast<double>(size);
            if (value > Design::railHigh || value < Design::railLow)
                ++clipped;
            sample = std::clamp(value, Design::railLow, Design::railHigh);
        }
        fft(upsampled, -1);
        fft(direct, -1);
        // What differs below 0.45 fs, and, as a check on what that is, what
        // the bare host-rate clip leaves and what differs below 0.25 fs,
        // where the BLAMP's own kernel is flat.
        double error = 0.0, signal = 0.0, bare = 0.0, lowError = 0.0, lowSignal = 0.0;
        for (std::size_t k = 0; k <= static_cast<std::size_t>(0.45 * size); ++k)
        {
            const auto reference = upsampled[k] / static_cast<double>(factor);
            error += std::norm(direct[k] - reference);
            bare += std::norm(naive[k] - reference);
            signal += std::norm(reference);
            if (k <= size / 4)
            {
                lowError += std::norm(direct[k] - reference);
                lowSignal += std::norm(reference);
            }
        }
        const double aliasDb = 10.0 * std::log10(std::max(error, 1.0e-300) / signal);
        const double bareDb = 10.0 * std::log10(std::max(bare, 1.0e-300) / signal);
        const double lowDb = 10.0 * std::log10(std::max(lowError, 1.0e-300) / lowSignal);
        const std::string label = "at " + std::to_string(rate);
        expect(clipped > 0, "the Pick at the bridge driven 4.4 dB harder does not clip the piezo preamp, "
                                + label);
        // Steel measures -62.0 and -65.4 dB here, a bare clip -56.7 and -58.4.
        expect(aliasDb < -60.0 && aliasDb < bareDb - 5.0,
               "piezo clip aliasing on an overdriven strum is above -60 dB or not 5 dB under a bare clip, "
                   + label);
        std::cout << "Piezo clip aliasing, Pick at velocity 127 at the bridge +4.4 dB, " << label << ": "
                  << aliasDb << " dB, below 0.25 fs " << lowDb << " dB; a bare clip "
                  << bareDb << " dB (" << clipped / static_cast<long>(factor)
                  << " host samples past the swing)\n";
    }
}

} // namespace

int main()
{
    testCaptureObservations();
    testCaptureLifecycle();
    testPiezoStringWeights();
    testPiezoClipAliasing();
    testPiezoSilence();
    testPiezoSilentAfterPlaying();
    testLoadedPiezoStaysWarmWhileUnheard();
    testPiezoSwitchingIsClickFree();
    testPiezoDoesNotReachTheMicrophones();
    testPiezoHeadroom();
    testPiezoRateConsistency();
    if (failures == 0)
        std::cout << "All Acustra capture tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
