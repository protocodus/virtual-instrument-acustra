// Mark Rau's guitar measurements (https://rau.mit.edu/projects/GuitarMeasurements/)
// carry no explicit redistribution license, so the coefficients fitted from
// them are not published with this repository. This stub keeps the
// declarations the engine compiled against, with no modes; the three models
// it names are silent here. Tools/GenerateRauGuitarCandidates.py regenerates
// the full header locally from the source ZIP.
#pragma once
#include <array>
namespace acustra::experimental {
struct RauRadiationMode { float frequency, q, real, imaginary; };
struct RauBridgeMode { float frequency, q, residue; };
inline constexpr int Martin_D18_radiationDelaySamples = 0;
inline constexpr std::array<RauRadiationMode, 0> Martin_D18_radiation {};
inline constexpr std::array<RauBridgeMode, 0> Martin_D18_mobility {};
inline constexpr int SCGC_OM3_radiationDelaySamples = 0;
inline constexpr std::array<RauRadiationMode, 0> SCGC_OM3_radiation {};
inline constexpr std::array<RauBridgeMode, 0> SCGC_OM3_mobility {};
inline constexpr int Washburn_1897_radiationDelaySamples = 0;
inline constexpr std::array<RauRadiationMode, 0> Washburn_1897_radiation {};
inline constexpr std::array<RauBridgeMode, 0> Washburn_1897_mobility {};
} // namespace acustra::experimental
