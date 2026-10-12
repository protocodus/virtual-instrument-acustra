# Acustra chorus repair, 2026-10-12

The user reported a new chorus-like tone compared with Rack 1.2.11f1 or earlier,
including at 1x with Detune and the modulation wheel at zero. Canonical baseline
`8ea51f9` is that comparison; `1d1f432` supplied the initial 1.3.0 tuning change.
The requested sound repair is the baseline for subsequent CPU optimization.

## Cause and repair

The initial tuning change independently corrected each polarisation's H1 bridge
phase with a different constant delay. That also shifted every upper partial,
although their physical bridge phases were already close. On low E at fret 12,
H5's plane separation grew from 0.04 to 11.70 cents. On open G, H8 grew from
0.02 to 3.18 cents. This appeared at both 48 and 96 kHz; it was not caused by
per-note Detune, Room, the quality converter or the modulation wheel.

Both planes again receive one shared tuned delay. The common correction uses
coupled saddle modes and their remaining energy after three seconds, replacing
an attack-weighted 100 ms estimate. Eigenvectors use their better-conditioned
representation; normalized logarithmic weights avoid underflow on damped notes.
This is a bounded unit-circle approximation, checked independently against
actual damped poles rather than described as an exact complex-pole solver.
No additional audio filter, gain shaping, string muting or numeric-format change
was added. Intrinsic loss, dispersion, pluck direction and the real shared
junction continue to govern both planes.

The calibration loads the other strings through history-free resting open
branches. Their inventory comes directly from `openNotes(parameters_.tuning)`,
so sequential preparation, reset and retuning cannot expose incomplete voices.
The reference builder uses the existing intrinsic loss/dispersion mathematics
and exact completed-fit cache. Its open branches do not recursively incorporate
this common correction. That approximation avoids a tuning feedback loop;
actual played strings, their bends and retained tails still interact in the
real junction and can produce natural acoustic shifts.

A per-voice cache covers exact nominal frequency, impedances, period gains,
string index, rate, construction/calibration generation and sympathy policy.
The small energy-driven attack excursion is excluded from nominal calibration;
it still changes the performed pitch and tuned delay. Copy/capture keeps the
complete cache, and reset/prepare invalidate it through the engine generation.
Rebuilding a reference or calibration from the same inputs must preserve audio
bits. There is no allocation, lock, I/O or extra random pitch modulation.

## Independent audible and physical checks

`ChorusRegressionTests` reads the microphone waveform with 60 ms Hann-window
sinusoidal projections over 0.30–1.25 s, removes only a straight logarithmic decay,
and bounds the remaining 5th-to-95th-percentile envelope spread to 1 dB. It tests
Finger, Pick and Thumb at 48/96 kHz on low-E fret-12 H5 and open-G H8. A six-decibel
bracket around the pre-polish harmonic amplitude prevents hiding beating by
suppressing the observed harmonic. The exact `8ea51f9` control passed all twelve
cases at 0.035–0.124 dB; `1d1f432` failed all twelve at 7.05–18.19 dB. The first
shared-delay control passed all twelve at 0.028–0.175 dB. The final canonical
candidate passed all twelve at 0.030–0.179 dB with the amplitude bracket retained.

Short-window Fourier maxima are not reliable intonation estimators for an
unresolved physical mode pair. The independent digital-pole oracle found old
open-G poles at -3.334/+0.199 cents, only 0.40 Hz apart, while a 1.1-second Hann
window's maximum read +4.566 cents, outside both poles. Old A-string D3 similarly
read -9.745 cents outside its four physical poles. Chasing those maxima changed
the actual guitar modes and created the upper-partial problem.

`ModalTuningTests` independently reconstructs the actual digital bridge, all
twelve loop transfers and the microphone observer, searches damped roots with
bounded starts, checks residuals/mode coverage, and uses the physical pluck split
and radiated residue to measure the three-second mode centre. It never calls
production's tuning helper. The physical centre and dominant sustained mode are
bounded to two cents on fifteen adversarial cases at 44.1, 48 and 96 kHz. The full
physics, frozen-source receipts and finite-window reconstructions are described
in `Docs/chorus-mode-measurement-2026-10-12.md` and retained in the Rack project's
`Output/validation/chorus-investigation-20261012/math-oracle` directory; its final
source/data manifest is `receipt-final.json` (the earlier `receipt.json` used
the superseded contact-noise weighting).

