// Offline named calibration input shared by recording comparison tools.
#pragma once

#include "DSP/FittedPhysicalData.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace acustra::offline
{
inline PhysicalCalibration readCalibration(const std::string& path)
{
    auto result = fittedPhysicalCalibration;
    const std::array fields {
        std::pair { "bodyFrequencyScale", &result.bodyFrequencyScale },
        std::pair { "bodyQScale", &result.bodyQScale },
        std::pair { "bridgeMobilityScale", &result.bridgeMobilityScale },
        std::pair { "residueTiltDbPerOctave", &result.residueTiltDbPerOctave },
        std::pair { "directGain", &result.directGain },
        std::pair { "nylon.fundamentalT60Scale", &result.nylon.fundamentalT60Scale },
        std::pair { "nylon.frequencyLossScale", &result.nylon.frequencyLossScale },
        std::pair { "nylon.apertureScale", &result.nylon.apertureScale },
        std::pair { "nylon.transientScale", &result.nylon.transientScale },
        std::pair { "nylon.pluckDistanceScale", &result.nylon.pluckDistanceScale },
        std::pair { "nylon.velocityBrightnessDepth", &result.nylon.velocityBrightnessDepth },
        std::pair { "steel.stiffnessScale", &result.steel.stiffnessScale },
        std::pair { "steel.fundamentalT60Scale", &result.steel.fundamentalT60Scale },
        std::pair { "steel.frequencyLossScale", &result.steel.frequencyLossScale },
        std::pair { "steel.apertureScale", &result.steel.apertureScale },
        std::pair { "steel.transientScale", &result.steel.transientScale },
        std::pair { "steel.pluckDistanceScale", &result.steel.pluckDistanceScale },
        std::pair { "steel.velocityBrightnessDepth", &result.steel.velocityBrightnessDepth },
        std::pair { "apertureRegisterExponent", &result.apertureRegisterExponent },
        std::pair { "lowBodyModeGain", &result.lowBodyModeGain },
        std::pair { "steelDisplacementScaleMetres", &result.steelDisplacementScaleMetres },
        std::pair { "steelFretT60Slope", &result.steelFretT60Slope },
        std::pair { "highLossCutoffScale", &result.highLossCutoffScale },
        std::pair { "bridgeConductanceFloor", &result.bridgeConductanceFloor },
        std::pair { "bridgeConductanceCornerHz", &result.bridgeConductanceCornerHz },
        std::pair { "bridgeTailLengthMetres", &result.bridgeTailLengthMetres },
        std::pair { "longitudinalGain", &result.longitudinalGain },
        std::pair { "longitudinalQ", &result.longitudinalQ },
        std::pair { "polarisationEndCorrectionMetres", &result.polarisationEndCorrectionMetres },
        std::pair { "pickReleaseVelocityShare", &result.pickReleaseVelocityShare },
        std::pair { "pickReleaseVelocityExponent", &result.pickReleaseVelocityExponent },
        std::pair { "pickTransientGain", &result.pickTransientGain }
    };
    // Match the engine's accepted domain. Reject an invalid experiment rather
    // than reporting a requested value that the engine silently clamps.
    constexpr std::array<std::pair<float, float>, 32> limits {{
        { .96f, 1.04f }, { .05f, 1.8f }, { .25f, 4.f }, { -6.f, 6.f }, { 0.f, .12f },
        { .4f, 2.f }, { .35f, 3.f }, { .35f, 2.5f }, { 0.f, 3.f }, { .7f, 1.3f }, { 0.f, 1.2f },
        { .25f, 4.f }, { .4f, 2.f }, { .35f, 3.f }, { .35f, 2.5f }, { 0.f, 3.f }, { .7f, 1.3f }, { 0.f, 1.2f },
        { -1.f, 1.f }, { .25f, 32.f }, { 0.f, .04f }, { -.06f, .05f }, { .5f, 4.f },
        { 0.f, .02f }, { 100.f, 8000.f }, { .00325f, .060f }, { 0.f, .5f }, { 10.f, 400.f }, { 0.f, .00082f },
        { 0.f, 2.f }, { 0.f, 4.f }, { 0.f, 8.f }
    }};
    static_assert(fields.size() == limits.size());
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("calibration path is not a regular file");
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    if (!input)
        throw std::runtime_error("cannot open calibration file");
    std::set<std::string> seen;
    while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
    {
        std::string name;
        float value {};
        if (!(input >> name >> value) || !std::isfinite(value) || !seen.insert(name).second)
            throw std::runtime_error("invalid or repeated calibration field");
        bool found = false;
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const auto& [key, destination] = fields[index];
            if (name == key)
            {
                if (value < limits[index].first || value > limits[index].second)
                    throw std::runtime_error("out-of-range calibration field: " + name);
                *destination = value;
                found = true;
                break;
            }
        }
        if (!found)
            throw std::runtime_error("unknown calibration field: " + name);
    }
    if (input.bad())
        throw std::runtime_error("failed while reading calibration file");
    return result;
}
} // namespace acustra::offline
