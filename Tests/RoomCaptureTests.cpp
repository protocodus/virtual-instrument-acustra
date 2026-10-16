// Physical room witnesses: individual reflected arrival times, full wet
// transfer on equal internal grids, and actual folded/image components.
// The colour measurement includes process(), not just the internal FDN.
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
    static auto room(double rate, bool directional = true, bool conversion = true)
    {
        auto result = std::make_unique<AcustraEngine::RoomAmbience>();
        result->prepare(rate, directional, conversion);
        return result;
    }
    static bool roomOptions(const AcustraEngine& engine, bool directional, bool conversion)
    {
        return engine.room_.directional == directional
            && engine.room_.rateConversion == conversion;
    }
};
}

namespace
{
constexpr double pi = 3.14159265358979323846;
bool passed = true;
void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        passed = false;
    }
}
using Response = std::array<std::complex<double>, 2>;

double magnitude(const Response& response)
{
    return std::sqrt(0.5 * (std::norm(response[0]) + std::norm(response[1])));
}

double db(double value) { return 20.0 * std::log10(std::max(value, 1.0e-30)); }

Response tone(double rate, double frequency, double observed,
              bool directional, bool conversion)
{
    auto room = acustra::AcustraEngineTestAccess::room(rate, directional, conversion);
    Response response {};
    const int settle = static_cast<int>(rate);
    const int count = static_cast<int>(rate / 4.0);
    for (int sample = 0; sample < settle + count; ++sample)
    {
        const float input = frequency == 0.0 ? 0.125f
            : static_cast<float>(0.125 * std::cos(2.0 * pi * frequency * sample / rate));
        float left, right;
        room->process(input, left, right);
        if (sample >= settle)
        {
            const auto projection = std::polar(1.0, -2.0 * pi * observed * sample / rate);
            response[0] += static_cast<double>(left) * projection;
            response[1] += static_cast<double>(right) * projection;
        }
    }
    for (auto& channel : response)
        channel *= (observed == 0.0 ? 1.0 : 2.0) / (0.125 * count);
    return response;
}

void geometry()
{
    auto room = acustra::AcustraEngineTestAccess::room(48000.0);
    const auto originalLeft = room->tapLeft;
    const auto originalRight = room->tapRight;
    std::array<std::array<double, 2>, 10> times {};
    for (std::size_t tap = 0; tap < times.size(); ++tap)
    {
        room->reset();
        room->tapLeft.fill(0.0f);
        room->tapRight.fill(0.0f);
        room->tapLeft[tap] = originalLeft[tap];
        room->tapRight[tap] = originalRight[tap];
        room->earlyCoefficient = room->outputScale = 1.0f;
        room->lateInput = 0.0f;
        std::array<double, 2> sum {}, moment {};
        for (int sample = 0; sample < 1200; ++sample)
        {
            room->step(sample == 0 ? 1.0f : 0.0f);
            sum[0] += room->currentLeft;
            sum[1] += room->currentRight;
            moment[0] += sample * static_cast<double>(room->currentLeft);
            moment[1] += sample * static_cast<double>(room->currentRight);
        }
        for (std::size_t channel = 0; channel < 2; ++channel)
            times[tap][channel] = moment[channel] / sum[channel] / 48000.0;
        expect(std::abs(times[tap][0] - times[tap][1]) <= 0.2 / 343.0 + 1.0e-9,
               "reflection exceeds the pair's physical 20 cm interaural delay bound");
        expect(std::abs(sum[0] - originalLeft[tap]) < 1.0e-8
               && std::abs(sum[1] - originalRight[tap]) < 1.0e-8,
               "fractional interpolation does not preserve reflection DC gain");
    }
    const double direct = std::hypot(0.1, 0.32);
    const double floorDistance = std::sqrt(0.1 * 0.1 + 0.32 * 0.32 + 1.9 * 1.9);
    expect(std::abs(times[0][0] - (floorDistance - direct) / 343.0) < 1.0e-9,
           "floor reflection is not the image's excess travel time");
    expect(std::abs(times[0][0] - times[0][1]) < 1.0e-10,
           "central reflection has a spurious stereo time difference");
    expect(times[4][0] < times[4][1] && times[8][1] < times[8][0],
           "left/right wall arrivals lost their physical time ordering");
    expect(originalLeft[4] > originalRight[4] && originalRight[8] > originalLeft[8],
           "side-wall inverse-distance/directivity cues have reversed");
    // Behind the parallel cardioids is their null, not a panning choice.
    expect(originalLeft[5] < originalLeft[1] * 0.001f,
           "rear arrival ignores receiver cardioid rejection");
    std::cout << "Room geometry: floor excess " << times[0][0] * 1000.0
              << " ms; left wall ITD " << (times[4][1] - times[4][0]) * 1000.0
              << " ms; right wall ITD " << (times[8][0] - times[8][1]) * 1000.0 << " ms\n";
}

