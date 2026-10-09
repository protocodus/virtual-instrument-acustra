// Capture is a read-only observation of the instrument. Verify the microphone
// channel identity, the piezo chain in the running instrument - its string
// weights, headroom on real strums, the clip's aliasing on the hottest ones,
// switching and silence (Docs/decisions.md, 2026-09-29) - and the room at the
// microphones and their capture voicing (2026-10-01). The chain against
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
    // Newtons per engine force unit: the calibration's displacement unit.
    static double newtonsPerUnit(const AcustraEngine& engine)
    {
        return engine.piezoNewtonsPerUnit_;
    }
    // U1B's clipped output, seven samples behind the drive the probe shows.
    static double gainStage(const AcustraEngine& engine) { return engine.piezoLastStage_; }
    // U1B's drive before U1A's input-range/diode knees, the same continuous
    // corner source that renderPiezo retains in its four-sample history.
    static double freeDrive(const AcustraEngine& engine) { return engine.piezoDrive_[3]; }
    static void setWeights(AcustraEngine& engine,
                           const std::array<float, 6>& weights)
    {
        engine.piezoStringWeights_ = weights;
    }
    // The released static force (initialisePluck) on or off.
    static void setReleaseStep(AcustraEngine& engine, bool enabled)
    {
        engine.releaseStepEnabled_ = enabled;
    }
    static float piezoWave(const AcustraEngine& engine) { return engine.lastPiezoWave_; }
    static std::array<float, 8> captureMix(const AcustraEngine& engine)
    {
        return engine.captureMix_;
    }
    static float outputGain(const AcustraEngine& engine) { return engine.outputGain_; }
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
    // The room's own impulse response at the engine's rate, left and right
    // interleaved, from a cleared room that is cleared again afterwards.
    static std::vector<float> roomImpulse(AcustraEngine& engine, int samples)
    {
        auto& room = engine.room_;
        room.reset();
        std::vector<float> out(2 * static_cast<std::size_t>(samples));
        for (int index = 0; index < samples; ++index)
        {
            float left = 0.0f, right = 0.0f;
            room.process(index == 0 ? 1.0f : 0.0f, left, right);
            out[2 * static_cast<std::size_t>(index)] = left;
            out[2 * static_cast<std::size_t>(index) + 1] = right;
        }
        room.reset();
        return out;
    }
    static float voicing(float frequency)
    {
        return AcustraEngine::captureVoicingGain(frequency);
    }
    static auto captureFilter(const AcustraEngine& engine)
    {
        return engine.bodyBank_.captureFilter;
    }
    template <typename Filter>
    static std::array<float, 2> filterSample(Filter& filter, float left, float right)
    {
        AcustraEngine::BodyOutput value { left, right, right };
        filter.render(value);
        return { value.left, value.right };
    }
    static void bypassCaptureFilters(AcustraEngine& engine)
    {
        engine.bodyBank_.captureFilter.enabled = false;
        engine.fadingBodyBank_.captureFilter.enabled = false;
    }
    static auto body(AcustraEngine& engine, float force, float moment)
    {
        return engine.renderBody(force, moment);
    }
    static auto bodyBank(const AcustraEngine& engine)
    {
        return engine.bodyBank_;
    }
    template <typename Filter>
    static auto filterHistory(const Filter& filter)
    {
        std::array<double, 4 * Filter::sections> values {};
        std::size_t index = 0;
        for (const auto* channel : { &filter.stateLeft, &filter.stateRight })
            for (const auto& section : *channel)
                for (const double value : section)
                    values[index++] = value;
        return values;
    }
    static auto filterStates(const AcustraEngine& engine)
    {
        const auto active = filterHistory(engine.bodyBank_.captureFilter);
        const auto fading = filterHistory(engine.fadingBodyBank_.captureFilter);
        std::array<double, 2 * std::tuple_size<decltype(active)>::value> values {};
        std::copy(active.begin(), active.end(), values.begin());
        std::copy(fading.begin(), fading.end(), values.begin() + active.size());
        return values;
    }
    static auto fadingFilterStates(const AcustraEngine& engine)
    {
        return filterHistory(engine.fadingBodyBank_.captureFilter);
    }
    static float bodyFade(const AcustraEngine& engine) { return engine.bodyModelFade_; }
    static bool bodyPending(const AcustraEngine& engine) { return engine.bodyUpdatePending_; }
    static bool hasBodyModel(const AcustraEngine& engine, GuitarModel model)
    {
        return engine.configuredGuitarModel_ == model;
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
    std::vector<double> stage, freeDrive;
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
        strum.freeDrive.push_back(Access::freeDrive(*engine));
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
        // The released static force acts on the saddle beside the strings'
        // waves and the piezo does not weigh it (its preamp's headroom was
        // set without it), so the identity is the strings' own.
        Access::setReleaseStep(*engines[0], false);
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
        const auto impulse = static_cast<float>(10.0 / Access::newtonsPerUnit(*engine));
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
// Position 0), driven beyond U1B's swing through heavier string weights.
// U1A's input range and diodes also act, entirely inside U1B's saturated
// interval. The guard below verifies that clipping the free and actual
// host drives gives identical samples, at the float probe's precision.
// Interpolate the free drive and clip it 8x oversampled for the reference:
// interpolating the already kinked U1A output would invent different U1B
// corners, even though U1B hides those input knees. The independent circuit
// overload/harmonic tests cover the input section's physical behavior.
// What differs below 0.45 fs is what the host-rate clip folded back, less
// what the BLAMP took out, including its kernel's passband error. A bare
// clip of that same free drive is reported beside it.
// The strum is taken at the default Touch. At velocity 127 the contact's
// Touch law saturates, so until 2026-09-30 any Touch from the default up
// played this strum sample for sample, and the drives below were set on it;
// the release's slip now follows Touch itself (a firm Touch lets the string
// go over a smaller edge, 0.56 of it at Touch 1), so the brighter Touch 1
// strum is read beside it and held to the same -60 dB at every drive.
void testPiezoClipAliasing()
{
    for (const float touch : { 0.58f, 1.0f })
    for (int rate : { 44100, 48000 })
    {
    double chainSum = 0.0, bareSum = 0.0;
    int drives = 0;
    for (const float overdrive : { 1.25f, 1.30f, 1.35f, 1.40f, 1.45f })
    {
        auto strum = hardStrum(rate, 1.0f, touch, acustra::PickingTechnique::Pick,
                               1.0, 0, 0.0f, overdrive);
        bool inputKneesHidden = true;
        for (std::size_t i = 0; i < strum.drive.size(); ++i)
            inputKneesHidden = inputKneesHidden && std::isfinite(strum.freeDrive[i])
                && static_cast<float>(std::clamp(strum.freeDrive[i],
                    Design::railLow, Design::railHigh))
                    == std::clamp(strum.drive[i], static_cast<float>(Design::railLow),
                                  static_cast<float>(Design::railHigh));
        auto& drive = strum.freeDrive;
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
        const std::string label = "at " + std::to_string(rate) + ", driven " + std::to_string(overdrive)
            + ", Touch " + std::to_string(touch);
        expect(inputKneesHidden,
               "U1A input knees are not hidden by U1B's clipped interval, " + label);
        expect(clipped > 0, "the Pick at the bridge driven harder does not clip the piezo preamp, "
                                + label);
        // Keep the same -60 dB limit at every drive. Improvement over bare
        // clipping varies with the few samples that cross a rail, so the
        // separate 1.5 dB improvement guard uses the default-Touch mean.
        expect(aliasDb < -60.0,
               "piezo clip aliasing on an overdriven strum is above -60 dB, " + label);
        chainSum += aliasDb;
        bareSum += bareDb;
        ++drives;
        std::cout << "Piezo clip aliasing, Pick at velocity 127 at the bridge, " << label << ": "
                  << aliasDb << " dB, below 0.25 fs " << lowDb << " dB; a bare clip "
                  << bareDb << " dB (" << clipped / static_cast<long>(factor)
                  << " host samples past the swing)\n";
    }
    const double chainMean = chainSum / drives, bareMean = bareSum / drives;
    std::cout << "Piezo clip aliasing at " << rate << ", Touch " << touch
              << ", mean over the drives: " << chainMean
              << " dB, a bare clip " << bareMean << " dB\n";
    if (touch == 0.58f)
        expect(chainMean < bareMean - 1.5,
               "piezo clip aliasing is not 1.5 dB under a bare clip over the drives, at "
                   + std::to_string(rate));
    }
}


// 11. The room (EngineParameters::room) is an observation at the
// microphones too: it adds to them, and never reaches the piezo, whether
// Main plays the piezo or the separate output takes it.
void testRoomReachesOnlyTheMicrophones()
{
    const auto play = [] (acustra::CaptureType capture, float room,
                          std::vector<float>& main, std::vector<float>& piezo)
    {
        acustra::EngineParameters parameters;
        parameters.capture = capture;
        parameters.room = room;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(48000, 64);
        for (int note : { 40, 47, 52, 56, 59, 64 })
            engine->noteOn(note, 0.8f);
        std::array<float, 64> left {}, right {}, under {};
        for (int block = 0; block < 1500; ++block)
        {
            if (block == 750)
                for (int note : { 40, 47, 52, 56, 59, 64 })
                    engine->noteOff(note);
            engine->process(left.data(), right.data(),
                            acustra::AcustraEngine::OutputBuses { under.data() }, 64);
            main.insert(main.end(), left.begin(), left.end());
            main.insert(main.end(), right.begin(), right.end());
            piezo.insert(piezo.end(), under.begin(), under.end());
        }
    };
    for (auto capture : { acustra::CaptureType::StereoMic, acustra::CaptureType::MonoMic,
                          acustra::CaptureType::Piezo })
    {
        std::vector<float> dryMain, dryPiezo, roomMain, roomPiezo;
        play(capture, 0.0f, dryMain, dryPiezo);
        play(capture, 0.8f, roomMain, roomPiezo);
        expect(dryPiezo == roomPiezo, "the room reached the separate Piezo output");
        if (capture == acustra::CaptureType::Piezo)
        {
            expect(dryMain == roomMain, "the room reached Main with Capture on Piezo");
            continue;
        }
        double direct = 0.0, added = 0.0;
        for (std::size_t index = 0; index < dryMain.size(); ++index)
        {
            const double difference = static_cast<double>(roomMain[index]) - dryMain[index];
            direct += static_cast<double>(dryMain[index]) * dryMain[index];
            added += difference * difference;
        }
        expect(added > 0.0, "the room did not reach a microphone capture");
        // 0.8 sends 6.8 dB more than the 0.5 the plug-in starts at.
        const double ratio = 10.0 * std::log10(direct / std::max(added, 1.0e-30));
        expect(ratio > 0.0 && ratio < 8.0,
               "the room at 0.8 sat " + std::to_string(ratio) + " dB under the guitar");
    }
}

// Output must scale an already-sounding room with its dry microphone. Compare
// an automated render with a constant-gain observation of the same instrument,
// using the engine's applied gain and accounting for the microphone pipeline.
// Starting muted also checks that opening Output reveals a warm room, while
// Piezo Mix and the separate output keep the piezo's own gain timing.
void testOutputGainScalesTheWholeCapture()
{
    using namespace acustra;
    constexpr float referenceGain = .03f;
    constexpr float piezoMix = .4f;
    constexpr int latency = AcustraEngine::outputLatencySamples();
    double worstRelativeError = 0.0;
    for (const int rate : { 44100, 96000 })
        for (const auto model : { GuitarModel::Original, GuitarModel::Bellido1978 })
            for (const auto capture : { CaptureType::StereoMic, CaptureType::MonoMic,
                                        CaptureType::Piezo })
                for (const bool initiallyMuted : { false, true })
                {
                    EngineParameters p;
                    p.guitarModel = model;
                    p.capture = capture;
                    p.room = .8f;
                    p.piezoMix = piezoMix;
                    p.outputGain = referenceGain;
                    auto reference = std::make_unique<AcustraEngine>();
                    reference->setParameters(p);
                    reference->prepare(rate, 64);
                    p.outputGain = initiallyMuted ? 0.0f : referenceGain;
                    auto moved = std::make_unique<AcustraEngine>();
                    moved->setParameters(p);
                    moved->prepare(rate, 64);
                    for (auto* engine : { reference.get(), moved.get() })
                        for (const int note : { 40, 47, 55, 64 })
                            engine->noteOn(note, .65f);
                    std::array<float, latency + 1> gains;
                    gains.fill(p.outputGain);
                    double peak = 0.0, error = 0.0;
                    for (int n = 0; n < rate / 3; ++n)
                    {
                        if (n == rate / 12 || n == rate / 6 || n == rate / 4)
                        {
                            p.outputGain = n == rate / 12
                                ? (initiallyMuted ? .06f : 0.0f)
                                : n == rate / 6 ? .015f : .04f;
                            moved->setParameters(p);
                        }
                        float rl, rr, rp, ml, mr, mp;
                        reference->process(&rl, &rr, AcustraEngine::OutputBuses { &rp }, 1);
                        moved->process(&ml, &mr, AcustraEngine::OutputBuses { &mp }, 1);
                        gains[static_cast<std::size_t>(n % gains.size())]
                            = Access::outputGain(*moved);
                        const double current = double(Access::outputGain(*moved)) / referenceGain;
                        const int delayed = n >= latency ? n - latency : 0;
                        const double held = double(gains[static_cast<std::size_t>(
                            delayed % gains.size())]) / referenceGain;
                        for (const auto samples : { std::array<double, 2> { rl, ml },
                                                    std::array<double, 2> { rr, mr } })
                        {
                            const double expected = capture == CaptureType::Piezo
                                ? samples[0] * current
                                : (samples[0] - piezoMix * double(rp)) * held
                                    + piezoMix * double(rp) * current;
                            peak = std::max(peak, std::abs(expected));
                            error = std::max(error, std::abs(samples[1] - expected));
                        }
                        peak = std::max(peak, std::abs(double(rp) * current));
                        error = std::max(error, std::abs(double(mp) - double(rp) * current));
                    }
                    expect(peak > 1.0e-6, "Output automation reference was silent");
                    const double relative = error / std::max(peak, 1.0e-30);
                    worstRelativeError = std::max(worstRelativeError, relative);
                    expect(relative < 2.0e-6,
                           "Output changed the wet/dry balance or misaligned Piezo Mix, model "
                               + std::to_string(static_cast<int>(model)) + ", capture "
                               + std::to_string(static_cast<int>(capture)) + ", rate "
                               + std::to_string(rate) + ", initially muted "
                               + std::to_string(initiallyMuted));
                }
    std::cout << "Output automation whole-capture relative peak error: "
              << worstRelativeError << '\n';
}

// 12. The room at the plug-in's 50% sits 11-14 dB under a held chord (about
// 10 under a released phrase, whose own sound stops before its room's), and
// decays in about 0.45 s, at every sample rate: it runs at the host rate
// divided down to 64 kHz or under and is energy-normalised at prepare.
void testRoomLevelAndDecayAcrossRates()
{
    std::vector<double> levels;
    for (int rate : { 44100, 48000, 96000, 192000 })
    {
        acustra::EngineParameters parameters;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(rate, 64);
        const auto impulse = Access::roomImpulse(*engine, static_cast<int>(1.5 * rate));
        // Broadband energy decay (Schroeder integration), -5 to -25 dB.
        std::vector<double> remaining(impulse.size() / 2);
        double sum = 0.0;
        for (std::size_t index = remaining.size(); index-- > 0;)
        {
            const double left = impulse[2 * index], right = impulse[2 * index + 1];
            sum += left * left + right * right;
            remaining[index] = sum;
        }
        const auto crossing = [&] (double db)
        {
            for (std::size_t index = 0; index < remaining.size(); ++index)
                if (10.0 * std::log10(remaining[index] / remaining[0]) < db)
                    return static_cast<double>(index) / rate;
            return 0.0;
        };
        const double rt60 = 3.0 * (crossing(-25.0) - crossing(-5.0));
        expect(rt60 > 0.35 && rt60 < 0.55,
               "the room decayed in " + std::to_string(rt60) + " s at " + std::to_string(rate));
        if (rate == 192000)
            continue;
        // The guitar against its room at 0.5, on the same chord.
        double direct = 0.0, added = 0.0;
        acustra::EngineParameters roomy;
        roomy.room = 0.5f;
        const auto dry = render(parameters, rate);
        const auto wet = render(roomy, rate);
        for (std::size_t index = 0; index < dry.left.size(); ++index)
            for (const auto& [a, b] : { std::pair { dry.left[index], wet.left[index] },
                                        std::pair { dry.right[index], wet.right[index] } })
            {
                direct += static_cast<double>(a) * a;
                added += (static_cast<double>(b) - a) * (static_cast<double>(b) - a);
            }
        levels.push_back(10.0 * std::log10(direct / std::max(added, 1.0e-30)));
        std::cout << "Room at " << rate << " Hz: RT60 " << rt60 << " s, the guitar "
                  << levels.back() << " dB over its room at 0.5\n";
    }
    for (double level : levels)
        expect(level > 11.0 && level < 14.0 && std::abs(level - levels.front()) < 0.5,
               "the room at 0.5 sat " + std::to_string(level) + " dB under the guitar");
}

// 13. The room renders the same at any block size, follows Width (zero is
// mono), and once it has rung out, a room returned to zero leaves Main bit
// for bit as an instrument that never had one.
void testRoomBlocksWidthAndReturn()
{
    const auto phrase = [] (acustra::EngineParameters parameters, int block,
                            float laterRoom)
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(48000, block);
        std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
        Audio out;
        constexpr int second = 48000;
        constexpr std::array<int, 3> edges { second, 2 * second, 6 * second };
        const int total = 8 * second;
        for (int done = 0; done < total;)
        {
            if (done == 0)
                for (int note : { 45, 52, 57, 61 })
                    engine->noteOn(note, 0.8f);
            if (done == edges[0])
                for (int note : { 45, 52, 57, 61 })
                    engine->noteOff(note);
            if (done == edges[1])
            {
                parameters.room = laterRoom;
                engine->setParameters(parameters);
            }
            if (done == edges[2])
                engine->noteOn(64, 0.7f);
            int count = std::min(block, total - done);
            for (int edge : edges)
                if (done < edge && done + count > edge)
                    count = edge - done;
            engine->process(left.data(), right.data(), count);
            out.left.insert(out.left.end(), left.begin(), left.begin() + count);
            out.right.insert(out.right.end(), right.begin(), right.begin() + count);
            done += count;
        }
        return out;
    };
    acustra::EngineParameters roomy;
    roomy.room = 0.6f;
    const auto base = phrase(roomy, 256, 0.6f);
    for (int block : { 1, 37 })
    {
        const auto other = phrase(roomy, block, 0.6f);
        expect(other.left == base.left && other.right == base.right,
               "the room rendered differently at block size " + std::to_string(block));
    }
    // Returned to zero at 2 s, the room has rung out by the next note at 6 s.
    const auto dry = phrase(acustra::EngineParameters {}, 256, 0.0f);
    const auto returned = phrase(roomy, 256, 0.0f);
    const auto from = static_cast<std::ptrdiff_t>(48000 * 6);
    expect(std::equal(dry.left.begin() + from, dry.left.end(), returned.left.begin() + from)
               && std::equal(dry.right.begin() + from, dry.right.end(),
                             returned.right.begin() + from),
           "a room returned to zero changed the next note");
    acustra::EngineParameters mono = roomy;
    mono.stereoWidth = 0.0f;
    const auto narrow = phrase(mono, 256, 0.6f);
    expect(narrow.left == narrow.right, "Width 0 left the room in stereo");
}

