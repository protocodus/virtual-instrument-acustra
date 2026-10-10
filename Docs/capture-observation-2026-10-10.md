# Capture-voicing observation — 2026-10-10

[Evaluation finding 11](physical-model-evaluation-2026-10-10.md) found that the
capture-voicing fit compares recordings with the L/R average of the Original's
spaced microphone pair. This record covers that work package. It changes the
fit's observation, and that of every tool whose scores feed calibration. It
gives a dry run on the frozen `e51f6a9` baseline and answers the Mono-contour
question. It also lists the refit sequence to run once the string-loss and
saddle/air-mode packages merge.

Only tools changed. `CaptureVoicingData.h`, the engine and the plug-in are
untouched, and no audio is committed. Nobody has listened to a refitted
voicing: every number below is a descriptor, not a preference.

## What was wrong

The Stereo mic writes each output as mid ± Width × side of two near-field
omnis about 20 cm apart: the treble-bridge microphone (left) and the
upper-bout one (right) (`AcustraEngine::process`). Every scoring tool took
`mean(axis=1)` of a render. That average is the pair's mid, from which Width
cancels. On g21's 1.23–1.37 kHz plate modes the two microphones' residues are
130–180° apart, so the mid loses that band. No coincident pair or single
microphone does.

Measured on the baseline renders of all five sources (equal-loudness note sums
over 0.03–1.0 s; whole GuitarSet clips), the model's L/R average loses
7.7 dB (5.3–9.2 by source) against its per-channel power in the 1.26 kHz
third octave. The losses are 1.6–2.0 dB at 1 kHz, 1.5–2.9 dB at 1.6 kHz and
1.9–4.5 dB at 5–6.3 kHz. The Eastman's coincident pair loses 0.9 dB at
1.26 kHz. A four-note probe on the default Dreadnought gives a microphone
correlation of −0.84 at 1.25 kHz and −7.0 dB of mid retention. The
evaluation's strummed chord gave −0.79 and −8.5 dB.

The fit therefore read a hole that neither output channel has, and pushed up
the 1 kHz and 1.4 kHz peaks that can fill it. Compared instead with the
capture each recording matches, the five-source consensus moves sharply. At
1.26 kHz (recording minus model, level removed) it goes from +1.05 dB, model
short, to −4.63 dB, model hot; at 1 kHz from −0.66 to −3.74 dB. Against all
five sources, the shipped contour leaves each Stereo output channel 3.7 dB
hot at 1.26 kHz and the Mono mic 5.4 dB hot. The evaluation's probes had
estimated +5–6 and +7–9 dB. This is the direction of the known "Finger
attack too bright" gap.

### Which capture each recording corresponds to

| Source (tool) | Recording | Capture, per its source | Model observation (`matched`) |
|---|---|---|---|
| Eastman E1D picked and finger (`PrepareEastmanCorpus.py`) | 48 kHz stereo, decoded from the CC0 Opus masters | Coincident pair; inter-channel delay about 0.08 ms; mono sum −0.1…−0.3 dB (`ThirdParty/Eastman-E1D-README.md`) | Per-channel power of both outputs |
| Bank flat-top (`AcustraPhysicalFitRenderer`) | 48 kHz stereo | Eight notes of the same Eastman finger-plucked take | Per-channel power of both outputs |
| Martin HD28 (`PrepareMartinCorpus.py`) | 44.1 kHz mono | Not documented (SFZ header, repository README); read as one microphone | Mono mic |
| GuitarSet (`BenchmarkPerformances.py`) | Mono | Neumann U87 about 30 cm from the 18th fret | Mono mic |
| Archtop bank train/validation (`AcustraPhysicalFitRenderer`) | Mono | Shinyguitar acoustic microphone | Mono mic |
| Walden G551E (`CalibrateRecordedGuitars.py`) | Mono | Not documented | Mono mic |

The bank flat-top's eight notes are the same performances as eight of the 55
Eastman finger rows: all eight bank anchors are corpus rows. The consensus
still weighs the bank flat-top as its own source, so the Eastman guitar
carries half of it. The weights are unchanged here; the sensitivity is
reported below.

