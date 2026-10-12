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

// A dip on the summed pressure at the construction's own air mode (A0),
// following each Shape's A0 as the fixed 125 Hz refinement above could not:
// the note at A0 stood 4-6 dB over its neighbours on every Shape (F#2/G2 on
// the Dreadnought, G#2/A2 on the Auditorium, whose A2 the user named as
// boom on 2026-10-09; Docs/studio-smoothness-2026-10-11.md), with the loss
// shared by both microphones and still there over the balanced observation
// blend (G2 +3.1, G#2 +5.5 on the Auditorium). The dip takes about 1 dB
// off that excess (G2 +3.1 to +2.0 dB on the Dreadnought, G#2 +5.5 to +4.6
// on the Auditorium) and moves E2, under the Dreadnought's A0, by 0.6 dB.
// An authored capture refinement, like the two above, preferred blind
// over main's engine on 2026-10-12 (Docs/decisions.md); the measured
// instrument is untouched. The piezo never passes through it.
inline constexpr float originalAirModeDipDb = -2.5f;
inline constexpr float originalAirModeDipQ = 2.5f;
// The dip takes no level restore: it costs the default cell's stereo phrase
// 0.24 LU (the calibration phrase set, Tools/CalibrateConstructionLoudness.py,
// -27.5164 to -27.7532 LUFS), inside the construction loudness contract,
// and a restore sized on that phrase overshot the shorter construction
// fixture's phrase by 0.2 LU. The dipped constructions are re-levelled to
// the default (the Parlor's Finger and Thumb cells half an LU over it,
// below).
inline constexpr float originalAirModeDipLevelDb = 0.0f;
// Every construction takes the dip, by the air mode's frequency: the
// Jumbo's sits at 84-91 Hz, the Dreadnought's at 94-101, the Auditorium's
// at 106-113 and the Parlor's at 121-128 as Spruce, Mahogany and Maple
// place it. The Parlor joined on 2026-10-12 (its B2 +5.7 to +4.6 dB over
// its neighbours): the dip costs its chord-and-strum fixture phrase 0.2 LU
// more than its calibration phrase, which its margin on the construction
// loudness fixture did not have, so its Finger and Thumb cells now sit
// 0.5 LU over the calibration target (CalibrateConstructionLoudness.py,
// CROSS_PHRASE_LIFT_LU) inside the calibration's own tolerance, and the
// fixture reads -2.57 LU on Parlor Spruce Finger. Nothing above 130 Hz is
// a guitar's air mode; the bound keeps a misread mode from dipping the
// low mids.
inline constexpr float originalAirModeDipUpperHz = 130.0f;

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