// 13b. The room rings out to exact silence: a released chord reaches exact
// zero on Main at most 2.5 s after the same chord without a room does (the
// strings' own residue takes longer than the room's tail).
void testRoomRingsOutToSilence()
{
    const auto lastSound = [] (float room)
    {
        acustra::EngineParameters parameters;
        parameters.room = room;
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(48000, 256);
        std::array<float, 256> left {}, right {};
        long last = -1;
        for (long block = 0; block < 48000L * 30 / 256; ++block)
        {
            if (block == 0)
                for (int note : { 45, 52, 57, 61 })
                    engine->noteOn(note, 0.8f);
            if (block == 48000 / 256)
                for (int note : { 45, 52, 57, 61 })
                    engine->noteOff(note);
            engine->process(left.data(), right.data(), 256);
            for (std::size_t index = 0; index < left.size(); ++index)
                if (left[index] != 0.0f || right[index] != 0.0f)
                    last = block * 256 + static_cast<long>(index);
        }
        return static_cast<double>(last + 1) / 48000.0;
    };
    const double dry = lastSound(0.0f);
    const double roomy = lastSound(0.6f);
    std::cout << "Exact silence after a released chord: " << dry << " s dry, " << roomy
              << " s with the room at 0.6\n";
    expect(dry < 29.0 && roomy < 29.0, "the instrument did not reach exact silence");
    expect(roomy >= dry && roomy - dry < 2.5,
           "the room's tail outlasted the instrument by " + std::to_string(roomy - dry) + " s");
}

