# Acustra keyswitch and strumming repair — 2026-11-02

> Written on a branch off 1.9.13 that numbered this work 1.9.14 and 1.9.15.
> Those versions reached main on 2026-11-05 as 1.9.20 and 1.9.21, after
> main's own 1.9.14 to 1.9.19; the figures here are from that branch.

Baseline is canonical `edeeed8` (1.9.14), retained as a frozen native library
with SHA-256 `56b795eb147a77aeb8923026b8e5f1dad7af3927d63cedcc3e5e15149b719c00`.
The candidate is 1.9.15. The Reason adapter separately follows as 1.9.15d1.
SDK authority is JukeboxSDK_500_028, TargetVersion 5.0.

## Printed contract and reproduced failures

The existing rear legend states C0 Downstrokes, C#0 Upstrokes, D0 Automatic
and D#0 Sustain Toggle, respectively MIDI 24/25/26/27 in Reason's naming.
Those note numbers agree with the canonical constants and wrapper. F0/F#0
(29/30) are the existing held-key strum controls; the rear legend omitted
them. They are now added without changing any existing key's meaning.

The new canonical regressions fail 23 checks against the frozen baseline:
explicit F#0 single-string direction under Finger/Thumb (and Pick with
automatic picking ablated), two-note forced-direction sweep/direction, and
latched C#0 picked-single direction with automatic picking ablated. Existing
StrumDirection passed before those missing cases were added.

The correction keeps two-note forced chords in the swept path and gives
explicit one-string strum gestures their direction independently of automatic
picking. Ordinary Finger/Thumb singles retain their original behavior. Tests
cover repeated F0/F#0, 1/2/4 held notes, all three techniques, automatic
picking enabled/disabled, live staggered fretting and single key-up ownership.
Only new endpoint-isolation fixtures turn scatter off; existing production
scatter coverage remains. No signal lengths or tolerances are reduced.

## Separate Rack event-order defect

At default 2x quality and 64-frame batches, entering Keys mode and sending
C0/C#0/D0 with a new note at frame 17 or 63 left that new note silent. The
same events one frame apart worked; direct Performer mode-first events at
the equivalent 96 kHz offsets also worked. CV gate strikes reproduced it.

Direction keys are now applied before the frame's MIDI/CV strikes. Their
idempotent setters are replayed in original control order to preserve mixed
F0/F#0 versus direction-key ordering. A first repair that skipped this replay
failed the added mixed-control regression; that failed run is retained.
D#0 remains in the original sustain/release path and is not preapplied.

The wrapper regressions cover 24 MIDI/CV exit cases (all three direction
keys, both diff orders, frames 17/63) and 24 mixed strum/direction cases.
They include actual asleep wake at the last frame, control-only silence,
key ownership, the next key's final-mode behavior and existing D#0 release
ordering. Both focused wrapper routes pass in the pre-integration probe.

## Validation record

The first full functional run passes 113/114 tests. Performer exposes a stale
fixture that calls MIDI 30 an unreachable pitch: 30 is now F#0, the explicit
up-strum control. Its former single-string direction bug happened to hide
this fixture error. The fixture now uses MIDI 32 instead, retaining both
unreachable notes, the same signal duration and the original equality check.
The failure log is preserved and the suite is rerun after that fixture repair.

All **116 registered native gates have passing final results**: the 113 passes
from the first full functional run, the repaired Performer rerun (43.45 s),
and both performance gates. This is cumulative final coverage, not a claim
that the first complete run was green. A fresh configuration also registers
the existing FretUpperReviewAnalysis self-test, beyond the prior 115-test
ledger. No test is skipped, disabled, shortened or given a larger timeout
or looser tolerance.

The focused native StrumDirection gate passes in 3.00 s at its unchanged
120-second limit; the full functional run also passes it in 2.62 s. The
Release build includes the C++17 DSP/Performer target. EnginePerformance
passes at 0.0306885x realtime versus its 0.25x bound. The eight-preset native
CPU gate passes at median 0.0337–0.0375x realtime versus 0.28x, 48 kHz and
256 frames with three repetitions on Apple M1 Max. Its largest observed
block is 15.1% of the callback deadline. These current-machine measurements
are not a CPU-optimization claim or Reason/target-device timing.

Fresh SDK 5 Deployment arm64 modules also compile from frozen baseline and
candidate sources without installing into the user's profile. The adapter's
integration record owns the final pin, complete Rack suite and paired
default-2x SDK-native timing evidence; native CMake results do not substitute
for that adapter coverage.

## Audio evidence and scope

An eight-second, 52-event native Performer comparison covers Finger/Thumb
single-key F#0 strokes and C#0 two-note chords at 48 kHz stereo, 64-frame
blocks, fixed seeds, default acoustic controls and no Gather Chords. One
renderer object links separately to the frozen baseline and candidate
libraries. Event manifests are identical. Native float captures are retained;
lettered PCM24 WAVs use one whole-file stereo-RMS trim: A 0 dB and B
-0.065092471 dB. Matched RMS difference is below 0.000001 dB and neither
file clips. No listening preference or installed-Reason recording is claimed.

This repairs the stated key behavior. It does not retune the acoustic
engine, introduce browser-preview extrapolation, or change MIDI IDs,
parameters, quality defaults or declared host latency. No package, portal
acceptance or installed-Reason result is inferred. The repository's existing
restriction against running Recon in the shared user profile is retained.

Evidence is outside Git under `diagnostics/acustra-keyswitch-20261102/` in
the shared workspace. It includes frozen baseline inputs, failing and passing
regressions, build logs, panel receipts and the final integration record.
The previous 16× browser preview stays presentation-only. No PR is merged.
