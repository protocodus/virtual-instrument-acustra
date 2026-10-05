// A small, reciprocal voicing of the two model microphones. The listener
// requested each model about 25% closer to the other's brightness (2026-10-02).
// Tools/FitModelConvergence.py measures matched, level-independent note spectra.
// The full-gap tilt is 1.75 dB/octave; a quarter gives each model 0.4375
// dB/octave toward the other, around 1 kHz. Original brightens and Bellido
// warms. This is an artistic balance, not a refit of either measured guitar.
//
// configureBody applies this once, after building the radiation continuation,
// to both microphones' complex force/moment residues at each mode's frequency.
// The physical bridge, string calibration, poles and mode counts stay intact;
// the piezo pickup response is unaffected. Construction level trims are
// calibrated separately, and no processing is added to the audio loop.
#pragma once

#include <algorithm>
#include <cmath>

namespace acustra::detail
{
inline constexpr float modelConvergenceAmount = 0.25f;
inline constexpr float modelConvergenceFullTiltDbPerOctave = 1.75f;
inline constexpr float modelConvergencePivotHz = 1000.0f;

inline float modelConvergenceGain(float frequency, bool bellido) noexcept
{
    const float boundedFrequency = std::clamp(frequency, 60.0f, 10000.0f);
    const float direction = bellido ? -1.0f : 1.0f;
    const float db = direction * modelConvergenceAmount
        * modelConvergenceFullTiltDbPerOctave
        * std::log2(boundedFrequency / modelConvergencePivotHz);
    return std::pow(10.0f, db / 20.0f);
}
} // namespace acustra::detail
