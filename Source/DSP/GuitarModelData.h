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

namespace acustra::detail
{
// Microphone sensitivity trims, not bridge gains or absolute SPL claims.
// The archives do not share microphone distance. An equal-weight eight-note
// velocity-100 sweep matches each named model to Original/Auditorium's RMS
// for its string material; rounded fixed values avoid a changing loudness
// normalizer. See Tools/CompareGuitarBodies.py and the body integration report.
inline constexpr std::array<float, 2> guitarMicrophoneTrims { 1.0f, 1.10f };

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
} // namespace acustra::detail
