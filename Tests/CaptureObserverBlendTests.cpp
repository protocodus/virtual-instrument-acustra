// The approved observer is a constant complex-pressure mixture, not an EQ
// or a change to the vibrating instrument. Its 48 kHz reference predates the
// production implementation; rate checks apply the independent hold mapping.
#include "DSP/AcustraEngine.h"

#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Observation
    {
        std::complex<double> pole;
        std::array<std::complex<double>, 4> residue;
    };
    static Observation observation(const AcustraEngine& engine, int shape, int wood, int slot)
    {
        const auto& mode = engine.bodyConfigurations_[3 * shape + wood].modes[slot];
        return { { mode.poleReal, mode.poleImaginary }, {{
            { mode.leftReal, mode.leftImaginary },
            { mode.rightReal, mode.rightImaginary },
            { mode.leftMomentReal, mode.leftMomentImaginary },
            { mode.rightMomentReal, mode.rightMomentImaginary }
        }} };
    }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Complex = std::complex<double>;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0;
void expect(bool okay, const std::string& message)
{
    if (!okay)
    {
        ++failures;
        if (failures <= 16) std::cerr << "FAIL: " << message << '\n';
    }
}
struct Reference
{
    int shape {}, wood {}, mode {};
    std::array<Complex, 4> residue {};
};
std::vector<Reference> readReference(const char* path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read approved observer fixture");
    std::vector<Reference> result;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#' || line.rfind("shape,", 0) == 0) continue;
        for (char& c : line) if (c == ',') c = ' ';
        std::istringstream values(line);
        Reference row;
        if (!(values >> row.shape >> row.wood >> row.mode))
            throw std::runtime_error("invalid observer fixture key");
        for (auto& residue : row.residue)
        {
            double real {}, imaginary {};
            if (!(values >> real >> imaginary) || !std::isfinite(real) || !std::isfinite(imaginary))
                throw std::runtime_error("invalid observer fixture residue");
            residue = {real, imaginary};
        }
        std::string extra;
        if (values >> extra || row.shape < 0 || row.shape > 3 || row.wood < 0 || row.wood > 2
            || row.mode < 0 || row.mode >= 160)
            throw std::runtime_error("invalid observer fixture record");
        result.push_back(row);
    }
    if (result.size() != 144) throw std::runtime_error("incomplete approved observer fixture");
    std::array<std::array<int, 3>, 4> counts {};
    std::set<std::array<int, 3>> keys;
    for (const auto& row : result)
    {
        if (!keys.insert({row.shape,row.wood,row.mode}).second)
            throw std::runtime_error("duplicate observer fixture mode");
        ++counts[row.shape][row.wood];
    }
    for (const auto& shape : counts)
        for (int count : shape)
            if (count != 12) throw std::runtime_error("fixture must cover all 12 constructions equally");
    return result;
}

double relativeError(Complex actual, Complex expected)
{
    return std::abs(actual - expected) / std::max(std::abs(expected), 1.0e-12);
}

void testApprovedObservation(const std::vector<Reference>& rows)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare(48000, 64);
    double worst = 0.0;
    for (const auto& row : rows)
    {
        const auto actual = Access::observation(*engine, row.shape, row.wood, row.mode);
        for (std::size_t axis = 0; axis < row.residue.size(); ++axis)
        {
            const double error = relativeError(actual.residue[axis], row.residue[axis]);
            worst = std::max(worst, error);
            expect(error < 2.0e-5, "approved force/moment microphone mixture differs at construction "
                + std::to_string(3 * row.shape + row.wood) + ", slot " + std::to_string(row.mode)
                + ", observation " + std::to_string(axis));
        }
    }
    std::cout << "approved observer: " << rows.size() * 4
              << " complex residues, max relative error " << worst << '\n';
}