## What changed

### The observation protocol

Version 2, `matched`, is now the default in every changed tool. Version 1,
`mid`, reproduces earlier scores and fits exactly; select it with
`--observation mid`. Reports, manifests and summaries record the observation
and, per source, how each model render was read.

- **Two-channel recording: per-channel power.** Every power spectrum, the
  onset follower's energy and the level average the channels' powers. The
  model's two outputs are read the same way, at the Width they were rendered
  with (0.62).
- **One-channel recording: the Mono mic.** A `mono_mic` (or piezo) render is
  used as rendered. A dry Stereo mic render yields its upper-bout microphone
  exactly. The Mono capture is that microphone's filtered pressure at its own
  level trim (`BodyBank::render`). With Width w, Room 0 and Piezo Mix 0, it is
  ((1 + w) R − (1 − w) L) / 2w (`FitPhysicalModel.mono_mic_from_stereo`). The
  recovery refuses Room, a Width below 0.05, a peak at the limiter's knee,
  and a render whose capture, Width or Room is not recorded.
- **Checked against the engine.** On all six GuitarSet performances, the
  microphone recovered from the Stereo render equals the Mono render to
  −143 dB, at unit trim on the default construction. On Auditorium/Mahogany
  (trim 0.956) and Auditorium/Spruce the match is also −143 dB.
  `BenchmarkPerformances.py --self-test --renderer` now runs this check under
  ctest, so a capture change that breaks the premise fails there.
  `FitCaptureVoicing.py --guitarset-mono` repeats it on every refit.

### Tools changed

| Tool | Change | Earlier behaviour | Regression in its self-test |
|---|---|---|---|
| `FitPhysicalModel.py` | Per-channel descriptors; `matched` default; a manifest may declare `observation`; paired comparisons refuse mixed observations | `--observation mid` or `"observation": "mid"` | A pair whose L/R average cancels 1.0–1.6 kHz: body residual 0.00 dB matched, −89 dB legacy, against one microphone and a coincident pair; recovery refusals |
| `BenchmarkOpenCorpora.py` | Declares the observation in manifests and summary; diagnostics read the model as the score does; `--compare` reads both runs alike | `--observation mid` | Smoke: the observation reaches manifest, score and both model views; the cancelling pair through `score_split` reads 0.00/−89 dB |
| `OptimizePhysicalModel.py` | Passes `--observation` to every manifest, spawned workers included | `--observation mid` | No self-test; `OptimizerFreezeTests` passes |
| `BenchmarkPerformances.py` | Mono mic for GuitarSet; imports its scorer by path so it runs under `python3 -I` | `--observation mid` | Cancelling pair 640–1280 Hz: 0.00 dB matched, −17.7 dB legacy; the engine premise to −143 dB |
| `FitCaptureVoicing.py` | Protocol version 2; `--guitarset-mono` check; diagnostics; Mono-contour test | `--observation mid` | Cancelling pair 1.0–1.6 kHz fitted within 0.19 dB of the truth, where legacy boosts +6.1 dB; a source without recovery controls stops the fit |
| `CalibrateRecordedGuitars.py` | Renders the Mono mic for its mono recordings | `--observation mid` | No self-test exists and its archive was not obtained; `evaluate()` checked on synthetic targets with the baseline renderer |

### Reviewed, not changed

- `BenchmarkTechniqueNotes.py` is an exploratory descriptor comparison. Its
  frozen protocol names the Stereo mics and says it is not a tuning set. If it
  ever informs a calibration, render `mono_mic` as `CalibrateRecordedGuitars.py`
  now does.
- `CompareBodyCapture.py` and `CompareConstructionResonance.py` already report
  per-channel power and mono retention separately.
- `CalibrateConstructionLoudness.py` and `MeasureMaterialLoudness.py` measure
  BS.1770 loudness, which sums channel powers, and they measure the Mono mic
  as its own capture.
