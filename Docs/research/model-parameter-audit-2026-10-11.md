# Model and parameter audit — 2026-10-11

Scope: ACUSTRA only, based on freshly fetched `main` at
`a534d774aa85f31acac6e74e6efebc18221ec748`. Work is isolated on
`codex/acustra-model-audit-20261011`; the pre-existing edit in the primary
checkout is untouched. This reviews the current steel instrument, its twelve
Shape/Material constructions, playing gestures, capture routes and native
plug-in parameter/state contracts. Historical retired guitar models are not
reintroduced.

## Corrections

### Preserve the fitted broad resonances

`radiationPole` imposed Q ≥ 4 even though the fitted banks contain a
286.767 Hz mode at Q 2.469 and a retained joint-body 371.907 Hz mode at
Q 1.253. This lengthened their free radiation decays by approximately 1.62×
and 3.19×. It also changed the latter mode's bridge response, which reads the
same fitted Q. The safety floor is now 0.05, below every retained mode even
at the minimum calibrated Q scale. The positive pole decay remains stable;
the separately selected air-mode extension and upper Q bound are retained.

Actual digital poles, recovered at 48 kHz for Dreadnought/Spruce:

| Mode | Played frequency | Previous Q | Corrected Q | Previous free T60 | Corrected free T60 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Measured g21 broad mode | 260.168 Hz | 4.000 | 2.469 | 33.806 ms | 20.869 ms |
| Retained joint-body broad mode | 332.257 Hz | 4.000 | 1.253 | 26.471 ms | 8.290 ms |

These are individual free radiation decays, not the decay of a whole played
note. A one-second, velocity-0.8 Finger E chord in Spruce at fixed controls changes overall
RMS by −0.082, −0.156, −0.200 and −0.112 dB across Parlor, Auditorium,
Dreadnought and Jumbo. No listening preference is claimed.

The regression checks 12,792 actual radiation poles across twelve
constructions, 8/48/96 kHz and Q calibration scales 0.05/1/1.8, including
216 broad-mode instances. The maximum relative Q recovery error is 0.0862%
from float pole storage. Low-Q chord renders remain finite and bounded, and
the cumulative observed body-port work remains nonnegative.

The correction lowers the reference calibration phrase slightly, so the
existing Pick gains needed an update to retain their loudness/headroom
policy. `CalibrateConstructionLoudness.py` regenerated only the Pick cells
from native measurements, constrained by the five-tuning strum measurements.
Finger/Thumb gains and the default reference remain unchanged. No loudness,
headroom or strum tolerance was loosened.

### Plan physical chords from playable pitches

The performer disabled chord planning whenever a sample contained more than
six note-on messages, even if the extras were duplicate owners or unreachable
keys. A three-pitch chord `[40, 59, 64]` acquired different physical string
timing when those silent/duplicate events were added. Planning now counts
unique playable pitches. Duplicate MIDI ownership and the existing fallback
for harmonics or genuinely oversized chords remain intact.

### Retain the original pedal decision for queued strums

A slow strum can receive key-ups before its last strings are picked.
Pressing sustain after those key-ups and then sending All Notes Off (CC123)
incorrectly caught the pending strings. CC123 now preserves the original
key-up ownership decision; a late pedal cannot recatch released strings.
The documented cancellation of pending attacks by CC123 is retained.
Regression cases cover both pedal orders at 44.1/48/96 kHz.

### Recover after invalid master tuning

Nonfinite master tuning was retained in the performer even though the
engine sanitized the resulting bend. Every later pitch-wheel message was
therefore suppressed too. NaN and either infinity now reset the stored
offset to zero, with rendered regressions proving that later RPN/wheel
changes still work.

### Expose the existing sound controls

Room and Release Noise were active, host-automatable parameters with 50%
and 70% plug-in defaults, but neither appeared in the custom editor. Both
now have attached knobs in the existing window. IDs, automation order,
ranges, defaults and legacy-state migration are unchanged. The editor's
Material tooltip now describes the effective soundboard substitution;
Body's tooltip explains its microphone-only path and audible minimum.