// 14. The Original's capture voicing (CaptureVoicingData.h) is one smooth
// gain: within 9 dB of its level from 40 Hz to 20 kHz, moving no more than
// 2.5 dB in a twelfth of an octave, and making no claim above 4 kHz, where
// the recordings it was fitted to disagree (within 1.5 dB of its level).
void testCaptureVoicingIsSmoothAndBounded()
{
    const double level = 20.0 * std::log10(Access::voicing(20000.0f));
    double previous = 20.0 * std::log10(Access::voicing(40.0f));
    double worstStep = 0.0, worstSpan = 0.0, worstTop = 0.0;
    for (double frequency = 40.0 * std::exp2(1.0 / 12.0); frequency <= 20000.0;
         frequency *= std::exp2(1.0 / 12.0))
    {
        const double db = 20.0 * std::log10(Access::voicing(static_cast<float>(frequency)));
        worstStep = std::max(worstStep, std::abs(db - previous));
        worstSpan = std::max(worstSpan, std::abs(db - level));
        if (frequency >= 4000.0)
            worstTop = std::max(worstTop, std::abs(db - level));
        previous = db;
    }
    std::cout << "Capture voicing: within " << worstSpan << " dB of its level, steepest "
              << worstStep << " dB per twelfth octave, " << worstTop
              << " dB above 4 kHz\n";
    expect(worstSpan < 9.0, "the capture voicing moved a band more than 9 dB");
    expect(worstStep < 2.5, "the capture voicing is not smooth");
    expect(worstTop < 1.5, "the capture voicing reaches above 4 kHz");
}

