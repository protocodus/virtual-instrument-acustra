# Fret and upper-register validation — 2026-11-02

Baseline: canonical `73665a810f17a07a2dff0bc6cb4adbc2b54284b1`, Acustra 1.9.13.
Candidate: Acustra 1.9.14 on `codex/fret-upper-20261102`.
Native JUCE-free Release build, AppleClang 21.0.0, C++17 DSP compatibility,
native architecture, Python 3.11 with NumPy 1.26.4/SciPy 1.14.1. Jukebox
authority for the separate Rack integration is `JukeboxSDK_500_028`,
`TargetVersion=5.0`. These measurements are native engine evidence.

## Paired render result

234 fixed-score cases per snapshot at 44.1/48/96 kHz, unchanged native level,
64-frame blocks. Exactly matching manifests; raw float stereo retained.
The [research note](../research/fret-upper-refinement-2026-11-02.md) defines
the observation windows and their limitations.

| High frets 14–20 | Median H2–H6/H1 change | Median centroid change |
| --- | ---: | ---: |
| Finger, velocity 40 | +0.381 dB | +22.55 Hz |
| Thumb, velocity 40 | +0.337 dB | +22.45 Hz |
| Finger, velocity 100 | +0.072 dB | +21.16 Hz |
| Thumb, velocity 100 | +0.037 dB | +10.53 Hz |
| Pick, both velocities | 0; byte-identical | 0 |

Soft-note upper-partial early-to-late fall changes +0.710/+0.493 dB/s for
Finger/Thumb, and the velocity-100 changes are near zero. This composite
descriptor changes with initial spectral weighting; no change to intrinsic
string decay and no solution to the missing snap/mellow mechanism is claimed.

At 48 kHz, Release Noise 0.1, the arrival-window null relative to the phrase
changes from -52.650 to -40.605 dB for Pick barre shifts and -51.440 to
-39.408 dB for a Finger wound-string line. Their full-file RMS changes are
-0.00085/+0.01981 dB, respectively. The isolated-arrival test measures
+12.034 to +12.042 dB at capture across the three rates. Its source force
gain is +12.0412 dB, with unchanged random draws and linear amount response.

Plain/same-fret null floors are about -99/-90 dB at 10%; member bends add
zero winding noise. The open-string control includes old key-up residue
at -55.30 dB, unchanged within 0.003 dB. It demonstrates why a difference
against Release Noise zero is not by itself an isolated fret-noise measurement.

The short listening sets isolate the changes: the upper set uses plain
high-E notes at velocities 40/100; the fret set uses Pick barre shifts.
Whole-file stereo RMS is matched; native files are untouched. Trims and
the letter key live beside the set. No listener preference is asserted.

## Verification status

The final test record is completed after the broad native and Rack-adapter
suites finish. This source iteration does not claim an installed Reason
test, a translated U45 package, a plug-in build or a portal release.