- Audit and analysis tools that average channels (`AuditAttackBands.py`,
  `AuditDampingCurve.py`, `ComparePickingAttacks.py`, `CompareGuitarBodies.py`,
  `ExtractExcitation.py`, `AnalyzeBridgeProbe.py`, `SummarizePluckSweep.py`,
  `MeasureStrums.py`) are diagnostics and probes, not calibration scores.
  Read their band levels near 1.26 kHz with that in mind.

### Earlier results stay reproducible

On the baseline renders, `--observation mid` reproduces the `e51f6a9` tools
exactly:

- the voicing fit: every reported number; only the balance header's path
  differs, its content is identical;
- the six physical-score manifests: score, every term and every weighted
  residual;
- the three open-corpus summaries, diagnostics included;
- the GuitarSet report.

The matched run of a Stereo render reproduces the Mono render's GuitarSet
metrics within 1e-6. With identical channels, both observations fit the same
voicing (self-test).

## Dry run on the `e51f6a9` baseline

### Provenance

- **Source.** `e51f6a9`. The evidence ran the tools at `5e5973e` (SHA-256 in
  the JSON companion); the commit adding this record changes only
  `FitCaptureVoicing.py`'s docstring. The legacy reference ran the `e51f6a9`
  copies of `FitCaptureVoicing.py` and `FitPhysicalModel.py`.
- **Executables.** The frozen baseline set, built from `e51f6a9` (GCC 13.3.0,
  Release). SHA-256: `AcustraPhysicalFitRenderer` `7b9779d2…04ee`,
  `AcustraExternalCorpusRenderer` `e68fc14f…c9ea`, `AcustraPerformanceRenderer`
  `95ba4099…96`.
- **Controls.** The 24 shipping values (`OptimizePhysicalModel.SHIPPING`) and
  default controls: Dreadnought, Spruce, Finger, Width 0.62, Room 0.
- **Data**, prepared with each tool's own protocol and kept out of Git:
  - GuitarSet v1.1.0: `annotation.zip` MD5 `b39b78e6…`, `audio_mono-mic.zip`
    MD5 `275966d6…`; both match the published values.
  - Eastman: `picked.opus` `35e9e45b…` and `plucked.opus` `16f5a8c7…`, the
    pinned SHA-256 values. 49 picked and 55 finger rows; all 16 README
    anchors detected.
  - Martin: all 17 files verified by git blob and SHA-256 at `7a9c478f`;
    11 rows, 4 rejected.
- **Renders.** The bank (54/14/8 examples, 40 renders); the open corpora
  (94 renders); GuitarSet with the Stereo and the Mono mic.
- **Environment.** Python 3.13.16, NumPy 2.5.3, SciPy 1.18.1, Linux x86-64.

### Section gains

"Settled" is after the simulated passes described below; "change" is
`matched` minus `mid`.

| Section | Shipped header | `mid`, pass 1 | `matched`, pass 1 | Change | `mid`, settled | `matched`, settled | Change |
|---|---:|---:|---:|---:|---:|---:|---:|
| Low shelf 120 Hz, Q 0.7 | +0.58 | −1.19 | −1.27 | −0.08 | −2.12 | −2.26 | −0.14 |
| Peak 125 Hz, Q 1.2 | +2.75 | +1.20 | −1.19 | −2.39 | +0.46 | −1.55 | −2.01 |
| Peak 250 Hz, Q 1.2 | −6.00 | −6.00 | −6.00 | 0.00 | −6.00 | −6.00 | 0.00 |
| Peak 500 Hz, Q 1.2 | −6.00 | −6.00 | −6.00 | 0.00 | −6.00 | −6.00 | 0.00 |
| Peak 1000 Hz, Q 1.2 | +3.28 | +2.61 | −0.76 | −3.38 | +2.57 | −0.99 | −3.56 |
| Peak 1400 Hz, Q 1.2 | +6.00 | +6.00 | +6.00 | 0.00 | +6.00 | +6.00 | 0.00 |
| Fit residual (dB rms) | | 1.98 | 2.32 | | 2.06 | 2.35 | |

