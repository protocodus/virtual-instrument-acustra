// Authored refinements of the summed close-microphone observation.
// Kept separate from the generated recording fit so regenerating that fit
// cannot silently overwrite the listening-feedback decisions.
// See Docs/microphone-resonance-2026-10-09.md for native comparisons.
#pragma once

#include "CaptureVoicingData.h"

#include <algorithm>
#include <cmath>

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

// A small brightening of both microphones: 0.4375 dB/octave about 1 kHz,
// bounded to 60 Hz-10 kHz. On 2026-10-02 the listener asked each of the two
// guitar models then shipping to come about 25% closer to the other's
// brightness: this is a quarter of the 1.75 dB/octave gap measured between
// their matched note spectra (Docs/decisions.md, 2026-10-02). The second
// model was removed on 2026-10-10; the Original keeps the voicing chosen
// then. This is an artistic balance, not a refit of the measured guitar.
//
// configureBody applies this once, after building the radiation
// continuation, to both microphones' complex force/moment residues at each
// mode's frequency. The physical bridge, string calibration, poles and mode
// counts stay intact; the piezo pickup response is unaffected. Construction
// level trims are calibrated separately, and no processing is added to the
// audio loop.
inline constexpr float microphoneTiltDbPerOctave = 0.25f * 1.75f;
inline constexpr float microphoneTiltPivotHz = 1000.0f;

inline float microphoneTiltGain(float frequency) noexcept
{
    const float boundedFrequency = std::clamp(frequency, 60.0f, 10000.0f);
    const float db = microphoneTiltDbPerOctave
        * std::log2(boundedFrequency / microphoneTiltPivotHz);
    return std::pow(10.0f, db / 20.0f);
}

} // namespace acustra::detail
