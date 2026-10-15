// The wet-side refinement must leave the actual mono observation and dry
// instrument alone. Its response is checked against the continuous
// Butterworth magnitude, independently of the stored biquad coefficients.
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
    static auto room(double rate)
    {
        auto room = std::make_unique<AcustraEngine::RoomAmbience>();
        room->prepare(rate);
        return room;
    }
};
}

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
bool passed = true;
void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        passed = false;
    }
}

struct Render
{
    std::vector<float> left, right, piezo;
};

Render play(bool enabled, float room, float width, acustra::CaptureType capture,
            int blockSize = 64)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.roomBassCoherence = enabled;
    engine->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.room = room;
    parameters.stereoWidth = width;
    parameters.capture = capture;
    engine->setParameters(parameters);
    engine->prepare(48000.0, blockSize);
    for (int note : { 40, 47, 52, 56, 59, 64 })
        engine->noteOn(note, 0.8f);
    Render result;
    constexpr int count = 24000;
    result.left.resize(count);
    result.right.resize(count);
    result.piezo.resize(count);
    for (int offset = 0; offset < count; offset += blockSize)
    {
        const int frames = std::min(blockSize, count - offset);
        engine->process(result.left.data() + offset, result.right.data() + offset,
            acustra::AcustraEngine::OutputBuses { result.piezo.data() + offset }, frames);
    }
    return result;
}
}

int main()
{
    double worstDb = 0.0;
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0,
                         192000.0, 384000.0 })
    {
        auto room = acustra::AcustraEngineTestAccess::room(rate);
        for (double hz : { 60.0, 150.0, 1500.0 })
        {
            room->reset();
            std::complex<double> input {}, output {};
            const int settle = static_cast<int>(0.2 * rate);
            for (int sample = 0; sample < 2 * settle; ++sample)
            {
                const double phase = 2.0 * pi * hz * sample / rate;
                const float signal = static_cast<float>(std::sin(phase));
                const float filtered = room->coherentSide(signal);
                expect(std::isfinite(filtered), "nonfinite side at " + std::to_string(rate));
                if (sample >= settle)
                {
                    const auto projection = std::exp(std::complex<double>(0.0, -phase));
                    input += static_cast<double>(signal) * projection;
                    output += static_cast<double>(filtered) * projection;
                }
            }
            const double frequencyRatio = std::tan(pi * hz / rate)
                                        / std::tan(pi * 150.0 / rate);
            const double expected = frequencyRatio * frequencyRatio
                / std::sqrt(1.0 + std::pow(frequencyRatio, 4.0));
            const double error = std::abs(20.0 * std::log10(std::abs(output / input) / expected));
            worstDb = std::max(worstDb, error);
            expect(error < 0.002, "side response moved at " + std::to_string(rate)
                + " / " + std::to_string(hz));
        }
        room->reset();
        expect(room->coherentSide(0.0f) == 0.0f && !room->active,
               "reset side filter was not exactly silent");
    }

    auto room = acustra::AcustraEngineTestAccess::room(48000.0);
    float left = 0.0f, right = 0.0f, side = 0.0f;
    for (int sample = 0; sample < 4 * 48000; ++sample)
    {
        room->process(sample == 0 ? 1.0f : 0.0f, left, right);
        side = room->coherentSide(0.5f * (left - right));
    }
    expect(!room->active && left == 0.0f && right == 0.0f && side == 0.0f,
           "wet-side filter prevented the room reaching exact silence");
    expect(room->sideHighpassState == std::array<double, 2> {},
           "silent room retained a side-filter tail");

    using Capture = acustra::CaptureType;
    for (const auto capture : { Capture::StereoMic, Capture::MonoMic, Capture::Piezo })
        for (float amount : { 0.0f, 1.0f })
        {
            const auto before = play(false, amount, 1.0f, capture);
            const auto after = play(true, amount, 1.0f, capture);
            expect(before.piezo == after.piezo, "wet-side processing reached the Piezo bus");
            if (amount == 0.0f || capture != Capture::StereoMic)
                expect(before.left == after.left && before.right == after.right,
                       "dry/mono/Piezo capture changed with wet-side processing");
            else
            {
                double difference = 0.0, monoError = 0.0;
                for (std::size_t i = 0; i < before.left.size(); ++i)
                {
                    difference += std::abs(static_cast<double>(before.left[i]) - after.left[i]);
                    monoError = std::max(monoError, std::abs(
                        (static_cast<double>(before.left[i]) + before.right[i])
                        - (static_cast<double>(after.left[i]) + after.right[i])));
                    expect(std::isfinite(after.left[i]) && std::abs(after.left[i]) <= 1.0f
                               && std::isfinite(after.right[i]) && std::abs(after.right[i]) <= 1.0f,
                           "full-room/full-width output escaped its bounds");
                }
                expect(difference > 0.01, "wet-side refinement did not reach stereo sound");
                // The mid is unchanged; final separate float additions to
                // the dry channels can differ by one rounding step.
                expect(monoError < 1.0e-7, "stereo sum changed beyond float rounding");
            }
        }
    const auto monoBefore = play(false, 1.0f, 0.0f, Capture::StereoMic);
    const auto monoAfter = play(true, 1.0f, 0.0f, Capture::StereoMic);
    expect(monoBefore.left == monoAfter.left && monoBefore.right == monoAfter.right,
           "Width zero changed with wet-side processing");
    const auto oneBlock = play(true, 1.0f, 1.0f, Capture::StereoMic, 64);
    const auto oddBlock = play(true, 1.0f, 1.0f, Capture::StereoMic, 37);
    expect(oneBlock.left == oddBlock.left && oneBlock.right == oddBlock.right,
           "wet-side processing depended on the host block partition");
    std::cout << "Room bass coherence: worst response error " << worstDb << " dB\n";
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