// Independent analog prototypes, bilinear transformed at each evaluation
// frequency. Checking complex pressure catches a pole-frequency residue gain
// substitution even when its modal peaks happen to have the desired levels.
std::complex<double> bellidoMicrophoneTransfer(double frequency, double rate)
{
    std::complex<double> response = 1.0;
    for (const auto section : { std::array<double, 2> { 500.0, -3.0 },
                                std::array<double, 2> { 1400.0, 0.0 } })
    {
        const double a = std::pow(10.0, section[1] / 40.0);
        const std::complex<double> s(0.0,
            std::tan(pi * frequency / rate) / std::tan(pi * section[0] / rate));
        response *= (s * s + a * s / 1.2 + 1.0)
            / (s * s + s / (a * 1.2) + 1.0);
    }
    return response;
}

void testMicrophoneFilterDigitalTransfer()
{
    using namespace acustra;
    for (const int rate : { 24000, 44100, 48000, 96000, 192000 })
    {
        EngineParameters p;
        p.guitarModel = GuitarModel::Bellido1978;
        auto engine = std::make_unique<AcustraEngine>();
        engine->setParameters(p);
        engine->prepare(rate, 64);
        auto filter = Access::captureFilter(*engine);
        const int length = rate / 20;
        std::vector<float> left(static_cast<std::size_t>(length));
        std::vector<float> right(left.size());
        for (int n = 0; n < length; ++n)
        {
            const auto out = Access::filterSample(filter, n == 0 ? .25f : 0.0f,
                                                  n == 17 ? -.125f : 0.0f);
            left[static_cast<std::size_t>(n)] = out[0];
            right[static_cast<std::size_t>(n)] = out[1];
        }
        double worst = 0.0;
        for (const double frequency : { 0.0, 80.0, 200.0, 392.0, 500.0, 800.0,
                                        1400.0, 2800.0, 5000.0, .45 * rate })
        {
            const auto expected = bellidoMicrophoneTransfer(frequency, rate);
            const auto actualLeft = responseAt(left, frequency, rate) / .25;
            const auto actualRight = responseAt(right, frequency, rate)
                / (-.125 * std::polar(1.0, -2.0 * pi * frequency * 17.0 / rate));
            worst = std::max({ worst, std::abs(actualLeft - expected) / std::abs(expected),
                                     std::abs(actualRight - expected) / std::abs(expected) });
        }
        std::cout << "Bellido microphone digital transfer " << rate
                  << " Hz: relative complex error " << worst << '\n';
        expect(worst < .0005,
               "summed microphone pressure differs from the independent digital peak cascade");
    }
}