void testRateMapping(const std::vector<Reference>& rows)
{
    auto reference = std::make_unique<Engine>();
    reference->prepare(48000, 64);
    auto engine = std::make_unique<Engine>();
    // Re-prepare the same object to catch a frozen/stale observer cache.
    for (double rate : {8000., 24000., 44100., 96000., 192000., 384000., 48000.})
    {
        engine->prepare(rate, 64);
        double worst = 0.0;
        int compared = 0, inactive = 0;
        for (const auto& row : rows)
        {
            // Continuation grid/count follows available bandwidth; it is not
            // the same set of poles at every rate. Its approved 48 kHz values
            // are checked above, without imposing a false all-rate golden.
            if (row.mode >= 141) continue;
            const auto base = Access::observation(*reference, row.shape, row.wood, row.mode);
            const auto actual = Access::observation(*engine, row.shape, row.wood, row.mode);
            if (actual.pole.imag() == 0.0)
            {
                for (const auto value : actual.residue)
                    expect(value == Complex {}, "inactive above-band observation is not zero");
                ++inactive;
                continue;
            }
            const double frequency = std::arg(base.pole) * 48000. / (2. * pi);
            const auto sinc = [frequency] (double fs)
            {
                const double x = pi * frequency / fs;
                return std::sin(x) / x;
            };
            // For a fixed continuous pole s, the held-input residue is
            // proportional to (exp(s/fs)-1)/s. Correct the reference hold's
            // sinc droop separately; the complex observer itself is fixed.
            const Complex scale = (actual.pole - Complex(1.0)) / (base.pole - Complex(1.0))
                * (sinc(48000.) / sinc(rate));
            for (std::size_t axis = 0; axis < row.residue.size(); ++axis)
            {
                const double error = relativeError(actual.residue[axis], row.residue[axis] * scale);
                worst = std::max(worst, error);
                expect(error < 2.0e-4, "observer changes with rate beyond the complex hold mapping");
                ++compared;
            }
        }
        std::cout << "observer rate " << rate << ": " << compared << " complex residues, "
                  << inactive << " inactive modes, max relative error " << worst << '\n';
    }
}

void testCalibrationDomain()
{
    // The four low spatial kernels were validated independently on the
    // full continuous-pole rectangle f=45..400 Hz, Q=.05..150. Cover the
    // actual sanitized frequency/Q extrema of every construction so a
    // future physical calibration cannot silently exceed that envelope.
    // Other calibration fields only scale residues/drive; they do not move
    // these pole arguments. The observation must remain finite there too.
    auto engine = std::make_unique<Engine>();
    double minimumFrequency = 1.0e30, maximumFrequency = 0.0;
    double minimumQ = 1.0e30, maximumQ = 0.0;
    int poles = 0;
    for (float frequencyScale : {.96f, 1.04f})
        for (float qScale : {.05f, 1.8f})
            for (float residueTilt : {-6.0f, 6.0f})
                for (float airGain : {.25f, 32.0f})
                {
                    auto calibration = acustra::fittedPhysicalCalibration;
                    calibration.bodyFrequencyScale = frequencyScale;
                    calibration.bodyQScale = qScale;
                    calibration.residueTiltDbPerOctave = residueTilt;
                    calibration.lowBodyModeGain = airGain;
                    engine->setPhysicalCalibration(calibration);
                    engine->prepare(48000, 64);
                    for (int shape = 0; shape < 4; ++shape)
                        for (int wood = 0; wood < 3; ++wood)
                            for (int slot : {0, 1, 133, 134})
                            {
                                const auto actual = Access::observation(*engine, shape, wood, slot);
                                const double frequency = std::arg(actual.pole) * 48000. / (2. * pi);
                                const double q = -pi * frequency / (48000. * std::log(std::abs(actual.pole)));
                                minimumFrequency = std::min(minimumFrequency, frequency);
                                maximumFrequency = std::max(maximumFrequency, frequency);
                                minimumQ = std::min(minimumQ, q);
                                maximumQ = std::max(maximumQ, q);
                                expect(std::isfinite(frequency) && frequency >= 45. && frequency <= 400.
                                           && std::isfinite(q) && q >= .05 && q <= 150.,
                                       "calibrated spatial pole left the validated Green-kernel domain");
                                for (const auto residue : actual.residue)
                                    expect(std::isfinite(residue.real()) && std::isfinite(residue.imag()),
                                           "calibration corner produced a nonfinite observer residue");
                                ++poles;
                            }
                }
    std::cout << "spatial calibration corners: " << poles << " poles, frequency "
              << minimumFrequency << ".." << maximumFrequency << " Hz, Q "
              << minimumQ << ".." << maximumQ << '\n';
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("expected approved fixture path");
        const auto rows = readReference(argv[1]);
        testApprovedObservation(rows);
        testRateMapping(rows);
        testCalibrationDomain();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
