# Fret and upper-register validation — 2026-11-02

> Written on a branch off 1.9.13 that numbered this work 1.9.14 and 1.9.15.
> Those versions reached main on 2026-11-05 as 1.9.20 and 1.9.21, after
> main's own 1.9.14 to 1.9.19; the figures here are from that branch.

Follow-up: both timeout gates were subsequently resolved without changing
their limits, cases or tolerances. [The goal follow-up](fret-upper-goal-2026-11-02.md)
records the final 115/115 passing coverage. The initial results below remain
as the original evidence, including its failed runs and diagnostic caveats.

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

All targets in the JUCE-free Release build compile, including normal and
extended-rate C++17 DSP. The full engine contract, FretMotion, WoundSlide,
PadReleaseCorner and ShiftArrivalAudibility pass. The review analyzer's
independent synthetic checks and self-test registry pass.

All 115 registered native gates were exercised. Their final formal status
is **113 passed, two timed out: BodyShape (90 s) and VelocityRealism (60 s)**.
This is not an all-green result. No deadline or numeric tolerance was raised.
The initial concurrent non-perf run completed 108 cases: 100 passes and eight
timeouts. Six of those timed-out cases pass in the subsequent serial runs:
ConstructionHistory (198.45 s), StringPitchRealism (38.87 s), PalmMutePitch
(40.42 s), StrummingRealism (42.55 s), ReleaseRealism (33.90 s), and
PhysicalHandRealism (43.90 s).

The retained serial controls show why wall time and model correctness must
remain separate. The old BodyShape baseline completes in 81.48 s, with
45.22 s of child CPU; the candidate's CTest attempt reaches 90 s with only
30.56 s of child CPU accumulated. PalmMutePitch's baseline times out at 60 s,
while its candidate completes in 40.42 s; its changed DSP paths do not execute
in that fixture. ReleaseRealism completes on both, baseline/candidate
34.21/33.90 s and child CPU 21.92/21.79 s. VelocityRealism's old baseline also
reaches its 60 s limit. These are separate measured controls, not a proof
that every earlier timeout was scheduling interference.

Additional full-run diagnostics collect the remaining signal assertions and
CPU time with a separate safety watchdog. They explicitly grant **no CTest
gate credit** and do not replace the failed 90/60 s gates. BodyShape completes
with zero assertion failures at 97.81 s wall / 45.89 s child CPU.
VelocityRealism completes all assertions on both versions: baseline
157.72 s wall / 96.33 s child CPU; candidate 99.21 s wall / 89.36 s child CPU.
Even the baseline's measured CPU time exceeds its 60 s formal limit on this
machine. These are completion diagnostics, not a claimed optimization
benchmark or a passing replacement for the failed time gates.

Both perf-tier gates pass with other task builds and renders idle. On the
Apple M1 Max at 48 kHz, the six-string engine ratio is 0.0987x realtime
(gate 0.25x). The eight-preset benchmark uses 256-frame callbacks, three
repetitions, and a 0.28x median budget: every preset passes at 0.1023–0.1146x.
The maximum observed block uses 1.266 times its 5.33 ms deadline, so this is
not evidence of zero deadline misses. These are native development-machine
figures, not Reason scheduling or translated-target CPU measurements.

The separate Rack integration's complete eight-stage SDK 5 suite passes:
declarations, all eight patches, pinned panel geometry, C++17 compilation,
demo renders, output-rate handling, wrapper host contracts, and all-patch
render/release/silence at 44.1/48/88.2/96/192 kHz. The task-scoped canonical
pin/version/source-ownership check also passes. All 15 host-rate/quality
combinations retain their declared event and output contracts.

Raw evidence is retained in the workspace's
`diagnostics/acustra-fret-upper-20261102/`: original failed runs, serial
comparisons, child CPU measurements, completion diagnostics, both perf
results, all Rack checks, source/tool/binary hashes and 234-case comparisons.
`final-check-summary.json` preserves each formal gate's result history and
does not turn diagnostic completions into passes.

This source iteration does not claim an installed Reason test, a translated
U45 package, a plug-in build or a portal release. The two formal timeout
gates remain open; no PR is merged.
