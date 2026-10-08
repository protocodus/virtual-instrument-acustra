# Body resonance, microphones and alignment — 2026-10-08

This pass addresses the request for a smoother, clearer and more natural body
sound. The reference is `0ab33425d9779baacf8be7713667a9e1641657a1`, frozen
with its source, compiler settings and native renderer before edits.
The changes fix three demonstrated signal-path problems. They do not replace
the measured guitars or claim a new listening preference.

## Microphone correction after modal summation

Original previously multiplied each complex radiation residue by the capture
contour's magnitude at that mode's frequency. That does not implement the
contour on the microphone pressure: overlapping modal tails can cancel, and
weighting them differently can turn a specified cut into a boost. The audit
found this reversal near B4 in the measured force/moment response of the
treble-side microphone, as well as smaller aggregate low-mid errors across
all four body shapes.

The existing low shelf and five peak sections now filter the summed pressure
of each microphone. Their frequencies, Qs and relative gains are unchanged.
The reference scalar moves from 3.82 to 4.12 dB, compensating the measured
0.3028 LU reduction in the default calibration phrase. This is a level
correction, not an additional tonal boost. The authored model-brightness
convergence remains a separate modal transform.

The filter uses double-precision coefficients and state for Original. A
float-only prototype had a 1.49% complex-response error at 384 kHz; the final
independent impulse-response check is below `2.8e-7` across 8–384 kHz, against
the frequency-prewarped analog prototypes. Classical 78 retains its existing
two sections and float arithmetic. Its dry three-note chord is byte-identical
to the reference at 44.1, 48 and 96 kHz in the isolated parity check.

Each body bank keeps its own filter histories through shape/wood changes and
model crossfades. Reset, All Sound Off and prepare clear them. No lookahead,
new parameter or additional host-reported latency is introduced. The physical
bridge, strings, measured resonance frequencies and losses remain intact.
In particular, the previously auditioned Original Q floor is not retuned.

## Construction observer alignment

A newer Shape, Wood or Model request can wait for an existing bridge fade.
The tuning observer previously mixed that newer request with shape factors
from the already configured bridge. In a Dreadnought → Parlor → Jumbo probe,
it reported a 76.09 Hz modal center while the deployed Parlor mode was
111.34 Hz. This is a body-mode mismatch, not a measured 659-cent note error.

The observer now reads the configured bridge's model, shape and material.
Applying a queued bridge already invalidates the voice tuning cache, so the
strings then follow that applied construction. Independent complex transfer
checks cover requests, cancellations, queue boundaries, voice-cache refresh
and reset at 44.1, 48 and 96 kHz. The new tests produce 36 failures against
the reference and pass after the fix.

## Output and room balance

Output previously scaled the room input, leaving the old room tail at its
earlier level after a gain reduction. The room now runs before Output and
its return receives the same gain as the dry microphones. A mute therefore
also mutes existing ambience; starting muted keeps the room history warm
for a later unmute.

The isolated regression's maximum relative gain-envelope error falls from
43.10% to `2.14e-7`. It covers both models, all capture choices, Piezo Mix,
the auxiliary piezo, muted startup, and 44.1/96 kHz. The dry path retains its
arithmetic. The existing seven-sample microphone/piezo pipeline compensation
and measured microphone channel mapping were already correct.

## Reproduction and interpretation

[`Tools/CompareBodyCapture.py`](../Tools/CompareBodyCapture.py) renders both
models with Finger, Pick and Thumb, dry and at Room 50%. Every isolated note
starts from a fresh engine; a separate arpeggio/chord phrase exercises overlap.
It retains native float audio, event scores and executable hashes, and applies
one shared audition gain across both versions. No per-note or per-version
normalization conceals level changes. Its band measurements describe the
signal; they are not perceptual scores or a new comparison with recordings.

The local frozen sources, raw audio, matched WAVs, calibration measurements,
diagnostics and build/test logs are under
`/workspace/scratch/acustra-resonance-2026-10-08/`.

## Native comparison and tradeoffs

The final set contains 24 clips per version, 169.2 seconds each: eight isolated
notes (E2, A2, D3, G3, B3, E4, G4, B4) and an overlapping phrase, both models,
three techniques, Room 0/50%. Every WAV uses the same +7.717 dB gain; raw floats
remain unscaled. Original's maximum native peak falls from 0.33469 to 0.31610.
The complete set's maximum remains 0.37014, with no full-scale samples.
Classical 78's dry stereo clips are byte-identical; wet differences are at
most `2.98e-8` from moving Output around the room computation.