void fullTransfer()
{
    double worstDb = 0.0, worstPhase = 0.0, legacyWitness = 0.0;
    // 48/96 and 64/384 kHz share exactly the same FDN grid. Compare complete
    // wet transfer, including converter phase and the compensated arrivals.
    for (bool directional : { false, true })
        for (const auto rates : { std::array<double, 2>{48000.0, 96000.0},
                                  std::array<double, 2>{64000.0, 384000.0} })
            for (double frequency : { 0.0, 1000.0, 4000.0, 8000.0, 12000.0, 16000.0 })
            {
                const auto reference = tone(rates[0], frequency, frequency, directional, true);
                const auto candidate = tone(rates[1], frequency, frequency, directional, true);
                worstDb = std::max(worstDb,
                    std::abs(db(magnitude(candidate) / magnitude(reference))));
                for (std::size_t channel = 0; channel < 2; ++channel)
                    worstPhase = std::max(worstPhase,
                        std::abs(std::arg(candidate[channel] / reference[channel])));
                if (!directional && rates[0] == 48000.0 && frequency == 12000.0)
                {
                    const auto legacy = tone(rates[1], frequency, frequency, false, false);
                    legacyWitness = db(magnitude(legacy) / magnitude(reference));
                }
            }
    expect(legacyWitness < -2.6 && legacyWitness > -2.8,
           "full wet legacy 12 kHz witness no longer reproduces the measured defect");
    expect(worstDb < 0.01, "rate conversion colours the full wet passband by over 0.01 dB");
    expect(worstPhase < 0.001, "converter group delay was not removed from room arrivals");
    std::cout << "Full wet transfer: legacy 96/48 at 12 kHz " << legacyWitness
              << " dB; corrected max " << worstDb << " dB, phase " << worstPhase << " rad\n";
}

void aliasesAndImages()
{
    double worstAlias = -300.0, worstImage = -300.0, worstStop = -300.0;
    for (double rate : { 96000.0, 384000.0 })
    {
        auto room = acustra::AcustraEngineTestAccess::room(rate);
        const double internal = rate / room->decimation;
        const double folded = internal / 2.0 - 4000.0;
        const auto low = tone(rate, folded, folded, true, true);
        const auto alias = tone(rate, internal - folded, folded, true, true);
        worstAlias = std::max(worstAlias, db(magnitude(alias) / magnitude(low)));
        const auto base = tone(rate, 8000.0, 8000.0, true, true);
        const auto image = tone(rate, 8000.0, internal - 8000.0, true, true);
        worstImage = std::max(worstImage, db(magnitude(image) / magnitude(base)));
    }
    // Stopband extends over the entire host band, including the highest
    // supported decimation factor; it is not a single convenient null.
    for (double rate : { 96000.0, 176400.0, 192000.0, 384000.0, 768000.0 })
    {
        const auto room = acustra::AcustraEngineTestAccess::room(rate);
        const double start = 0.5 / room->decimation;
        for (int bin = 0; bin <= 512; ++bin)
        {
            const double frequency = start + (0.5 - start) * bin / 512.0;
            std::complex<double> response {};
            for (int index = 0; index < room->converterLength; ++index)
                response += static_cast<double>(room->converterCoefficients[static_cast<std::size_t>(index)])
                    * std::polar(1.0, -2.0 * pi * frequency * index);
            worstStop = std::max(worstStop, db(std::abs(response)));
        }
    }
    expect(worstAlias < -70.0, "full wet alias rejection is below 70 dB");
    expect(worstImage < -70.0, "full wet image rejection is below 70 dB");
    expect(worstStop < -74.0, "anti-alias filter stopband is below the 74 dB numerical bound");
    std::cout << "Full wet alias " << worstAlias << " dB; image " << worstImage
              << " dB; anti-alias worst stopband " << worstStop << " dB\n";
}