The processor suite checks both directions of parameter binding and control
visibility at minimum, default and maximum sizes. Rendered editor snapshots
were visually inspected at 896×784, 1120×980 and 1456×1274. The README image
is refreshed from the actual component render.

## Preserved choices and limits

The bare engine's dry/no-release defaults and the native plug-in's Room and
Release Noise defaults intentionally differ for existing offline references
and saved-state compatibility. Legacy no-op parameter slots remain for host
identity. Construction presets, active parameter conversions and migration
were reviewed without finding a further actionable mismatch.

The earlier room experiment was explicitly rejected by the listener; this
audit does not restore it or refit the accepted capture voicing. Wood remains
an effective plate-material substitution, with the qualifications in
[its source report](wood-materials-2026-10-11.md).

The joint-body fitter uses shared fitted frequency/Q values with different
transfer conventions: radiation uses a damped sinusoid, while the passive
bridge uses a second-order mobility denominator. Their mathematical poles
are therefore not identical at broad Q. This audit restores the fitted
values under those original conventions; it does not refit the raw data or
claim exact shared poles. This remains a model approximation requiring a
separate jointly parameterized fit before changing the transfer functions.

## Validation

The final source builds in Release on Apple M1 Max, macOS 26.5.1, with Apple
Clang 21. The native arm64 VST3, AU and Standalone targets and the C++17 DSP
compatibility target build successfully. All **83 CTests pass** in 260.89 s,
including the required Python tool tests and processor/editor tests. No test
tolerance was relaxed. This is native validation, not a universal binary,
Rack SDK build or an interactive DAW qualification.

The fresh native loudness check passes all 108 construction/picking/capture
cells against its unchanged ±1 LU policy:

| Capture | Minimum relative loudness | Maximum relative loudness |
| --- | ---: | ---: |
| Stereo microphone | −0.174 LU | +0.212 LU |
| Mono microphone | −0.000 LU | +0.840 LU |
| Piezo | −0.900 LU | +0.195 LU |

The default phrase reference is −27.541 LUFS. The 180 native Finger strums
(twelve constructions × three captures × five tunings) are finite and pass
the existing ±9 dB construction guard; the largest absolute departure is
4.701 dB. The calibration's hardest Finger/piezo cases still engage the
existing limiter. Pick/piezo uses the existing permitted loudness sacrifice
of up to 0.9 LU rather than promising 1 dB of spare headroom in every case.
Passing this policy does not mean every signal stays below the limiter knee.

The quiet paired callback benchmark uses the same compiler and flags for
the fetched baseline and frozen final candidate, four warmup pairs and 32
measured pairs per case. Across 24 cases at 48/96 kHz and 32/64/128 frames,
the median of paired case medians is 0.9970 current/baseline; the median of
separate callback-median ratios is 1.0018. This supports no material CPU
change in these tested native engine callbacks, not a speedup claim. The
largest per-case paired median increase is 2.79% for a 96 kHz/64-frame
six-string re-pick.

One existing performance limit remains: the simultaneous six-string re-pick
at 96 kHz/32 frames exceeds its 333.333 µs deadline in 32/32 baseline and
31/32 candidate observations. Candidate median/p95 are 420.917/505.250 µs;
baseline median/p95 are 442.667/1043.125 µs. The other 23 cases have no deadline
misses in either build. This audit does not establish support for that most
demanding buffer setting. Timing excludes Performer, JUCE and host overhead.

The tracked [validation receipt](../validation/model-parameter-audit-2026-10-11/receipt.json)
records source hashes, build settings and commands. That directory also
retains the final build/test logs, calibration and strum measurements,
before/after Q probe, and raw paired callback observations with provenance.
Full frozen source copies and binaries remain locally in
`build-audit/model-audit-evidence/`.
