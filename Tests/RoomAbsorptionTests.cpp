// The accepted room's 48 kHz late-field absorption is the reference. Check
// physical decay from the actual stored filter transfer functions, without
// reproducing the rate-mapping equation used to construct their poles.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto room(double rate)
    {
        auto result = std::make_unique<AcustraEngine::RoomAmbience>();
        result->prepare(rate);
        return result;
    }
};
} // namespace acustra

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

double magnitude(double gain, double pole, double frequency, double rate)
{
    return std::abs(gain / (1.0 - pole
        * std::exp(std::complex<double>(0.0, -2.0 * pi * frequency / rate))));
}

double decay(double gain, double pole, int length, double frequency, double rate)
{
    return -3.0 * std::log(10.0) * length
        / (rate * std::log(magnitude(gain, pole, frequency, rate)));
}
} // namespace

int main()
{
    // The old 48 kHz topology's prime-rounded line lengths. Derive its
    // original coefficients directly from the old DC/Nyquist definition.
    constexpr std::array<int, 8> referenceLengths {
        839, 947, 1109, 1277, 1439, 1619, 1783, 1987
    };
    std::array<float, 8> referenceGain {}, referencePole {};
    const auto reference = acustra::AcustraEngineTestAccess::room(48000.0);
    for (std::size_t line = 0; line < referenceLengths.size(); ++line)
    {
        const double low = std::pow(10.0,
            -3.0 * referenceLengths[line] / (0.45f * 48000.0));
        const double high = std::pow(10.0,
            -3.0 * referenceLengths[line] / (0.15f * 48000.0));
        const double pole = (low - high) / (low + high);
        referencePole[line] = static_cast<float>(pole);
        referenceGain[line] = static_cast<float>(low * (1.0 - pole));
        expect(reference->lengths[line] == referenceLengths[line]
                   && reference->absorptionPole[line] == referencePole[line]
                   && reference->absorptionGain[line] == referenceGain[line],
               "48 kHz reference line changed: " + std::to_string(line));
    }

    double worstLow = 0.0, worstHigh = 0.0;
    std::size_t checked = 0;
    // Include the 64 kHz decimation boundary, low supported rates, common
    // host rates and the maximum ordinary build rate. Room storage is fixed.
    for (double hostRate : { 8000.0, 11025.0, 16000.0, 22050.0, 24000.0,
                             32000.0, 44100.0, 48000.0, 50000.0, 64000.0,
                             64001.0, 88200.0, 96000.0, 176400.0,
                             192000.0, 384000.0 })
    {
        const auto room = acustra::AcustraEngineTestAccess::room(hostRate);
        const double rate = hostRate / room->decimation;
        const double frequency = std::min(8000.0, 0.25 * rate);
        for (std::size_t line = 0; line < referenceLengths.size(); ++line)
        {
            const double gain = room->absorptionGain[line];
            const double pole = room->absorptionPole[line];
            const double dc = magnitude(gain, pole, 0.0, rate);
            expect(std::isfinite(gain) && std::isfinite(pole)
                       && pole >= 0.0 && pole < 1.0 && dc > 0.0 && dc < 1.0,
                   "absorption is not passive at " + std::to_string(hostRate));
            const double low = decay(gain, pole, room->lengths[line], 0.0, rate);
            const double high = decay(gain, pole, room->lengths[line], frequency, rate);
            const double target = decay(referenceGain[line], referencePole[line],
                referenceLengths[line], frequency, 48000.0);
            worstLow = std::max(worstLow, std::abs(low / 0.45f - 1.0));
            worstHigh = std::max(worstHigh, std::abs(high / target - 1.0));
            ++checked;
        }
    }
    // Float coefficient storage sets the tolerance; the pre-fix 64 kHz
    // line decay differs by up to 18.6%, not a numerical rounding effect.
    expect(worstLow < 2.0e-6, "DC decay lost its 0.45 s physical target");
    expect(worstHigh < 2.0e-6, "HF decay moved from its 48 kHz reference");
    std::cout << "Room absorption: " << checked << " line/rate cases; max relative"
              << " DC error " << worstLow << ", HF error " << worstHigh << '\n';
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
