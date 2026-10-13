# Responsive dynamics — 13 October 2026

The user requested less level contrast between velocities and removal of
dramatic accents on repeated strums. This intentionally replaces the wider
keyboard response requested on 10 October; existing performances sound
stronger at soft and medium velocities. Parameter identities, defaults,
timing and release-velocity behavior are unchanged.

The authored playing-force curve is linear in MIDI velocity instead of
quadratic. Effective release edges scale with the force ratio, preserving
the established release speed/edge ratio and soft-to-hard articulation.
Raw velocity still drives Touch, contact aperture, pick release share and
noise. This is an authored keyboard response, not a measured law of hand
force or contact size. There is no compressor, output level follower or
per-note loudness normalization.

The isolated before/after probe covers 324 cases at 48 kHz: three picking
styles, three captures, open and octave-fretted low/high strings, and nine
velocities. Stereo RMS is measured over the first 300 ms with identical
random states and no per-layer gain adjustment.

| Style | MIDI 32 below 127, before → after | MIDI 64 below 127, before → after |
| --- | --- | --- |
| Finger | 24.18 → 12.20 dB | 11.99 → 6.04 dB |
| Pick | 29.14 → 17.19 dB | 12.76 → 6.81 dB |
| Thumb | 24.93 → 12.95 dB | 12.60 → 6.64 dB |

All 36 velocity paths rise monotonically. All 36 full-velocity single-note
waveforms are byte-identical to the frozen preceding engine. The largest
level-normalized four-band difference is 0.77 dB and the largest spectral
centroid change is 2.12%. Pick's very soft strokes retain more attenuation
from their slow release than Finger or Thumb. These measurements are not
a claim of a listening preference or identical low-velocity waveforms.

Live strum members previously drew up to ±7.74 dB of force variation from
a corpus that included uncontrolled player accents. Identical commanded
velocities now use a one-dB residual with the same random draw order and
shared hand motion. The independent uniform gain draw is normalized to
unit linear mean. Natural ringing, traversal and stroke-direction effects
remain; successive chords are not artificially forced to identical RMS.

An isolated strum probe covers 72 fixed-velocity sequences of 12 strokes:
three styles, two chord shapes, automatic/down direction, MIDI 64/100,
and 150/300/600 ms intervals. Narrowing this residual reduces the 95th
percentile adjacent RMS increase from 4.02 to 2.67 dB and the maximum
from 7.04 to 4.70 dB. Removing random force entirely retains almost the
same remaining variation, confirming the contribution of ringing and
direction. The combined shipped response is covered by the rendered
strum regression and the bounded velocity tests.

The Walnut Sessions renderer schedules its chord notes directly without
the live strum-member gain. Its scores contain only modest repeat accents,
so their phrasing and exact MIDI are retained for the new DSP renders.
The narrower velocity response addresses their level contrast; the live
strum residual correction additionally applies in VST/AU and Rack playing.

A native CPU/headroom probe on Apple M1 Max measured 45,000 callbacks per
engine version at 96 kHz/128 frames, equivalent to a 48 kHz/64-frame host
at the default 2x quality. It plays six strings in repeated chords across
three styles and three velocities, with 10% Room. Worst-case p95 thread
CPU is 171 microseconds after the change versus 173 before, against a
1,333-microsecond block deadline. Neither run has a thread-CPU deadline
miss, nonfinite output or clipped sample. Offline wall-clock scheduling
misses occur in both runs (36 current, 12 baseline); this is not a claim
of guaranteed host timing or Recon target CPU performance.

Frozen probes and detailed receipts are retained at
`/Volumes/SSD 1TB/Acustra-Demos/velocity-feedback-20261013/`.