The observation moves two sections: the 1 kHz peak by −3.4 dB (−3.6 settled)
and the 125 Hz peak by −2.4 dB (−2.0 settled). The 250 and 500 Hz cuts stay
on their −6 dB bound, and the 1.4 kHz peak on its +6 dB bound, under both
observations.

Both observations also move the shelf and the 125 Hz peak well away from the
shipped header. The header was fitted on an earlier engine (2026-10-01, before
the 2026-10-08 summed-pressure filter), so part of any post-merge change is
drift that predates this fix.

### Per band

Consensus: recording minus model, level removed. Fitted: the fitted
contour, level removed. Retention: the model's L/R average over its
per-channel power. Mono minus Stereo: the same renders read both ways, level
removed. Values in dB.

| Band (Hz) | Consensus, `mid` | Consensus, `matched` | Change | Fitted, `mid` | Fitted, `matched` | Model retention, mean (range) | Eastman pair retention | Mono minus Stereo |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 79 | −2.34 | −1.66 | +0.68 | −0.45 | −0.32 | −0.2 (−0.2…−0.1) | −0.1 | −2.58 |
| 99 | +1.10 | +0.59 | −0.51 | −0.27 | −0.91 | −0.0 (−0.1…−0.0) | −0.1 | +0.19 |
| 125 | +4.21 | +3.19 | −1.02 | −0.48 | −1.63 | −0.2 (−0.2…−0.1) | −0.1 | +1.12 |
| 157 | −1.34 | −2.23 | −0.90 | −1.78 | −2.45 | −0.1 (−0.1…−0.1) | −0.1 | +0.88 |
| 198 | +1.73 | +2.36 | +0.63 | −4.14 | −4.07 | −0.1 (−0.1…−0.0) | −0.1 | −1.23 |
| 250 | −0.73 | −0.36 | +0.37 | −6.19 | −5.69 | −0.0 | −0.1 | −1.18 |
| 315 | −0.82 | −0.36 | +0.46 | −5.95 | −5.30 | −0.0 | −0.1 | −1.31 |
| 397 | −1.42 | −2.72 | −1.29 | −5.70 | −5.10 | −1.0 (−1.3…−0.7) | −0.3 | +0.60 |
| 500 | −2.46 | −2.07 | +0.39 | −5.32 | −5.01 | −0.3 (−0.6…−0.1) | −0.5 | −1.42 |
| 630 | −3.75 | −2.03 | +1.72 | −2.28 | −2.64 | −1.1 (−1.5…−0.6) | −0.5 | −1.83 |
| 794 | −0.57 | +1.40 | +1.98 | +1.65 | +0.22 | −0.8 (−1.8…−0.4) | −0.4 | −4.15 |
| 1000 | −0.66 | −3.74 | −3.08 | +5.06 | +3.00 | −1.8 (−2.0…−1.6) | −1.0 | +2.30 |
| 1260 | +1.05 | −4.63 | −5.69 | +7.08 | +5.79 | −7.7 (−9.2…−5.3) | −0.9 | +1.78 |
| 1587 | +0.83 | +2.09 | +1.26 | +6.56 | +6.30 | −2.1 (−2.9…−1.5) | −2.0 | −4.46 |
| 2000 | −0.04 | +1.19 | +1.22 | +4.34 | +4.75 | −0.9 (−1.2…−0.6) | −2.4 | −0.78 |
| 2520 | +2.97 | +4.84 | +1.86 | +2.68 | +3.46 | −1.7 (−3.0…−0.8) | −2.8 | +1.28 |
| 3175 | −2.72 | +0.10 | +2.82 | +1.78 | +2.76 | −0.3 (−0.7…−0.2) | −5.2 | −0.14 |
| 4000 | +1.03 | +2.82 | +1.79 | +1.29 | +2.38 | −0.9 (−1.3…−0.7) | −5.7 | +3.58 |
| 5040 | +2.36 | +2.08 | −0.28 | +1.02 | +2.17 | −2.9 (−4.5…−1.9) | −4.7 | +2.34 |
| 6350 | +3.12 | −0.41 | −3.53 | +0.86 | +2.05 | −3.9 (−4.2…−3.5) | −3.3 | +3.91 |
| 8000 | −3.70 | −4.23 | −0.52 | +0.77 | +1.98 | −1.7 (−1.9…−1.4) | −2.6 | +2.48 |
| 10079 | +0.68 | −0.92 | −1.61 | +0.71 | +1.94 | −1.9 (−2.2…−1.6) | −2.7 | +3.13 |

