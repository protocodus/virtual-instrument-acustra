// Authored refinements of the summed close-microphone observation.
// Kept separate from the generated recording fit so regenerating that fit
// cannot silently overwrite the listening-feedback decisions.
// See Docs/research/microphone-resonance-2026-10-09.md for native comparisons.
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
// boom on 2026-10-09; Docs/research/studio-smoothness-2026-10-11.md), with the loss
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

// The mid-band dip (2026-10-12): the summed microphone response of every
// construction carries, between 450 and 800 Hz, a deep antiresonance where
// the measured g21 bank's same-sign residues cancel (the C#5 hole of
// Tests/BodyShapeTests: 36-40 dB under the response's mid-band peaks) and,
// three and a half semitones below it, the upper of the body's two strong
// mid modes (404-614 Hz as Shape and Wood place it). On the E2-E5 sweep at
// the plug-in's Room 0.5 (Tools/MeasureNoteEvenness.py) the note on that
// upper mode stood +11.4 dB over its neighbours on the Dreadnought (A#4,
// the mode at 460 Hz), +8.4 on the Auditorium (C5, 516 Hz) and +10.5 on
// the Jumbo (G#4, 419 Hz); on the Parlor the pair sits at 605/741 Hz,
// D5/D#5 +4.5/+5.4 with the hole above the sweep. The lower mode of the
// Dreadnought, Auditorium and Jumbo, as tall in the pressure response
// (320-416 Hz), plays only 2-3 dB over its neighbours (the Parlor's, at
// 461-486 Hz, puts A#4 +5.2 over), so the hole, not the response's tallest
// point, finds the peak: the engine locates both per construction from its
// own response at prepare (AcustraEngine::locateMidBand), so Shape, Wood
// and the calibration move them together, and the capture filter dips the
// peak. Measured with the dip (same sweep): A#4 +11.4 to +5.0 dB on the
// Dreadnought, C5 +8.4 to +3.8 on the Auditorium, G#4 +10.5 to +5.7 on the
// Jumbo, D#5 +5.4 to +2.2 on the Parlor. A matching boost at the hole
// (+8 dB, Q 8) was measured and not shipped: the hole's notes (C#5 on the
// Dreadnought, D#5 on the Auditorium, B4 on the Jumbo) are radiation nulls
// whose fundamental carries only -7 to -15 dB of the note's sustained
// power, so the boost raised each by about 1 dB while lifting the note
// above it 4.4-5.0 dB (D5 +4.9 to +9.4, E5 +8.4 to +12.8 against the dip
// alone), leaving each hole deeper against its neighbours. A real
// flat-top's own sweep deviates about 2 dB RMS. An authored capture
// refinement of the same kind as the air-mode dip, shipped on the user's
// direction without a listening verdict; the piezo never passes through it.
inline constexpr float originalMidHoleLowerHz = 450.0f;
inline constexpr float originalMidHoleUpperHz = 800.0f;
// The peak is the highest point within a fourth below the hole and must
// stand at least this far over it; otherwise the construction takes no dip.
inline constexpr float originalMidHoleSearchRatio = 1.3348f;
inline constexpr float originalMidHoleMinimumDepthDb = 6.0f;
inline constexpr float originalMidPeakDipDb = -9.0f;
inline constexpr float originalMidPeakDipQ = 8.0f;
// The dip's level restore, sized on the default cell's calibration phrase
// (Tools/CalibrateConstructionLoudness.py, Dreadnought Spruce Finger on
// the stereo microphones): the dip took it from -27.7532 to -28.2795 LUFS,
// and since every other construction and capture is levelled to that
// cell, the whole instrument went half a dB quieter with it (the Rack's
// piezo patch fell under its bank's loudness window). The restore puts
// the default cell back on its level; the constructions are re-levelled
// to it afterwards.
inline constexpr float originalMidBandLevelDb = 0.5263f;
// The mono microphone is the right channel at its own reference
// (monoReferenceFor). Under the dip it rose 0.15 LU against the pair on the
// default construction (the dipped mode radiates more to the left
// microphone), from 0.85 to 1.0002 LU over the default's stereo loudness,
// past the calibration's 1 LU; its default cell is pinned at 1 in the
// generated tables, so it takes this level instead, which puts the
// default's mono microphone on the target the other cells are levelled to.
// -1.0 dB, as a gain.
inline constexpr float originalMonoLevelGain = 0.891250938f;

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