void testMicrophoneFilterBypassAndLifecycle()
{
    using namespace acustra;
    for (const int rate : { 24000, 44100, 48000, 96000, 192000 })
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        auto bypass = Access::captureFilter(*engine);
        expect(bypass.enabled, "Original did not configure its microphone filter");
        bypass.enabled = false; // Explicit diagnostic bypass preserves sample bits.
        bool sameBits = true;
        for (int n = 0; n < 2048; ++n)
        {
            const float left = static_cast<float>(std::sin(n * .371) * .2);
            const float right = static_cast<float>(std::cos(n * .173) * .13);
            const auto out = Access::filterSample(bypass, left, right);
            sameBits = sameBits && out[0] == left && out[1] == right;
        }
        expect(sameBits, "Disabled capture filter changed microphone sample bits");
        expect(Access::filterHistory(bypass) == decltype(Access::filterHistory(bypass)) {},
               "Disabled capture filter advanced an unnecessary history");

        EngineParameters p;
        p.guitarModel = GuitarModel::Bellido1978;
        engine->setParameters(p);
        engine->reset();
        auto filter = Access::captureFilter(*engine);
        auto fresh = filter;
        expect(filter.enabled, "Bellido did not configure its microphone filter");
        for (int n = 0; n < rate / 100; ++n)
            Access::filterSample(filter, static_cast<float>(std::sin(n * .19)),
                                static_cast<float>(std::cos(n * .071)));
        expect(filter.stateLeft != fresh.stateLeft && filter.stateRight != fresh.stateRight,
               "microphone filter lifecycle probe did not establish both histories");
        const auto hotFilter = filter;
        filter.configure(rate, true);
        expect(filter.coefficients == hotFilter.coefficients
                   && filter.stateLeft == hotFilter.stateLeft
                   && filter.stateRight == hotFilter.stateRight,
               "unchanged filter configuration altered its coefficients or histories");
        filter.reset();
        bool sameReset = true;
        for (int n = 0; n < 128; ++n)
            sameReset = sameReset && Access::filterSample(filter, n == 0 ? .2f : 0.0f,
                n == 1 ? -.13f : 0.0f) == Access::filterSample(fresh, n == 0 ? .2f : 0.0f,
                n == 1 ? -.13f : 0.0f);
        expect(sameReset, "filter reset did not recover the cold two-channel impulse response");

        for (int n = 0; n < rate / 100; ++n)
            Access::body(*engine, static_cast<float>(std::sin(n * .13)), .27f);
        const auto hot = Access::filterStates(*engine);
        expect(std::any_of(hot.begin(), hot.end(), [] (float value) { return value != 0.0f; }),
               "body filter clear probe did not establish a history");
        engine->allSoundOff();
        expect(Access::filterStates(*engine) == decltype(Access::filterStates(*engine)) {},
               "all sound off retained a microphone filter history");
        for (int n = 0; n < 64; ++n)
        {
            const auto out = Access::body(*engine, 0.0f, 0.0f);
            expect(out.left == 0.0f && out.right == 0.0f && out.upper == 0.0f,
                   "cleared microphone filter emitted a tail");
        }
        Access::body(*engine, .4f, -.2f);
        engine->prepare(44100, 64);
        auto cold = std::make_unique<AcustraEngine>();
        cold->setParameters(p);
        cold->prepare(44100, 64);
        expect(Access::filterStates(*engine) == decltype(Access::filterStates(*engine)) {},
               "prepare retained a microphone filter history from the previous rate");
        bool samePrepared = true;
        for (int n = 0; n < 128; ++n)
        {
            const auto actual = Access::body(*engine, n == 0 ? .2f : 0.0f, 0.0f);
            const auto reference = Access::body(*cold, n == 0 ? .2f : 0.0f, 0.0f);
            samePrepared = samePrepared && actual.left == reference.left
                && actual.right == reference.right && actual.upper == reference.upper;
        }
        expect(samePrepared, "prepare reused stale capture-filter coefficients or history");
    }
}

