// Authored refinements of the summed close-microphone observation.
// Kept separate from the generated recording fit so regenerating that fit
// cannot silently overwrite the listening-feedback decisions.
// See Docs/microphone-resonance-2026-10-09.md for native comparisons.
#pragma once

#include "CaptureVoicingData.h"

namespace acustra::detail
{

// Restore the default phrase's -24.0513 LUFS after its 1.21724 LU loss.
// This scalar is separate from the tonal refinement and pickup calibration.
inline constexpr float originalCaptureLevelAdjustmentDb = 1.2172f;
inline constexpr float originalBassShelfAdjustmentDb = -3.0f;
inline constexpr float originalAirPeakAdjustmentDb = -2.75f;

constexpr CaptureVoicingSection balancedOriginalCaptureSection(
    CaptureVoicingSection section) noexcept
{
    // The fitted bass shelf and 125 Hz boost reinforce the close microphone's
    // already dominant bass fundamental, especially on Auditorium A2.
    // Refine broad pressure balance, retaining the measured instrument.
    if (section.kind == CaptureVoicingKind::LowShelf && section.frequencyHz == 120.0f)
        section.gainDb += originalBassShelfAdjustmentDb;
    else if (section.kind == CaptureVoicingKind::Peak && section.frequencyHz == 125.0f)
        section.gainDb += originalAirPeakAdjustmentDb;
    return section;
}

inline constexpr CaptureVoicingSection classicalCaptureVoicingSections[] {
    // The broad -6 dB cut deepened the measured A4/B4 fundamental dips.
    // Retain a gentler body-band correction without lifting their upper
    // harmonics. The first +3 dB presence refinement still sounded banjo-like.
    { CaptureVoicingKind::Peak, 500.0f, -3.0f, 1.2f },
    { CaptureVoicingKind::Peak, 1400.0f, 0.0f, 1.2f },
};

} // namespace acustra::detail