Original's B3–G4 notes generally lose low-mid emphasis. G4 Thumb at Room 50%
reduces the 180–630 Hz / 800–1600 Hz ratio by 2.71 dB. B4 recovers a weak
fundamental: its largest increase in that ratio, Thumb dry, is 5.91 dB, from
−11.17 to −5.26 dB, still presence-dominant. Across individual notes, RMS moves
−1.07 to +0.88 dB and peaks −1.35 to +1.33 dB. Worst mono-retention movement is
−0.44 dB on G4 Pick dry, ending at −4.52 dB relative to stereo-channel power.

This is not a uniform mud-reduction result. Pooled isolated-note low-mid /
presence ratios increase slightly, 0.14–0.33 dB. Phrase ratios move only
−0.031 to +0.070 dB. The corrected transfer, register-specific improvements
and preserved general balance motivate the change; no human preference or
new recording-realism score is inferred.

Original microphone gains are regenerated from the existing 216-cell
construction/technique/capture protocol. Classical 78's microphone gains stay
fixed. A separate small pre-existing level discrepancy was exposed in Pick's
piezo headroom check: its trims on both models now decrease 0.107–0.332 dB,
using the existing generator's `gains` result and −0.9 LU headroom allowance.
The formula for each changed relative pickup trim is
`fitted_mic * fitted_piezo / retained_mic`. Finger/Thumb pickup levels retain
their previous absolute references to float rounding. These are static output
trims, not changes to the piezo circuit or its input drive. The hardest
deliberate stress cases can still enter the final safety limiter; the protocol
prioritizes ±1 LU level consistency and caps the headroom sacrifice at 0.9 LU.

## Regression interpretation

The new capture changes which harmonics dominate a band-sum brightness ratio.
All 256 tested Finger bridge-force trajectories remain bit-identical, yet
C5's loud/soft H5–H12/H1–H4 rise changes from +4.02 to −0.086 dB: its H1 falls
about 14.6 dB at both velocities and H2/H4 now dominate the denominator.
Its harmonic centroid still rises 14.5% with velocity. The test retains the
3 dB articulation threshold on a declared fixed reference observation and
also requires the actual native harmonic centroid to increase for every
tested note. Native band-ratio results remain printed, including C5.

The cross-rate mic/piezo test formerly compared different host-rate random
attack bursts. It now removes that burst only from the timing fixture, with
the existing 0.5 dB / 20 µs limits unchanged. Correct alignment gives
0.091–0.130 dB and 4–6 µs; deliberately omitting seven samples gives
0.803–1.033 dB and 75–80 µs, failing both guards in every case. The shipped
stochastic takes remain in the native audio evidence and ordinary capture
tests. No arbitrary delay was introduced to fit those different waveforms.

## Validation

- All 70 JUCE-free DSP and tool suites pass on the final calibrated source.
  Python-dependent checks were required at configure time, not silently skipped.
- Both native plug-in suites pass: PluginProcessor and PluginTempoRelease.
  Linux VST3 and Standalone targets build successfully.
- The full 216-cell native construction check passes: stereo −0.90..+0.00 LU,
  mono −0.90..+0.25 LU, piezo −0.90..+0.31 LU relative to the default phrase.
  Every Pick cell lacking the desired stress-case headroom uses the allowed
  0.9 LU sacrifice. The reference default remains approximately −24.05 LUFS.
- DSP/Performer compile as C++17, including a separate strict warning build of
  the capture implementation. `git diff --check` passes.
- All 29 frozen baseline Source files match the reference commit. Final
  source, renderer, scorer, event, raw-audio and listening-audio hashes are
  preserved with the local evidence.

The build is GCC 14.2, Release, on Linux x86-64. AU/macOS, Windows, Reason
packaging and physical audio-device audition were not run. The canonical
instrument repository is changed; the Reason wrapper's instrument pin is
not advanced. Human listening remains necessary to judge preference.

The quiet-machine callback benchmark uses matched GCC `-O3 -DNDEBUG` builds,
32 alternating measured pairs after eight warmups, pinned to CPU 4. Its 48
cases cover 48/96 kHz, 32/64/128 frames, both guitars, and initial chords,
re-picks, natural harmonics and live rolled joins. Finite output, repeated
hashes and active-string checks all pass. Original's median per-case paired
cost increases 0.67%; the median per-case p50 increase is 2.29 µs. Bellido's
paired ratio is 0.987, with all 24 output hashes unchanged; this small timing
movement is not claimed as an optimization.

Tail timings vary: 33 reference and 36 candidate observations exceed their
block deadline out of 1,536 observations each. These are native note-on plus
processing measurements, excluding host overhead and steady-only playback;
they do not establish a hard real-time guarantee. Full observations, source
and binary hashes, compiler flags and the run receipt are in `cpu/` beside
the other local evidence.