// A bank's microphone filter must follow that bank through a model fade.
// Independent copies of the sounding banks retain their pre-switch histories;
// the delivered fade must be their blend, including an interrupted request.
void testMicrophoneFilterModelTransitions()
{
    using namespace acustra;
    for (const int rate : { 44100, 48000, 96000 })
    {
        EngineParameters p;
        p.guitarModel = GuitarModel::Bellido1978;
        auto engine = std::make_unique<AcustraEngine>();
        auto held = std::make_unique<AcustraEngine>();
        for (auto* target : { engine.get(), held.get() })
        {
            target->setParameters(p);
            target->prepare(rate, 64);
            for (int n = 0; n < rate / 100; ++n)
                Access::body(*target, static_cast<float>(std::sin(n * .23)), .13f);
        }
        const auto before = Access::captureFilter(*engine);
        auto original = p;
        original.guitarModel = GuitarModel::Original;
        engine->setParameters(original);
        expect(Access::fadingFilterStates(*engine)
                   == Access::filterHistory(before),
               "model fade did not copy the sounding microphone filter history");
        engine->setParameters(p); // Cancel before the first target sample.
        bool restored = true;
        for (int n = 0; n < 256; ++n)
        {
            const float force = static_cast<float>(std::cos(n * .17));
            const auto actual = Access::body(*engine, force, -.21f);
            const auto reference = Access::body(*held, force, -.21f);
            restored = restored && actual.left == reference.left
                && actual.right == reference.right && actual.upper == reference.upper;
        }
        expect(restored, "same-tick model cancellation lost the sounding filter history");

        const auto beforeWood = Access::captureFilter(*engine);
        auto maple = p;
        maple.bodyMaterial = BodyMaterial::Maple;
        engine->setParameters(maple);
        const auto afterWood = Access::captureFilter(*engine);
        expect(afterWood.coefficients == beforeWood.coefficients
                   && afterWood.stateLeft == beforeWood.stateLeft
                   && afterWood.stateRight == beforeWood.stateRight,
               "same-model Wood reconfiguration reset the microphone filter history");
        engine->setParameters(p); // Restore the sounding bank before advancing.

        auto oldBank = Access::bodyBank(*engine);
        engine->setParameters(original);
        auto nextBank = Access::bodyBank(*engine);
        bool sameFade = true;
        int samples = 0;
        while (Access::bodyFade(*engine) < 1.0f && samples < rate / 20)
        {
            if (samples == 17)
            {
                engine->setParameters(p);
                expect(Access::bodyPending(*engine), "interrupted filter fade was not queued");
            }
            const float force = static_cast<float>(std::sin(samples * .151));
            const float moment = static_cast<float>(std::cos(samples * .097) * .17);
            const auto old = oldBank.render(force, moment);
            const auto next = nextBank.render(force, moment);
            const float mix = Access::bodyFade(*engine);
            const auto actual = Access::body(*engine, force, moment);
            sameFade = sameFade && actual.left == old.left + mix * (next.left - old.left)
                && actual.right == old.right + mix * (next.right - old.right)
                && actual.upper == old.upper + mix * (next.upper - old.upper);
            ++samples;
            if (samples > 17 && !Access::bodyPending(*engine))
                break; // The first fade ended and the queued Bellido began.
        }
        expect(samples > 17 && samples < rate / 20 && sameFade,
               "queued model update interrupted a sounding bank's filtered waveform");
        expect(Access::hasBodyModel(*engine, GuitarModel::Bellido1978)
                   && Access::bodyFade(*engine) == 0.0f,
               "queued filter model did not start at the existing fade boundary");
        engine->reset();
        held->reset();
        expect(Access::filterStates(*engine) == decltype(Access::filterStates(*engine)) {},
               "reset retained active or fading microphone filter histories");
        bool resetMatches = true;
        for (int n = 0; n < 128; ++n)
        {
            const auto actual = Access::body(*engine, n == 0 ? .4f : 0.0f, 0.0f);
            const auto reference = Access::body(*held, n == 0 ? .4f : 0.0f, 0.0f);
            resetMatches = resetMatches && actual.left == reference.left
                && actual.right == reference.right && actual.upper == reference.upper;
        }
        expect(resetMatches, "reset after an interrupted model fade retained stale filter output");
    }
}

