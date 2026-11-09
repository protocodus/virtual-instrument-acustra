# Pick release direction and the 1.9.36 review pass — 2026-11-09

The source is 1.9.36, compared with 1.9.35 (`87d5227`, this branch's base).
All renders are 48 kHz in 256-frame blocks through the shipping Performer
(`AcustraRenderEventList`), with a fresh instance per render, unless
stated. Nothing here is a listening verdict. The A/B set described below
is waiting for the owner's ear.

## The defect

A plectrum leaves the string with a velocity as well as a displacement
(`pickReleaseVelocityShare`, FittedPhysicalData.h). `writePickRelease`
writes the rest displacement `D(phase)`, slips it through the edge's
one-pole, and adds a velocity box `V(phase)` between the folded contact
points, with height `h` relative to the rest. 1.9.35 solved
`h^2 V + 2 h X = share D` on the grid and took the root that vanishes with
the share. That root's sign follows the sampled cross term `X`. The source
said the sign was inaudible because the two waves are in quadrature.

They stop being in quadrature once the slip is applied. The slip is the
anticausal `1 / (1 - j omega tau)` of a force that unloads as `e^(t/tau)`
up to release. It leads each partial of the rest, so at release the slipped
rest already moves toward rest, and the sign of `h` becomes audible. In the
rest frame, with the line moving toward larger phase, the box gives the
contact a velocity of the displacement's sign exactly when `h / rest > 0`.
A tip carries the string the way it pushed it, so that is the physical
direction. `X` is mostly negative, so 1.9.35 released most picked notes
moving back toward rest. Where the slip moves `X` through zero as the
stroke hardens, the hump turned round between adjacent velocities:

| Jumbo Steel Pick, fresh instance | 1.9.35 | 1.9.36 |
| --- | --- | --- |
| C6, 5-80 ms centroid, v78 to v80 | 4049 to 2441 Hz | 3961 to 4009 Hz |
| C6, 5-80 ms centroid, v48 / v127 | | 3133 / 4854 Hz, rising at every step scanned |
| C6, 0.1-0.6 s centroid, v78 to v80 | 1614 to 2250 Hz | continuous |
| B5, 5-80 ms centroid, v84 to v86 | 5170 to 3765 Hz | continuous, 5587 Hz (v88) to 6181 Hz (v120) |

Bisecting the realism switches put the step on `pluckBoundaryCompliance`,
because that switch moves the slip. Printing `X` in the 48 kHz slip branch
showed it crossing zero at the step.

## The change

`pickReleaseDirection` (PerformanceRealism.h, on) takes `h = sqrt(share D / V)`:
the physical sign always, and the hump's own kinetic energy equal to the
fitted share of the pluck's, which is how FittedPhysicalData.h defines the
share. No root of the old equation has that sign and vanishes with the share
when `X` opposes it, so the cross term stays in what is written. The attack
pitch cue's slope energy on the 44.1-192 kHz reference grid now uses the
written energy `D + h(hV + 2X)`. At 48 kHz it was already read from the
written loop. Finger and Thumb never enter this code, and their renders are
byte-identical to 1.9.35 on all five Finger/Thumb presets.

At a unit share, the line less its rest carries the displacement's own
energy (1.002-1.005 at MIDI 40/52/64). The whole line now carries 1.75-1.86
times it, where 1.9.35 forced 2.0. The outward velocity opposes the slip's
own motion toward rest.

## Measurements

The keyboard survey covers MIDI 40-86 on every string within 20 frets of
an open string, at velocities 16, 64 and 112, with Room 0 (`MeasureFretboardSweep`
metrics). The attack centroid's rise from v16 to v112:

| Preset | 1.9.35 median (range) | 1.9.36 median (range) |
| --- | --- | --- |
| Jumbo Steel Pick | +1104 cents (169 to 1838) | +1348 cents (737 to 1782) |
| Palm Muted Steel Pick | +983 cents (571 to 1902) | +1215 cents (678 to 1758) |
| Piezo Stage Steel | +1415 cents (741 to 1628) | +1487 cents (1062 to 1881) |

The sustain span from v16 to v112 moves by 0.5-0.6 dB on each Pick preset.
The 2-6 kHz T60 median rises by 0.1-0.6 s. The level slope against pitch
is still flat (Jumbo v112: +0.03 dB/oct before, +0.17 after), so the Known
gap about the Pick's level against pitch stays open. The neighbour flags
that are larger than 3 dB are the same notes before and after: the measured
bodies' documented structure at Dreadnought C5/C#5 and Auditorium
G#3/G#4/C#5/D5, plus Jumbo B5. Jumbo B5's flag fell from -5.5 to -3.5 dB at v112.

Output (`Acustra.OutputLevels`, the median of 27 cases, in VU):

| Preset | 1.9.35 | 1.9.36, old Output | 1.9.36, Output re-trimmed |
| --- | --- | --- | --- |
| Jumbo Steel Pick | -9.211 | -10.027 | -9.234 (Output -0.9 to -0.1 dB) |
| Palm Muted Steel Pick | -9.673 | -10.624 | -9.635 (Output 4.5 to 5.5 dB) |
| Piezo Stage Steel | -9.188 | -9.229 | unchanged |

The Rack device's factory patches mirror these Output values. They take
the new values when the Rack's submodule pin advances.

## Contracts

- New test, `Acustra.VelocityRealism`: through the Performer, Jumbo Steel
  Pick's B5 and C6 attack RMS frequency, from velocity 60 to 120 in steps
  of 2, may never fall by more than 5% from one step to the next. 1.9.35
  fails it, with a worst fall of 25.8%. 1.9.36's worst fall is 0.67%.
- `Acustra.Engine`, `testAPlectrumReleasesWithVelocity`: re-derived. It
  checked the whole line at 2.0 times the displacement, which is the old
  law. It now checks the velocity's own energy at 1.0, within the same 1%.
- `Acustra.PickRoughness`: the restrained burst's median change to the early
  4-16 kHz band, in the shipping pluck, moved from -0.68 dB to -0.47 dB.
  Each of the twelve cases is still below -0.1 dB. The outward velocity
  fills more of that band on a loud stroke. The bound was re-derived from
  -0.5 to -0.4 dB, and a new check holds the burst's own change, measured
  against the string with no release velocity, to -2.5 dB. That change
  measures -3.17 dB on both versions. This bound was loosened, and the owner
  can veto it with the candidate.
- `Acustra.RepeatedPluckRealism`: the rapid-tremolo check failed once, at
  44.1 kHz on a picked C4 at 12 strokes a second (strokes 24-31 held 1.54
  times the energy of strokes 8-15, against 1.5; 1.9.35 had 0.65). That check
  reads where a beat falls, not whether energy accumulates. Scanning the
  stroke rate, 1.9.35 itself fails it at 12.2 Hz (1.64 at 44.1 kHz, 1.70 at
  48 kHz), and with the merge's work bound removed (`repluckForceGain` held
  at 1) it passes at 12 Hz (0.77) while the train holds 1.4 times its
  strokes' own energy. Over 120 strokes the candidate's train settles to
  1.9.35's level by about stroke 56. The check was re-derived against the
  passive bound the merge enforces: strokes 24-31 may hold no more energy per
  stroke than the sum of the isolated stroke's energy over the train. The
  candidate's worst case is 0.79 (48 kHz, C4, 12 Hz) and 1.9.35's 0.41 (the
  unchanged Finger E2 at 192 kHz). The bound-free merge fails it at 1.42.
  The audible consequence: a tremolo whose rate nearly divides the note's
  partials swells for about two seconds, where 1.9.35's train settled
  within one.