The existing 1.8-cent single-peak sweep remains for its other 87 cases. Its three
unresolved open-G cases now use the independent physical guard. The previous
6.5-cent short-window sympathy guard likewise defers D-string E3 to the physical
guard: that waveform's maximum was +10.925 cents although its poles topped out
at +1.234 cents and the three-second radiated centre was about -0.008 cents.
For the exact mono-average observer used in Engine, four physical damped modes
reconstructed the measured +10.924547-cent maximum at +10.922089 cents with
0.04048% waveform residual. This demonstrates the invalidity of that window
maximum as a single-pole tuning guard; it does not widen a physical pitch bound.
Its fast lower member at -9.523 cents decays at 3.623/s; natural transient modes
are not described as a zero-cent instantaneous waveform. Other established
pitch, articulation, force, release and port-energy guards are retained.

## Detune range steering

For the upcoming revision the user reduced maximum Detune from the historical
±50 cents to ±20 cents. `EngineParameters.detune` remains normalized 0–1;
100% now draws uniformly within ±20 cents at actual contact. Dedicated random
streams, scheduled-control sampling, held offsets, reset and reattack semantics
are unchanged. The initial ±50-cent evidence in `rack-polish-2026-10-12.md` is
historical and is not presented as validation of the new range.

## Validation status

The initial focused iteration passed ChorusRegression, StringPitchRealism,
PitchGeometryCache and the C++17 DSP build. Its Engine run retained all existing
articulation/force guards and exposed only the invalid short-window pitch
observers described above; that failed log is preserved at
`/tmp/acustra-chorus-focused-core-20261012.log`. Reference-builder checks cover
all five tunings at three rates and three string ages, with an independent
master-tune offset. They compare the actual intrinsic open-string coefficients,
gains and delay, plus fresh/reset/forced preparation and cached/cold audio.

The settled canonical focused run passed Engine, ChorusRegression, ModalTuning,
StringPitchRealism and PitchGeometryCache (five suites, 123.82 s), plus the
JUCE-free C++17 DSP build. The subsequently finalized ModalTuning source passed
all fifteen cases at 44.1, 48 and 96 kHz, including D-string E3 at velocity 0.72.
The maximum absolute centre across those cases was 1.53401 cents. The 48/96 kHz
centres in the original focused receipt were:

| Fixture | 48 kHz centre | 96 kHz centre |
| --- | ---: | ---: |
| Dreadnought open G | +0.480 cents | +0.474 cents |
| Dreadnought low-E fret 12 | +1.532 cents | +1.506 cents |
| Dreadnought A-string fret 5 | -1.161 cents | -1.161 cents |
| Jumbo open low E | +0.224 cents | +0.231 cents |
| Dreadnought D-string fret 2, velocity 0.72 | -0.008 cents | -0.006 cents |

Each centre and dominant three-second mode passed the two-cent bound. The
physical test rejects the old `8ea51f9` control on low-E E3 and Jumbo open E2,
while the audible chorus test rejects the initial-polish `1d1f432` control on
all twelve cases. These controls discriminate accurate sustained tuning from
coherent upper partials independently.

Focused receipts are `/tmp/acustra-chorus-focused-final-20261012.log`,
`/tmp/acustra-chorus-modal-final-20261012.log` and
`/tmp/acustra-chorus-audible-final-20261012.log`. The cache suite also compares
98,304 rendered frames with forced recalibration and nonzero Detune. The initial
failed measurements remain available; they have not been replaced by passing
logs. Broad DSP and actual SDK/native CPU validation are coordinated by the
Rack owner after this sound-source freeze. This report makes no CPU speedup or
host-listening claim.
