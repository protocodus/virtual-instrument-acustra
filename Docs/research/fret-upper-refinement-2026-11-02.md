# Fret noise and upper-register refinement — 2026-11-02

> Written on a branch off 1.9.13 that numbered this work 1.9.14 and 1.9.15.
> Those versions reached main on 2026-11-05 as 1.9.20 and 1.9.21, after
> main's own 1.9.14 to 1.9.19; the figures here are from that branch.

This iteration starts from canonical Acustra `73665a8` (1.9.13), the Rack
Extension's recorded instrument revision. It changes the canonical DSP;
the Reason adapter consumes that revision without a copied or patched engine.

## Research and choice

[Pakarinen, Penttinen and Bank, JASA 122 (2007), EL197–EL202](https://doi.org/10.1121/1.2786607)
find winding harmonics and noise RMS proportional to sliding speed, with
longitudinal modes superimposed. Acustra already implements winding crossings,
speed-dependent roughness and a separate experimental axial observer. That
paper does not identify this instrument's absolute noise level. The axial
observer remains experimental.

The 1.9.13 shift-arrival source used the key-up brush's force share, 0.0005,
at a preset Release Noise of 0.1. This iteration gives only that short source
four times the force, 0.002 (+12.041 dB). Continuous slides, key-up brushes,
random draws, timing and the linear control retain their existing behavior.
This is an authored audibility choice in response to the user's request.
No fret buzz or sampled squeak is added. Open/plain strings, repeated frets,
first attacks and fixed-fret member bends remain ineligible for shift noise.

The upper-register investigation found a mathematical error in the 1.9.11
soft-contact release. Forward/backward filtering squares a one-pass filter's
magnitude ([SciPy's filtering documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.filtfilt.html)).
For a one-pole release with pole `b`, the old conversion `q = b^sqrt(2)`
does not preserve the stated half-power corner. Even its continuous-time
limit gives amplitude 2/3 (-3.522 dB) there instead of 1/sqrt(2) (-3.010 dB),
with a rate-dependent discrete error as the corner approaches Nyquist. The
error's direction is not uniform near Nyquist, so this is a corner correction
rather than a promised treble boost at every possible contact setting.

With `H_b(z) = (1-b)/(1-b z^-1)`, the exact corner gives
`2(1-cos(wc)) = (1-b)^2/b`. Requiring `|H_q(wc)|^2 = 1/sqrt(2)` gives
`(1-q)^2/q = (1-b)^2/[b(sqrt(2)-1)]`. Production evaluates the stable
reciprocal quadratic root; the existing analytic release contract instead
bisects the response equation independently. Thumb's inverse reference uses
the same correction. The existing rest-at-bridge correction follows the
corrected kernel's continuous-time width approximation. No fitted contact width, damping, release time or
new treble EQ is introduced. Both changes have offline-only ablation switches.

Removing the legacy upper-fret contact width was considered but left alone:
brighter sustained partials would not resolve the documented snap/mellow gap.
Extra constant damping was also rejected here because it would remove an
attack the instrument already lacks. This iteration improves the intended
soft-contact filter, rather than claiming to complete high-register realism.

## Reproducible comparison

`Tools/RenderFretUpperReview.cpp` renders the canonical Performer/engine path
at each stated native rate, with fixed events, seeds and 64-frame blocks.
It does not include a front end's output-rate adapter or host scheduling.
Output is unity and mixed Piezo is 20%; individual high-note cases are dry,
while the fret phrases use Room 10%. Its 234 cases cover 44.1/48/96 kHz; B/high-E frets
9, 12, 13, 14, 16, 19, 20 at velocity 100; soft velocity 40 at frets 12, 16,
20; Finger/Pick/Thumb; wound lines and allocated barre-chord shifts at Release
Noise 0/0.1/0.5; plain/open/same-fret and fixed-fret member-bend controls.
Every case has native-level float stereo WAV and an exact event manifest.

`Tools/AnalyzeFretUpperReview.py` requires identical manifests. It measures
20–120 ms harmonic balance and centroid, a separate 250–450 ms upper-partial
fall descriptor, native RMS/peaks and shift-window nulls against the same
score at Release Noise zero. Those nulls can contain earlier key-up residue;
the controls and isolated-source regression distinguish that residue. The
fall descriptor is neither a fitted T60 nor a listener preference score.

The lettered comparison is whole-file stereo-RMS matched, with its trims and
letter key recorded separately. Native files remain untouched. This is a
local review artifact, not a replacement for the frozen factory demos.

Validation results and comparison measurements are recorded with the final
iteration in `Docs/validation/fret-upper-refinement-2026-11-02.md`.