void testMicrophoneFilterOnlyChangesObservation()
{
    using namespace acustra;
    for (const int rate : { 44100, 96000 })
        for (const auto model : { GuitarModel::Original, GuitarModel::Bellido1978 })
        for (const auto capture : { CaptureType::StereoMic, CaptureType::MonoMic, CaptureType::Piezo })
        {
            EngineParameters p;
            p.guitarModel = model;
            p.capture = capture;
            p.room = 0.0f;
            p.outputGain = .05f;
            auto filtered = std::make_unique<AcustraEngine>();
            auto bypass = std::make_unique<AcustraEngine>();
            for (auto* engine : { filtered.get(), bypass.get() })
            {
                engine->setParameters(p);
                engine->prepare(rate, 64);
            }
            Access::bypassCaptureFilters(*bypass);
            bool physicalSame = true, piezoSame = true, piezoMainSame = true;
            double difference = 0.0, signal = 0.0;
            for (int n = 0; n < rate / 5; ++n)
            {
                if (n == 0)
                    for (int note : { 47, 59, 67 })
                    {
                        filtered->noteOn(note, .7f);
                        bypass->noteOn(note, .7f);
                    }
                if (n == rate / 12)
                {
                    filtered->noteOff(59);
                    bypass->noteOff(59);
                }
                if (n == rate / 9)
                {
                    filtered->noteOn(67, .83f);
                    bypass->noteOn(67, .83f);
                }
                float fl, fr, fp, bl, br, bp;
                filtered->process(&fl, &fr, AcustraEngine::OutputBuses { &fp }, 1);
                bypass->process(&bl, &br, AcustraEngine::OutputBuses { &bp }, 1);
                physicalSame = physicalSame
                    && filtered->getLastBridgeVelocity() == bypass->getLastBridgeVelocity()
                    && filtered->getLastBridgeReactionForce() == bypass->getLastBridgeReactionForce()
                    && filtered->getLastBridgeBodyForce() == bypass->getLastBridgeBodyForce()
                    && filtered->getLastBridgeTailForce() == bypass->getLastBridgeTailForce()
                    && filtered->getLastBridgePower() == bypass->getLastBridgePower()
                    && filtered->getLastBridgeBodyPower() == bypass->getLastBridgeBodyPower()
                    && filtered->getLastBridgeTailPower() == bypass->getLastBridgeTailPower();
                piezoSame = piezoSame && fp == bp;
                piezoMainSame = piezoMainSame && fl == bl && fr == br;
                difference += (double(fl) - bl) * (double(fl) - bl)
                    + (double(fr) - br) * (double(fr) - br);
                signal += double(bl) * bl + double(br) * br;
            }
            expect(physicalSame && piezoSame && Access::chainState(*filtered) == Access::chainState(*bypass),
                   "microphone output filter changed the physical junction or loaded piezo history");
            expect(signal > 1e-12, "filter observation control was silent");
            if (capture == CaptureType::Piezo)
                expect(piezoMainSame, "microphone output filter reached Main with Piezo selected");
            else
                expect(difference > signal * 1e-6, "microphone output filter did not reach the microphones");
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
    testRoomReachesOnlyTheMicrophones();
    testOutputGainScalesTheWholeCapture();
    testRoomLevelAndDecayAcrossRates();
    testRoomBlocksWidthAndReturn();
    testRoomRingsOutToSilence();
    testCaptureVoicingIsSmoothAndBounded();
    testMicrophoneFilterDigitalTransfer();
    testMicrophoneFilterBypassAndLifecycle();
    testMicrophoneFilterModelTransitions();
    testMicrophoneFilterOnlyChangesObservation();
    if (failures == 0)
        std::cout << "All Acustra capture tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