The fitted contour at 1.26 kHz drops only 1.3 dB, short of the consensus's
5.7 dB change. The matched target now wants less at 1.26 kHz and more at
1.6 kHz (+9.1 dB). The 1.0/1.4 kHz pair at Q 1.2 cannot follow both, and the
1.4 kHz peak is on its bound. After the fit, the matched contour sits +2.7 dB
over its target at 1.26 kHz and −2.9 dB under it at 1.6 kHz.

Moving that section's centre to 1.6 or 1.8 kHz lowers the matched residual
from 2.32 to 2.13 or 2.01 dB rms (the 1 kHz peak then rises to +0.5 or
+1.7 dB). That would refine an existing section, not add a parameter. This
package does not adopt it; it would need its own A/B.

### Stability

The leave-one-source-out ranges below are each section's spread over five
fits, one per source left out.

- **1 kHz peak.** +1.15…+4.33 dB under `mid` and −1.39…−0.25 dB under
  `matched`.
- **125 Hz peak.** +0.28…+2.84 dB under `mid` and −2.21…+0.03 dB under
  `matched`.
- **Direction.** The direction of both changes holds for every subset.
- **Shelf.** The least determined section, with a range of 5.0 dB (`mid`) and
  5.4 dB (`matched`). It follows the bank flat-top: without that source it
  moves −2.7 dB (`mid`) and −3.1 dB (`matched`).

### Passes to convergence

One pass does not settle the fit. The analog band-mean model of the contour
differs from what partial-weighted note spectra see, most of all at the shelf.

The passes were simulated rather than rebuilt. Both baseline outputs (and with
them the Mono mic) were filtered with the engine's own RBJ sections at 48 kHz,
as H(new)/H(old). The capture filter is one LTI filter on each microphone's
summed pressure, applied before the linear Width mix. The strings never see
it, so the filtered render equals a re-render up to float rounding.

| Pass | `matched`: shelf, 125 Hz, 1 kHz moves (dB) | `mid`: shelf, 125 Hz, 1 kHz moves (dB) |
|---:|---|---|
| 2 | −0.70, −0.26, −0.22 | −0.62, −0.46, −0.03 |
| 3 | −0.21, −0.06, −0.01 | −0.21, −0.19, −0.01 |
| 4 | −0.06, −0.03, 0.00 | −0.08, −0.07, 0.00 |
| 5 | −0.02, −0.01, 0.00 | −0.02, −0.02, 0.00 |

Plan on four render–fit–rebuild passes, not two, and stop when every gain
moves less than 0.1 dB.

### What a listener would hear change

The runtime contour is the generated base plus the authored balance
(`MicrophoneBalanceData.h`), level removed, in dB.

| Contour | 100 Hz | 125 Hz | 250 Hz | 500 Hz | 800 Hz | 1 kHz | 1.26 kHz | 1.6 kHz | 2 kHz | 3.2 kHz |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shipped | −1.82 | −2.02 | −6.33 | −5.02 | +2.34 | +5.89 | +7.77 | +7.05 | +4.69 | +2.03 |
| `mid`, settled | −4.50 | −4.64 | −6.21 | −4.45 | +2.62 | +6.05 | +8.09 | +7.59 | +5.38 | +2.84 |
| `matched`, settled | −4.89 | −5.47 | −5.66 | −4.20 | +1.02 | +3.78 | +6.64 | +7.22 | +5.72 | +3.77 |
| `matched` − `mid` | −0.39 | −0.83 | +0.55 | +0.25 | −1.59 | −2.26 | −1.45 | −0.37 | +0.34 | +0.94 |
| `matched` − shipped | −3.07 | −3.45 | +0.67 | +0.83 | −1.32 | −2.11 | −1.12 | +0.17 | +1.03 | +1.75 |