## Tried and not kept: a plane split that grows up the neck

A termination that is stiffer in one plane moves that plane's effective
end by about T/k, a fixed length, so the split in cents would scale as
open length over speaking length (1.5 cents at the 12th fret). That
contradicts three guards. `ChorusRegression`'s E3 H5 at the 12th fret
beat 9-22 dB against a 2 dB bound. That is the per-plane end-delay chorus
the guard was kept for on 2026-10-30, when the owner chose 0.75 cents by
ear. B4's settled pitch moved -5.2 cents through the coupled pair. A
manager/member bend split spread 0.050 cents against 0.05: the delay
followed the length and the tuning solve followed the pitch. The candidate
was reverted, and the 0.75 cent split stays.

## The attached leads (written against 1.9.35)

1. Velocity-to-brightness: the share is a ratio to the pluck's stored
   energy and itself rises as `v^0.86`, so a soft stroke gets the smaller
   share, not the larger one. The quoted Finger `releaseBoostLimit`
   clamp is not in the source. The Pick's brightening did rise, through the
   direction fix above, not through a refit.
2. The contact burst already scales with velocity through `effectiveTouch`
   (`Touch + 1.10625 (v - 0.5)`), about +10 dB from v16 to v112 relative
   to the string.
3. Take-to-take variation was set by ear on 2026-10-30 and 2026-11-06 and is
   left as it is.
