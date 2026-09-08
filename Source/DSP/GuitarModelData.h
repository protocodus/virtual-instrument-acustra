// Runtime adapters for separately fitted physical guitars. The source headers
// retain measurement provenance and fit qualifications; no coefficients are
// inferred from a manufacturer's name or from a numbered microphone capture.
#pragma once
#include "MeasuredBodyData.h"
#if defined(ACUSTRA_MEASURED_BRIDGE_DATA_HEADER)
#include ACUSTRA_MEASURED_BRIDGE_DATA_HEADER
#else
#include "MeasuredBridgeData.h"
#endif
#include "NylonG35CandidateData.h"
#include "RauQualifiedGuitarData.h"

namespace acustra::detail
{
// Microphone sensitivity trims, not bridge gains or absolute SPL claims.
// The archives do not share microphone distance. An equal-weight eight-note
// velocity-100 sweep matches each named model to Original/Auditorium's RMS
// for its string material; rounded fixed values avoid a changing loudness
// normalizer. See Tools/CompareGuitarBodies.py and the body integration report.
inline constexpr std::array<float, 5> guitarMicrophoneTrims { 1.0f, 1.10f, 3.15f, 3.65f, 3.24f };

template <std::size_t N>
constexpr auto adaptMonoRadiation(
    const std::array<experimental::RauRadiationMode, N>& source)
{
    std::array<MeasuredBodyMode, N> result {};
    for (std::size_t i = 0; i < N; ++i)
    {
        const auto& m = source[i];
        // One measured microphone is deliberately the same observation on
        // both channels. Neither a second microphone nor a torque response
        // can be recovered from this scalar measurement.
        result[i] = { m.frequency, m.q, m.real, m.imaginary,
            m.real, m.imaginary, m.real, m.imaginary, 0, 0, 0, 0, 0, 0 };
    }
    return result;
}

template <std::size_t N>
constexpr auto adaptNormalMobility(
    const std::array<experimental::RauBridgeMode, N>& source)
{
    std::array<MeasuredBridgeMode, N> result {};
    for (std::size_t i = 0; i < N; ++i)
        result[i] = { source[i].frequency, source[i].q,
                      source[i].residue, 0, 0 };
    return result;
}

inline constexpr auto bellidoBodyModes = [] {
    std::array<MeasuredBodyMode, experimental::nylonG35Radiation.size()> result {};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        const auto& m = experimental::nylonG35Radiation[i];
        result[i] = { m.frequency, m.q,
            m.trebleReal, m.trebleImaginary, m.bassReal, m.bassImaginary,
            m.upperReal, m.upperImaginary,
            m.trebleMomentReal, m.trebleMomentImaginary,
            m.bassMomentReal, m.bassMomentImaginary,
            m.upperMomentReal, m.upperMomentImaginary };
    }
    return result;
}();
inline constexpr auto bellidoBridgeModes = [] {
    std::array<MeasuredBridgeMode, experimental::nylonG35Mobility.size()> result {};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        const auto& m = experimental::nylonG35Mobility[i];
        result[i] = { m.frequency, m.q, m.heave, m.cross, m.rock };
    }
    return result;
}();
inline constexpr auto washburnBodyModes = adaptMonoRadiation(experimental::Washburn_1897_radiation);
inline constexpr auto washburnBridgeModes = adaptNormalMobility(experimental::Washburn_1897_mobility);
inline constexpr auto santaCruzBodyModes = adaptMonoRadiation(experimental::SCGC_OM3_radiation);
inline constexpr auto santaCruzBridgeModes = adaptNormalMobility(experimental::SCGC_OM3_mobility);
inline constexpr auto martinBodyModes = adaptMonoRadiation(experimental::Martin_D18_radiation);
inline constexpr auto martinBridgeModes = adaptNormalMobility(experimental::Martin_D18_mobility);
} // namespace acustra::detail