Relative to the shipped voicing, the settled matched refit lowers 0.8–1.26 kHz
by 1.1–2.1 dB and raises 2–3.2 kHz by 1.0–1.8 dB. The observation alone
accounts for 1.5–2.3 dB of the lowering.

The refit also lowers 100–125 Hz by 3.1–3.5 dB, 2.6–2.7 dB of it the drift
noted above. The authored balance (−3 dB shelf, −2.75 dB at 125 Hz) applies
on top of any refit, so the runtime bass would then sit about 3 dB under the
state a listener approved on 2026-10-09. Audition it specifically.

### Scores of the same renders under both observations

These differences come from the scorer, not the engine: the renders are
identical. Lower note-split scores mean closer descriptors on these
recordings. Scores taken under different observations are not comparable, and
the reports now say which observation they used.

| Split | `mid` | `matched` |
|---|---:|---:|
| Bank train (archtop, single microphone) | 6.8630 | 6.6863 |
| Bank validation | 6.5775 | 6.4755 |
| Bank flat-top | 6.5853 | 6.2147 |
| Eastman pick | 5.5814 | 5.4754 |
| Eastman finger | 6.2741 | 5.9635 |
| Martin HD28 | 6.9128 | 6.7117 |
| GuitarSet log-magnitude / SC / chroma | 13.990 / 0.821 / 0.230 | 14.046 / 0.842 / 0.270 |

The GuitarSet octave bands (model minus U87, mean of six) show where the
voicing over-boosts:

| Octave (Hz) | 80–160 | 160–320 | 320–640 | 640–1280 | 1280–2560 | 2560–5120 | 5120–10000 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `mid` (Stereo average) | −4.28 | +0.17 | +1.23 | +3.13 | −1.79 | −3.93 | −7.87 |
| `matched` (Mono mic) | −3.76 | −1.25 | +1.05 | +8.39 | +1.85 | −1.99 | −1.74 |

GuitarSet reads further under `matched` because the Mono mic carries 8.4 dB
more than the U87 in the 640–1280 Hz octave. The old average showed only
3.1 dB, so the shipped contour's over-boost was hidden there.

## Does the Mono capture need its own contour?

Not with the current structure, on the baseline.

The Mono mic is the upper-bout microphone alone. Against each Stereo output
channel (level removed, consensus of the five sources) it reads:

- +2.3 dB at 1.0 kHz and +1.8 dB at 1.26 kHz;
- −4.2 dB at 0.8 kHz and −4.5 dB at 1.6 kHz;
- +2.3…+3.9 dB from 5 to 10 kHz.

The signs at 1.0, 1.26 and 1.6 kHz hold on every source. But the six-section
structure cannot follow alternations within an octave, and it has no section
above 1.4 kHz.

Fitted to all five sources through each capture alone, the two contours
differ only at the 1 kHz section: by +1.44 dB, and 1.02–1.63 dB leaving one
source out. With its own contour, the Mono capture's residual against the
consensus improves from 2.668 to 2.655 dB rms, a gain of 0.013 dB. The
Stereo capture's improves from 2.387 to 2.383 dB. A separate Mono table would
add an internal table and per-sample filtering for no measurable gain. The
recommendation is one shared contour, fitted with the matched observation.

`FitCaptureVoicing.py` reports `mono_contour_test` for the post-merge refit.
It indicates a separate Mono contour when the Mono-only fit reads at least
0.25 dB rms closer to the consensus than the shared contour. That threshold
was declared after this dry run, not before it. WP-B's air-mode filter and
saddle termination change the balance between the microphones, so read the
test on the post-merge renders.

If the test is indicated, the engine change would be as follows. It is not
made here, and WP-B also edits these files.