4. Two-stage upper decay is the Known gap on partial envelopes. Raising the
   bridge's high conductance moved it little (README). It is not addressed here.
5. `steelFretT60Slope` is -0.0598, fitted, and it sits on its bound. The
   quoted 0.0150 is not the shipping value. Moving it would fit a number by ear.
6. The 0.75 cent split is the owner's by-ear choice. A length-scaled split
   was tried above and fails the guards.
7. The Finger easing models the hand moving toward the soundhole up the neck.
   A plectrum's contact stays at its distance from the bridge, which the
   1.9.35 audit also concluded. The share is dimensionless, so it adds no
   `1/L`. The Pick's level against pitch stays a Known gap.

## CPU

Callgrind, 2.5 s of a Jumbo chord and two notes, Room 0.1:

| Function | Share of instructions |
| --- | --- |
| `evaluateStringAxialStretch` | 48.9% |
| `process` | 18.7% |
| `BridgeLoad::advanceModes` | 8.6% |
| `BodyBank::render` | 8.3% |
| `RoomAmbience::step` | 5.5% |
| Capture filter | 1.35% |
| `renderFretMotion` | 1.3% |
| Complex division | 1.0% |

The axial stretch integrates every cell of both loops on every sample of a
played voice. Its design is already cached on exact keys. Its cross terms
leave no exact recursion, and any reordering of the sums changes the output
bits. The change in this pass runs once per pick contact, so it costs no
measurable CPU. The `Acustra.CpuBudget` and `Acustra.EnginePerformance`
results are in the final section.

## Final checks

The `unit` and `audio` tiers (125 tests, Python tool checks included) pass
after the `RepeatedPluckRealism` repair above, and `AcustraDSPCxx17` builds.
The README's local technique renders complete (10 files). The `perf` tier
passes: `Acustra.EnginePerformance` measures a six-string real-time ratio of
0.125, and every factory demo in `Acustra.CpuBudget` is under its 0.28
budget.

`CpuBudget` against 1.9.35, two alternating rounds of three repeats each on
the same machine (gcc 13.3, 4-thread Xeon at 2.1 GHz, 48 kHz in 256-frame
blocks), median real-time factor:

| Preset | 1.9.35 | 1.9.36 |
| --- | ---: | ---: |
| Dreadnought Steel | 0.128 | 0.118 |
| Parlor Steel Fingerstyle | 0.095 | 0.086 |
| Mahogany Steel Thumb | 0.095 | 0.107 |
| Jumbo Steel Pick | 0.110 | 0.116 |
| Palm Muted Steel Pick | 0.175 | 0.165 |
| DADGAD Mono Mic | 0.122 | 0.111 |
| Open G Slide-ready | 0.121 | 0.110 |
| Piezo Stage Steel | 0.123 | 0.121 |

The Finger and Thumb presets compute identical samples in both versions and
still move by -9% to +13%, so that is this machine's noise, and the Pick
presets' changes are inside it. Palm Muted Steel Pick's worst block still
exceeds its period (2.1-2.5 times in both versions). That is the inherited
limit the 1.9.35 CPU audit traced to the damped-pole solve re-running while
the hand's loss smooths after a CC2 change
([cpu-optimization-2026-11-09](../benchmarks/cpu-optimization-2026-11-09.md)).
1.9.37 removes it ([palm-tuning-cpu-2026-11-09](../benchmarks/palm-tuning-cpu-2026-11-09.md)).
