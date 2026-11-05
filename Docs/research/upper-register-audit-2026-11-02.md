# Upper-register release audit — 2026-11-02

> Written on a branch off 1.9.13 that numbered this work 1.9.14 and 1.9.15.
> Those versions reached main on 2026-11-05 as 1.9.20 and 1.9.21, after
> main's own 1.9.14 to 1.9.19; the figures here are from that branch.

The validated 1.9.14 changes remain the exact sampled half-power conversion for
Finger/Thumb release and the authored increase of the short fret-shift arrival
source. This follow-up found a smaller numerical approximation to investigate,
but no evidence supporting an additional release-phase or damping change now.
It does not establish that the documented high-note snap/mellow gap is solved.
No engine, test or build files were changed for this audit.

## Reference provenance

The historical Shinyguitar bank must be described as **archtop**, not flat-top.
The [instrument's own README](https://github.com/sfzinstruments/karoryfer.shinyguitar)
identifies an archtop guitar played and mapped by D. Smolken. Some old local
manifest labels and comments say flat-top; those labels are not independent
instrument evidence. The identified Eastman finger recordings remain a separate
source. The recovered native-PCM bank in
`../diagnostics/acustra-goal-20261102/reference-export/wavs/` is historical
calibration material, not a new held-out corpus. Its `index.tsv` and the old
manifest distinguish those sources. Retain native levels and the pinned slicing
protocol in any paired comparison; do not infer high-register flat-top realism
from the archtop samples alone. The historical `compare_reference_takes.py`
at `7b52fd1` selects only bank 0 (Shinyguitar), roots 40–84 and four velocity
layers; it excludes bank 1 (the eight Eastman finger regions). Its layered
brightness and onset-glide figures therefore describe Shinyguitar. The
separate historical eight-row flat-top split remains Eastman evidence.

## Endpoint restoration: sampled variance is still approximate

`applyPadRelease` restores its zero-at-bridge endpoint over a raised-cosine
window. After the 1.9.14 half-power correction, this width still uses the
continuous exponential time constant `tau = -1/log(q)` and the continuous
raised-cosine moment:

```
a = 1/3 - 2/pi²
W_old = min(N/2, tau * sqrt(2/a))
```

For the normalized, infinite, bilateral sampled kernel proportional to
`q^|k|`, the unwrapped variance is instead

```
variance_bilateral = 2*q/(1-q)²
```

Substituting this moment would give `W_simple = sqrt(variance_bilateral/a)`.
However, the actual correction is also sampled. With
`w_k(W) = (1 + cos(pi*k/W))/2` for `|k| < W`, its moment is

```
M(W) = sum(k²*w_k(W)) / sum(w_k(W))
```

A width can be called variance-matched only after solving `M(W)` against the
intended kernel moment. Correcting the bilateral moment alone is insufficient.
For `0 < W <= 1`, the sampled correction has zero moment; a positive target
requires `W > 1`. The existing `N/2` cap can also make a target unreachable.
For broad releases, the actual wrapped finite-loop kernel needs auditing;
the infinite unwrapped formula is not a global proof for that case.

A read-only 36-note initial-state probe covered high-E frets 12/16/20,
velocities 40/100, Finger/Thumb, and 44.1/48/96 kHz. Across those cases the
old sampled-cosine moment exceeded the infinite bilateral target by about
0.16–6.57%. Correcting only the bilateral moment left approximately
−0.79% to +0.44% sampled-moment error. At 48 kHz, high-E fret 20, velocity
100, Finger, the target was approximately 2.6587 samples²: the old width
4.6449 produced 2.8320 samples²; the simple width 4.5104 produced 2.6632;
a sampled-moment solve gave width 4.5069. These figures are approximate:
the original pole CSV used six-digit output, not an ULP-level oracle.

The evidence is in
`../diagnostics/acustra-goal-20261102/upper-release-width/{probe.cpp,poles.csv,variance-audit.json}`.
This is a defensible, bounded numerical follow-up, with an independent sampled
moment oracle and explicit cap behavior. Its modest scale does not support a
claim of materially corrected upper-note articulation. It has not been adopted
or audio-validated here.

## Release phase: physical derivation and a negative defect search

[Brauchler, Ziegler and Eberhard, Acta Acustica 4, 9 (2020)](https://doi.org/10.1051/aacus/2020008),
“Examination of polarization coupling in a plucked musical instrument string
via experiments and simulations,” measures both transverse directions during
realistic plucking. Figure 10 shows distributed initial velocities: the vertical
central motion continues with displacement, while the horizontal central motion
opposes it. The experiment uses a bare 0.43 mm electric-guitar third string,
0.65 m long, plucked at its midpoint. It is not a high B/E acoustic-body
measurement. This supports investigating displacement and velocity together,
not transplanting absolute parameters or imposing a universal plane sign.
The [full article PDF](https://pdfs.semanticscholar.org/a555/0309f199d9fef529d2f03dae4275731bc48e.pdf)
was inspected, including Figure 10 on printed page 8 (PDF page index 7).

The following reconstruction is derived from Acustra's encoded wave and its
read direction, rather than inferred from microphone phase. Let `s = x/L`,
`T = 2L/c`, and `W` denote its folded displacement wave in the physical release
frame. Playback advances toward decreasing stored phase. The two directions
then give

```
q(s) = W(s/2) - W(-s/2)
v(s) = [W'(-s/2) - W'(s/2)] / T
```

The initializer uses a common fitted-frame shift. If
`W_fit(u) = W(u - p/2) - C`, first undo it as
`W(u) = W_fit(u + p/2) + C`; the constant cancels in both equations.
Before slip, the displacement basis `D` is odd and the velocity basis `V` is
even. With `W = -a*(D + h*V)`, positive `h` gives central velocity along the
held displacement and negative `h` opposes it. An overall polarization sign
reverses both and does not change that relation.

Acustra slips `D` alone. That operation already contributes physical velocity,
so the sign of the added `h` alone is not the total release direction. Its
sampled energy equation is `V_energy*h² + 2*X*h = added`. The implemented root
has the sign of `X` and tends to zero with `added`. Forcing the opposite sign
when `X != 0` selects the other root and leaves a finite component as the
release share tends to zero. Neither an arbitrary sign flip nor energy-only
orthogonalization is a demonstrated correction: the latter can also alter the
held displacement. The fitted frame matters to body and idle-string phase.

To test the suspected neighboring-note sign discontinuity, a frozen source
snapshot and library were used for 66 fresh-engine initial states, without
processing audio: B and high E, frets 9–20 at Pluck Position 0.28, plus fret 20
on both strings at positions 0.26–0.30 in steps of 0.002; Pick, velocity 100,
48 kHz. The scratch basis belongs to the last initialized, parallel plane.
A periodic-slip-plus-velocity reconstruction of its final stored field had
relative residuals 2.2e-8–5.3e-8 and recovered added-energy shares
0.4740457–0.4740474, consistent with the existing authored velocity law.
All 66 cross terms were negative (−0.05960 to −0.01231), as were all recovered
hump ratios (−0.18590 to −0.11878); no zero crossing or sign discontinuity was
found in this grid.

After undoing the common shift, a real trigonometric interpolation of each
stored period also gave negative displacement/velocity dot products and
negative central velocity/displacement ratios in every case. This is a reduced
periodic-wave diagnostic. It does not reconstruct dispersive/filter initial
state, prove the other plane's direction, cover alternating or legato strokes,
or validate the radiated attack. The deterministic fresh-engine sweep is not
a broad phase-realism test. Reproduction and results are in
`../diagnostics/acustra-goal-20261102/phase-audit/{probe.cpp,analyze.py,states.csv,audit.json}`.

## Recommendation

Retain the current release phase and by-ear contact, release-energy and loss
constants. This audit retracts the suspected phase-sign defect for the tested
grid. Additional constant high-frequency loss or removal of the legacy upper
contact width is not justified by these findings: neither restores the missing
early articulation while preserving the later decay.

If another numerical iteration is pursued, isolate the sampled endpoint-moment
correction first. Require an independent moment check including sub-sample
widths, broad/capped kernels and finite-loop behavior; preserve level and the
existing exact half-power response. Then use identical events, seeds and
whole-file review trims for high B/E Finger/Thumb comparisons, with Pick as an
unchanged control, and inspect onset versus late partial behavior separately.
The native Eastman finger slices can supply a technique-matched historical
reference where available. A change to distributed release velocity remains a
separate research experiment requiring stronger technique/register-matched
evidence; Figure 10 alone does not select an Acustra waveform or absolute
phase. No extra mechanism should ship merely to label this another realism
improvement.