- **`CaptureVoicingData.h`.** Add a second table,
  `captureVoicingMonoSections[]`, with the same six kinds, frequencies and Qs.
  The fit writes it from `capture_contours.mono.sections`, and the shared
  table takes the `capture_contours.stereo` gains. `captureVoicingLevelDb`
  stays shared: `constructionMonoTrim` already sets the Mono level per
  construction, and the loudness calibration refits it.
- **`AcustraEngine::BodyBank::CaptureFilter`.** Add Mono coefficients from
  that table through the same `balancedOriginalCaptureSection` refinement, and
  a third set of double-precision histories (`stateUpper`). Set them in
  `configure()`, clear them in `reset()`, and copy them with the bank through
  Shape/Wood fades.
- **`BodyBank::render`.** Keep the upper-bout microphone's unfiltered sum,
  filter it with the Mono sections into `output.upper` (today
  `output.upper = output.right` after the Stereo filter), and leave left and
  right as they are. This adds six biquads per sample per sounding bank.
- **Tools and tests.** Give `captureVoicingGain` a Mono variant for the tests
  that divide the voicing out, and have `FitCaptureVoicing.write_header` write
  both tables.
- **Follow-up.** Re-level with `CalibrateConstructionLoudness.py` (mono
  trims), extend `CaptureTests`/`CaptureVoicingTests` to the Mono path, and
  audition Stereo and Mono against the accepted voicing.

## Post-merge refit

Run this from the merged tree's root once the string-loss and saddle/air-mode
packages land. `DATA` holds the prepared corpora and GuitarSet archives from
this dry run. Prepare them again with `PrepareEastmanCorpus.py` and
`PrepareMartinCorpus.py` elsewhere. `OUT` is a new directory; `python3` needs
NumPy and SciPy.

```sh
DATA=/tmp/claude-0/-home-user/3341a6c0-325e-5cce-be54-2af95316457a/scratchpad/wp-l/data
OUT=/path/to/new/capture-refit
mkdir -p "$OUT"
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF \
  -DACUSTRA_BUILD_TOOLS=ON -DBUILD_TESTING=ON
RENDERERS="AcustraPhysicalFitRenderer AcustraExternalCorpusRenderer \
  AcustraPerformanceRenderer AcustraConstructionStrumLevels"
cmake --build build-dsp --config Release --parallel 2 --target $RENDERERS
V=$(python3 -c "import sys; sys.path.insert(0, 'Tools'); \
from OptimizePhysicalModel import SHIPPING; print(' '.join(format(v, '.9g') for v in SHIPPING))")

# 0. The default construction's loudness before the refit.
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer ./build-dsp/AcustraPerformanceRenderer --json "$OUT/loudness-before.json"

# 1. One pass: render the three sources, fit, write the gains, rebuild.
refit_pass() {
  P="$OUT/pass$1"; mkdir -p "$P"
  ./build-dsp/AcustraPhysicalFitRenderer "$P/bank" $V
  python3 -I Tools/BenchmarkOpenCorpora.py \
    --renderer ./build-dsp/AcustraExternalCorpusRenderer --output "$P/open" --keep --jobs 2 \
    "$DATA/corpora/eastman/rows.json" "$DATA/corpora/martin-hd28/rows.json"
  python3 -I Tools/BenchmarkPerformances.py --dataset "$DATA/guitarset" \
    --renderer ./build-dsp/AcustraPerformanceRenderer --output "$P/guitarset"
  python3 -I Tools/BenchmarkPerformances.py --dataset "$DATA/guitarset" \
    --renderer ./build-dsp/AcustraPerformanceRenderer --output "$P/guitarset-mono" \
    --capture mono_mic
  python3 -I Tools/FitCaptureVoicing.py --bank "$P/bank" --open "$P/open" \
    --guitarset "$P/guitarset" --guitarset-mono "$P/guitarset-mono" \
    --json "$P/fit.json" --write-header
  cmake --build build-dsp --config Release --parallel 2 --target $RENDERERS
}
refit_pass 1; refit_pass 2; refit_pass 3; refit_pass 4
# Compare each pass's "sections" in fit.json with the previous pass; run
# further passes until every gain moves < 0.1 dB. Read diagnostics.
# mono_contour_test in the last fit.json (see above).

# 2. Restore the default construction's loudness with the header's level.
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer ./build-dsp/AcustraPerformanceRenderer --json "$OUT/loudness-after.json"
# Only the level changes: the header keeps the last pass's gains (refitting
# the last renders against the rewritten header would apply its step twice).
python3 - "$OUT" <<'EOF'
import json, sys
sys.path.insert(0, "Tools")
import FitCaptureVoicing as fit
out = sys.argv[1]
read = lambda name: json.load(open(f"{out}/{name}"))["summary"]["target_lufs"]
sections, level = fit.read_header()
level += read("loudness-before.json") - read("loudness-after.json")
fit.write_header(sections, level)
print(f"captureVoicingLevelDb {level:.2f}")
EOF
cmake --build build-dsp --config Release --parallel 2 --target $RENDERERS

# 3. Re-level every construction and capture, keeping the strum guards (the
#    physical packages land in the same merge), then check.
./build-dsp/AcustraConstructionStrumLevels > "$OUT/strums.json"
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer ./build-dsp/AcustraPerformanceRenderer --json "$OUT/loudness-relevel.json" \
  --write-header --strum-measurements "$OUT/strums.json"
cmake --build build-dsp --config Release --parallel 2
./build-dsp/AcustraConstructionStrumLevels > "$OUT/strums-check.json"
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer ./build-dsp/AcustraPerformanceRenderer --check \
  --strum-measurements "$OUT/strums-check.json"
```

