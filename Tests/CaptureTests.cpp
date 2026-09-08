// Capture is a read-only observation of the instrument. Verify the microphone
// channel identity, loaded saddle-force observation, switching and MIDI
// silence; absolute piezo sensitivity requires recorded calibration pairs.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static float loadedPiezo(AcustraEngine& engine, float force)
    {
        return engine.renderLoadedPiezo(force);
    }
    static std::array<float, 2> loadedPiezoState(const AcustraEngine& engine)
    {
        return { engine.piezoLoadInput_, engine.piezoLoadOutput_ };
    }

};
} // namespace acustra

namespace
{
int failures = 0;
void expect(bool condition, const char* message)
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

void testLoadedPiezoStaysWarmWhileUnheard()
{
    using Access = acustra::AcustraEngineTestAccess;
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
                expect(Access::loadedPiezoState(*switched)
                           == Access::loadedPiezoState(*reference),
                       "unheard piezo loading lost its capacitor history");
                expect(Access::loadedPiezoState(*switched)[0]
                           == switched->getLastBridgeReactionForce(),
                       "piezo electrical load received a different saddle-force observable");
                expect(switched->getLastBridgeBodyForce()
                           == reference->getLastBridgeBodyForce(),
                       "piezo electrical observation changed the mechanical bridge");
            }
            expect(left == right && left == referenceLeft && right == referenceRight,
                   "switching to warmed piezo did not reach the continuously observed output");
        }
}

} // namespace

int main()
{
    testCaptureObservations();
    testCaptureLifecycle();
    testLoadedPiezoElectricalResponse();
    testLoadedPiezoStaysWarmWhileUnheard();
    if (failures == 0)
        std::cout << "All Acustra capture tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
