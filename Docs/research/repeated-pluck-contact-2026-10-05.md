# Repeated-string contact — 2026-10-05

This report records the first contact-only change. Its separate-tail approach
was subsequently replaced for same-pitch repeats by
[one continuing string and corrected Recuerdos holds](recuerdos-tremolo-2026-10-05.md).
The original measurements and audition files remain historical evidence.

Equal-pitch tremolo previously wrote every new release independently of the
ringing string, carrying the preceding wave in the existing damped tail. The
tail could therefore contribute its old displacement at the point the new
stroke was already holding. This change constrains that displacement before
the new release, in both string polarisations.

## Contact approximation

The constraint reads the same direct and returning fractional taps as
`StringLoop::displacementAt`. Their sparse interpolation weights form a linear
functional `l`. Its discrete static Green function `h` solves the cyclic
Laplacian equation `Lh = l`. With `D` denoting adjacent-sample differences, the
retained wave becomes

```
s_new = s_old - (<D s_old, D h> / <D h, D h>) h
```

This zeros the observed contact displacement within float rounding and removes
`<D s_old, D h>² / <D h, D h>` from the sampled slope norm. The mean is preserved;
the wrap-edge difference is included. Two linear passes solve and apply the
constraint without heap allocation or a delay-sized scratch buffer. The
incoming derivative is re-referenced to its preceding trend so the imposed
displacement change is not counted as a one-sample impact.

Only an elapsed, same-pitch/channel, normal-string re-pluck with a captured
ringing state applies this correction. First plucks, silent reattacks,
same-sample duplicates, changed pitches and natural harmonics retain their
existing release path. The approved release shape, force draws, contact noise,
calibration and hand-loss settings remain unchanged.

This is an approximation to the hold before release. It does not resolve the
tool's trajectory or establish whole string/body passivity: the fractional
period discretisation, copied filters, emitted contact waves and provisional
two-port tail model remain. No measured contact duration or listener preference
is inferred from the slope-norm identity.

## Scheduled-repeat correction

The engine clock now advances after each rendered sample. Previously, a
delayed release inside a block used that block's starting timestamp for repeat
history. A queued equal-velocity repeat could consequently be classified as a
same-sample duplicate, unlike an immediate note-on at the identical absolute
sample. The new regression failed on the old implementation and compares the
two rendered outputs exactly across block cuts.

## Validation and listening

`Acustra.RepeatedPluckRealism` covers the contact constraint, retained-wave
energy, scheduled repeats and actual repeated audio. Existing engine, strum,
release, contact transport, allocation and performer suites remain the broader
guards. Run the focused suite using the standard CMake DSP build:

```sh
cmake --build build --target AcustraRepeatedPluckRealismTests
ctest --test-dir build -R '^Acustra.RepeatedPluckRealism$' --output-on-failure
```

The accompanying listening package compares the prior `8dccaa5` DSP with this
change using identical original scores, deterministic starting states, velocity
and common gain. Finger, Pick and Thumb repeat at 4, 8 and 12 strokes per second
on open and fretted strings; an original musical tremolo passage supplies
context. Dry and studio-room versions are included. Source hashes, scores and
the external renderer accompany the audio. These examples support listening;
numerical variation alone does not establish greater naturalness.

Independent probing covered 3,840 phase/contact cases across 44.1–192 kHz and
MIDI 40–100. Contact displacement residual RMS was approximately `2.25e-8`
of the original; no sampled slope-energy increase or heap allocation occurred.
All 24 listening pairs preserve bit-identical PCM before the first repeat.
Their whole-file RMS changes range from -0.0394 to +0.0514 dB, with no clipping.

At the separate 33-strokes/second stress rate, both old and new DSP exceed a
comparison against the first isolated 30 ms: the low string/body attack has
not yet developed in that window. The corrected guard checks later-stroke
stability instead. The candidate's radiated window energy is approximately
10–12% higher than the baseline there, while both settle; this is not ruled
out by the retained delay-line slope constraint. No claim of coupled energy
conservation follows from these microphone measurements.

Validation completed: the focused repeated-pluck suite, eight broader DSP
suites and the two demo-renderer checks passed. The focused suite covers
12/33 Hz repeats at four sample rates and exact stereo output across block
sizes 1, 17, 127 and 512. Linux VST3 and standalone builds completed, and the
standalone opened and rendered its window under Xvfb. The ten standard demos
were regenerated from this DSP; their separate distribution archive retains
the renderer's usual per-file peak normalization, unlike the common-gain A/B.