void lifecycle()
{
    for (double rate : { 8000.0, 11025.0, 16000.0, 22050.0, 24000.0,
                         32000.0, 44100.0, 48000.0, 50000.0, 64000.0,
                         64001.0, 88200.0, 96000.0, 176400.0, 192000.0,
                         384000.0, 768000.0 })
    {
        auto room = acustra::AcustraEngineTestAccess::room(rate);
        float left = 1.0f, right = 1.0f;
        room->process(0.0f, left, right);
        expect(left == 0.0f && right == 0.0f && !room->active,
               "fresh room is not exact silence");
        for (int sample = 0; sample < static_cast<int>(5.0 * rate); ++sample)
        {
            room->process(sample == 0 ? 1.0f : 0.0f, left, right);
            if (!std::isfinite(left) || !std::isfinite(right))
            {
                expect(false, "nonfinite room at " + std::to_string(rate));
                break;
            }
            if (!room->active)
                break;
        }
        expect(!room->active && left == 0.0f && right == 0.0f,
               "room failed to flush exactly at " + std::to_string(rate));
        room->reset();
        std::vector<float> first;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int sample = 0; sample < 4096; ++sample)
            {
                room->process(static_cast<float>((sample % 13 - 6) * 0.01), left, right);
                if (pass == 0)
                {
                    first.push_back(left);
                    first.push_back(right);
                }
                else
                    expect(first[static_cast<std::size_t>(2 * sample)] == left
                           && first[static_cast<std::size_t>(2 * sample + 1)] == right,
                           "reset retained converter or reflection history");
            }
            room->reset();
        }
    }
}

std::vector<float> dry(bool directional, bool conversion)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.directionalRoom = directional;
    realism.roomRateConversion = conversion;
    engine->setPerformanceRealism(realism);
    acustra::EngineParameters parameters;
    parameters.room = 0.0f;
    engine->setParameters(parameters);
    engine->prepare(96000.0, 64);
    // Setup-time changes must also rebuild coefficients after prepare().
    realism.directionalRoom = !directional;
    realism.roomRateConversion = !conversion;
    engine->setPerformanceRealism(realism);
    expect(acustra::AcustraEngineTestAccess::roomOptions(*engine, !directional, !conversion),
           "setPerformanceRealism left the prepared room in its old mode");
    realism.directionalRoom = directional;
    realism.roomRateConversion = conversion;
    engine->setPerformanceRealism(realism);
    engine->noteOn(64, 0.8f);
    std::vector<float> result(8192), right(8192);
    for (int offset = 0; offset < 8192; offset += 64)
        engine->process(result.data() + offset, right.data() + offset, 64);
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

int main()
{
    geometry();
    fullTransfer();
    aliasesAndImages();
    lifecycle();
    const auto reference = dry(false, false);
    for (const auto flags : { std::array<bool, 2>{true, false},
                              std::array<bool, 2>{false, true},
                              std::array<bool, 2>{true, true} })
        expect(dry(flags[0], flags[1]) == reference,
               "Room 0 changed the dry guitar or its latency");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