Step 0 keeps the merged build's default loudness. If the physical packages
already moved it away from the accepted −24.05 LUFS (`e51f6a9`), use that
value as the "before" level instead.

Step 3 is for a merge that also re-levels physical changes. For a voicing
change alone, the existing microphone-only option `--write-header
--preserve-piezo-level` keeps the absolute pickup level instead; it cannot be
combined with `--strum-measurements`.

Then run the affected suites (`Capture`, `CaptureVoicing`,
`ConstructionLoudness`, `ConstructionMatrix`, `OutputBuses`, `BodyShape`) and
the tools' self-tests (`FitCaptureVoicing`, `PhysicalScorer`,
`OpenCorporaBenchmark`, `PerformanceBenchmark`). Score the open corpora and
GuitarSet with `--compare` against pass 1. Then hold a blind A/B against the
accepted voicing, with Finger attacks on the upper strings and the bass
change in particular.

## Limits

- **Listening and engine.** No listening took place. No engine source,
  header or audio was changed or committed. CPU is unchanged: only tools
  changed.
- **Sources.** The Martin's capture is undocumented and is read as one
  microphone. The bank flat-top and the Eastman finger rows are the same
  take.
- **Simulated passes.** Passes 2–5 were simulated by exact LTI filtering,
  not rebuilt. WP-B moves an air-mode filter into the capture filter, so the
  real post-merge passes must be rendered.
- **Fixed structure.** The structure is unchanged. Its 1.4 kHz peak sits on
  its +6 dB bound under both observations, and it has no section above
  1.4 kHz. The threshold for a separate Mono contour was set after seeing
  this baseline.
- **Platform.** All results are Linux x86-64 with GCC 13.3.0. Cross-platform
  waveform hashes need not match.
- **Defaults.** The tools now default to `matched`. Scores and fits from
  before 2026-10-10 used `mid`; compare like with like.

The dry-run evidence lives outside the repository, under
`/tmp/claude-0/-home-user/3341a6c0-325e-5cce-be54-2af95316457a/scratchpad/wp-l/`:

- `evidence-final/`: fit reports, sensitivity, passes, parity checks and tool
  hashes;
- `renders/`: the baseline renders;
- `data/`: the prepared sources;
- `scripts/`: the analysis scripts.

[`capture-observation-2026-10-10.json`](capture-observation-2026-10-10.json)
holds these tables in machine-readable form.
