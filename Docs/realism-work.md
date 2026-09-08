# Guitar realism work, 2026-09-08

The requested end state is more realistic steel and nylon guitars, three
capture choices (stereo microphone, mono microphone, loaded piezo), normal
high-velocity note release without a second stroke, and physical models
calibrated against recordings of different guitar constructions. Passing the
release or capture tests alone does not establish that broader end state.

## Verified behavior changes

- Normal note release is dissipative at every MIDI release velocity. Active
  pull-offs require CC68. See [release measurements](release-fix-2026-09-08.md).
- The editor exposes only Stereo mic, Mono mic and Piezo. Mono uses the
  measured upper microphone response identically on L/R; piezo includes its
  electrical load. Retired signal paths are removed and saved states migrate.
  Legacy capture automation lanes must be moved to the new `captureMode` ID.

## Recording calibration

The initial experiment uses Mohammed Alkooheji's
[Acoustic Guitar Notes v3](https://www.kaggle.com/datasets/mohammedalkooheji/guitar-notes-dataset):
Walden G551E steel and Yamaha CM-40 nylon. The manufacturer's
[G551E specifications](https://waldenguitars.com/product/g551e/) identify a
grand auditorium with a mahogany top. Both body models remain proxies:
the steel auditorium/mahogany setting does not independently identify the
Walden response, and nylon auditorium/spruce does not identify the Yamaha.

The first two numerically ordered finger/thumb takes at each of nine fixed
pitches are training data. Nine other pitches are development validation.
Selection depends only on filenames; the source archive SHA-256 is checked.
Six of nine training model pitches use open strings; none of the development
pitches do. This tests an open-to-fretted distribution shift, rather than a
balanced random sample of all guitar notes.
Velocity 91 and lowest-fret string assignment are assumptions because the
recording metadata does not supply them. Capture, playing position and source
trim are also unknown. Only normalized attack/spectral shape, harmonics,
decay and body-band descriptors enter this exploratory fit. Source latency,
absolute tuning and dynamic response cannot be inferred from these files.

Two rounds of bounded coordinate polling change only string decay, loss,
contact width and pluck position. Validation is scored after the training
winner is selected, and cannot choose a different winner. These development
pitches are now used data, not an untouched final test set.

With the original contact kernel, the selected nylon candidate changes
`fundamentalT60Scale` from 0.86484718 to 1.037816616 and `apertureScale` from
1.7688939 to 2.12267268. On the Yamaha files, training score changes
8.280994 → 8.016665 and development score 8.182101 → 7.947889. In the separate
embedded classical-guitar corpus, nylon training changes 7.530845 → 7.443032
and development 7.472233 → 7.158937. Steel audio is unchanged by these two
nylon-only parameters. These two values are now in the instrument; the full
engine, release, capture, renderer-control and plugin-processor suites pass.

The steel training winner improves 9.133216 → 8.952589 but worsens its
development score 8.360820 → 8.382826. It is not selected for the instrument.
Scores are engineering descriptor losses, not listener realism ratings.
Exact values, parameter changes and hashes are in
[the calibration report](realism-calibration-2026-09-08.json).

Reproduce the experiment with a fresh output directory:

```sh
cmake --build build-dsp --target AcustraPerformanceRenderer
python3 Tools/CalibrateRecordedGuitars.py \
  --renderer build-dsp/AcustraPerformanceRenderer \
  --archive /path/to/guitar-notes-v3.zip \
  --initial-calibration Docs/realism-baseline-calibration.json \
  --output /new/calibration-directory --fit
```

Omit `--fit` for evaluation with the supplied calibration. The renderer also
accepts `--body-shape`, `--body-material`, and `--calibration FILE`. The latter
contains whitespace-separated `parameter.name value` pairs; unspecified
parameters inherit the compiled defaults. Unknown, duplicate, nonfinite or
out-of-range values are rejected before rendering. The calibration tool
supplies all 29 named values and retains every actual control file.
It copies the renderer executable before any measurements, freezes source
hashes at startup, and records library versions. An independent rerun with
that workflow reproduced all 36 baseline float-stereo files byte for byte.

## Model work still under evaluation

The former contact approximation used five spatial taps with a
`cos^4(pi*n*a)` response. This has repeated full-amplitude high-harmonic
lobes. A continuous Gaussian contact with the same variance has response
`exp(-2*pi*pi*n*n*a*a)` and eliminates these revivals. That implementation now
retains the signed endpoint correction and passes the combined engine and
release checks with the nylon calibration. Its recording results improve nylon
and pooled development scores, while steel changes are small and mixed. See
[contact and power-observer evidence](contact-kernel-and-power-2026-09-08.md).

The Original steel radiation bank still adapts the nylon-strung Mores g21.
The new Model selector adds four independently fitted bridge/radiation pairs:
Bellido 1978 nylon, Washburn 1897 parlor, Santa Cruz OM3 2022 and Martin D18V
2007 steel. The Bellido has three real microphones; the other three have one
and deliberately remain mono. Shape is a more audible radiation variation
around each bank, not a measurement of four rebuilt bodies. See
[measured model integration](body-models-2026-09-08.md).

## Picking and shape feedback

The user's follow-up identified weak differences between picking styles and
between body shapes. Finger and Pick previously converged exactly at maximum
steel velocity. Pick now uses a narrower contact nearer the bridge; Thumb
uses a broader contact nearer the neck. Finger's calibrated path is unchanged.
Level-independent attack comparisons now separate every pair at maximum
velocity, and the measured pick/finger contrast error improves in both
materials. Thumb remains an authored style because the real dataset merges
finger and thumb labels. See [picking evidence](picking-styles-2026-09-08.md).

The expanded shape range changes air resonance, modal frequency, damping and
bass response. All six shape pairs separate more strongly after independent
note-level normalization. Its recording comparison must be kept separate
from the earlier contact/calibration checkpoint:

| Dry-note split | Task-start engine | Current wider shapes |
| --- | ---: | ---: |
| Training, 83 examples | 6.441610 | 6.355798 |
| Development validation, 24 examples | 6.525011 | 6.567155 |
| Independent flat-top steel, 8 examples | 9.263118 | 8.791977 |

The flat-top score improves about 5.1%, while the development score worsens
about 0.65%. This is mixed evidence, not a universal realism gain. These
renders use Original/Dreadnought and Finger, so they do not score the new
named guitars or the distinct Pick/Thumb styles. The matched Yamaha/Walden
calibration uses Auditorium; its earlier contact/calibration evidence remains
applicable to that unmodified Finger/reference-shape path.

A separate six-player GuitarSet performance replay also gives mixed evidence.
Mean log-magnitude error changes from 13.148 to 13.541 dB, spectral convergence
from 0.91994 to 0.91573, and chroma distance from 0.31774 to 0.30546. Lower is
better for all three. This corpus uses known string/onset annotations but
unknown gesture and velocity; it does not independently test automatic
strumming. Frozen binaries and complete per-performance results are in
[the performance comparison](realism-performance-comparison-2026-09-08.json).

## Solver choice and remaining limits

The real-time implementation retains its reciprocal six-string waveguide and
measured modal bridge. This work adds continuous contact integration, a
corrected power observer, newly fitted positive-real body banks, and contact
styles tested against recorded notes. The review includes DAFx26 and the
2025–2026 regulated-SAV literature. A local nonlinear modal replacement is
not promoted: the earlier isolated SAV candidate had energy stability but
failed acoustic gates, and the newest paper does not supply a validated
shared six-string real-time implementation to substitute directly.

[The research review](physical-modelling-review-2026-09-08.md) records those
mechanistic limits and a possible future experiment. No claim of being the
most advanced or most realistic instrument follows from publication recency,
a larger mode count, descriptor scores, or green tests. Listening preference,
matched piezo calibration, fully coupled local nonlinear strings and a
measured stereo pair for each steel guitar remain outside the demonstrated
results.

## Final local verification

The final source passes Engine (127.77 s), Release (45.91 s), Capture,
CalibrationRenderer, generated Gaussian-data verification, and the separate
GuitarModels suite (3.00 s). The plug-in processor, state migration and editor
layout tests pass; default, minimum-size and restored-Bellido screenshots
were visually checked. Twelve demos were regenerated, and the standalone was
rebuilt and reopened for audition.

The [final callback audit](callback-benchmark-2026-09-08.md) includes all six
model/material combinations and 108 event/rate/block configurations. All p95
measurements meet their native-engine deadlines. At 96 kHz the worst p95 uses
40.0% of a 64-frame block and 25.7% of a 128-frame block. Desktop scheduling
outliers occurred in both baseline and current runs; this does not guarantee
glitch-free 32-frame hosting. Isolated Gaussian initialization optimizations
preserve all 36 compared audio hashes and reduce callback cost.
