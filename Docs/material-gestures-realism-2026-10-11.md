# Materials, stopping position and continuous hand gestures

This candidate builds on merged `fd17dd2` and the previously approved physical
hand. The user asked to implement further improvements, then expanded the
audit beyond a fixed count and specifically requested more realistic open
versus fretted strings. Parameters, ranges, defaults, layout, presets and
host latency are preserved. This is a new listening candidate; approval of
the preceding version is not a listening verdict on these changes.
The subsequent Wood audit adds a material response correction and a separate
fixed-performance listening comparison.

## Changes and expected audible effects

| Mechanism | Correction | Expected effect |
| --- | --- | --- |
| Plain steel stiffness | B and high E use their actual steel-wire section, while wound strings retain the approved effective-core correction. The attack-tension calculation uses the same section. | More accurate partial spacing, increasingly apparent high on the neck. Fundamental tuning is still compensated. |
| Shared hand travel | Shared posture displacement is expressed in metres, then divided by each string's actual speaking length. Independent contact variation is retained. | Coherent changes in contact colour across a mixed-fret chord; fretting no longer shrinks the physical movement of one hand. |
| Continuous posture | A deterministic, bounded C1 trajectory in elapsed time replaces note-triggered diffusion and the abrupt two-second rest redraw. | Connected phrasing through rests. Adding an ornament does not change the later hand trajectory. |
| Fractional damping contact | A fractional spatial gather and its adjoint reaction replace rounded contact taps and rounded string period. | Smooth changes in which partials the hand damps as its location or string length changes. |
| Release brush trajectory | The existing wound-string brush's frequency follows relative slip speed and its duration follows the inverse. Longer brushes are compensated to retain the existing force-envelope budget. | Slow lifts are longer and darker; fast lifts are shorter and brighter, continuously. |
| Physical open/fretted endings | Actual speaking length determines nut versus fretting-hand release, with a smooth finite-contact transition. | An open note slid up the neck is stopped by the fretting hand; a stopped note slid to the nut gets the open-string ending. |
| Physical vibrato eligibility | The same actual stopping geometry enables fretting-hand vibrato, independent of the original MIDI fret. | Vibrato engages when the finger stops the string and withdraws continuously on returning to the nut. |
| Wood mass and stiffness | Representative density and orthotropic stiffness drive coupled air/top modes, modal participation and broadband bridge conductance. | Material-dependent resonance balance and string decay through both microphones and piezo, without an arbitrary species EQ. |

Conventional and MPE-manager bends represent slides in Acustra. MPE-member
bends represent tension changes at a fixed fret and do not move the hand's
stopping regime. Natural harmonics retain the open-string catch.

## Physical basis and authored approximations

For a plain steel cylinder, `EI = E*pi*d^4/64` and
`B = pi^2*EI/(T*L^2)`. Thus halving the speaking length quadruples B; changing
tension at fixed length changes B inversely. Effective wound-string bending
stiffness cannot be inferred by treating its full outside diameter as solid
steel. [Woodhouse et al., 2012, equations 1–3](https://www.lam.jussieu.fr/Publications/Articles/AAA_Woodhouse_2012.pdf)
supports this distinction. The shipped wound fit is retained.

The shared travel amplitude retains the existing open-string reference:
`0.02 * 0.70 * 0.648 m` times bounded posture. Independent variation retains
its existing relative spread. The existing pluck-position knee and reflection
remain, as does explicit MPE control precedence. A smooth bound handles
virtual slides shorter than a realistic guitar string; ordinary frets use
the exact metre conversion. The existing static point compliance converts
the changed position into displacement at unchanged playing force.

The posture uses counter-hashed bounded targets at one-second intervals and
shape-preserving cubic Hermite interpolation. Its position and velocity are
continuous; it cannot overshoot its target bounds. The one-second bandwidth
and variation amplitudes are authored motor approximations, not measured
physiological constants. Independent per-contact residuals still vary. First
contact calibration, simultaneous snapshots, queued-stroke ownership, reset
and panic remain explicit contracts.
The smoother trajectory reduces the measured long-run posture variance from
the old uniform target's one-third to approximately 0.265; it does not retain
the old random walk's full marginal distribution. Existing variation and
correlation guards pass without changing their thresholds.

For the damping contact, let the unnormalized signed interpolation vector be
`g = [1-a, a, -(1-b), -b]` and `q = |g|^2/2`. Gather with `g/sqrt(q)`, use
contact strength `r*q`, and distribute the reaction with the transpose of
the gather operator. The signed basis has squared norm two, so the existing
relaxing two-port's wave-velocity plus storage-energy bound extends to all
four spatial samples. The strength adjustment preserves the unnormalized
reaction instead of amplifying fractional positions. The supports remain
disjoint at the endpoint limits. This is a fixed-contact passivity result;
the existing moving-hand withdrawal work remains separately bounded.

Winding crossings follow `frequency = sliding speed / winding pitch`.
[Pakarinen, Penttinen and Bank, 2007](https://aaltodoc.aalto.fi/bitstreams/475276ab-f408-4ef4-a46f-5528bb8ca63d/download)
measured handling-noise frequency and level changing with speed. The present
short key-up brush approximates an oblique pad slip. Its existing authored
centre frequencies and nominal durations remain the reference; neither
absolute winding pitch nor finger speed is claimed to have been measured.
The existing bounded release-firmness map supplies relative speed. Nominal
or missing release velocity is unchanged. Rack SDK 5 encodes note-off as
zero note velocity and supplies no independent release velocity, so this
particular speed-dependent branch is available only where the front end
provides explicit release velocity. Rack uses the nominal brush.

Near the nut, `w = smoothstep(clamp((0.648-L)/0.018, 0, 1))` uses the existing
18 mm authored pad inset. Release loss rates add:
`1/T60 = (1-w)/1.25 + w/0.16`, before the existing release-velocity scaling.
Contact location, strength and noise coefficients interpolate continuously,
while pure open/fretted endpoints retain their existing values. The contact
centroid during this transition is an effective soft-pad approximation, not
two literal hands moving into each other. Noise draw scheduling retains key
identity so moving a wheel does not switch subsequent random history.
Vibrato uses the same weight and retains its existing pressure, onset, rate
and depth limits. Requested geometry is evaluated before caching, so a
wheel event cannot use stale eligibility.

The [Wood report](wood-materials-2026-10-11.md) records the material inputs,
coupled-model assumptions and audit of the user-supplied articles. Wood
previously changed four authored factors and omitted material mass from
bridge residues and broadband conductance. The new model retains the
measured/reference damping and preserves the Spruce construction exactly
before construction level recalibration.

## Further findings

The capture audit found an invalid aliasing-test reference. An isolated
output-stage clipping reference cannot validate the whole preamp after its
input clamp diode conducts: the diode changes the circuit's subsequent
state, even when the host-rate output samples look unchanged. The isolated
test now samples the physical interval between output-stage clipping onset
and input-stage overload and asserts zero input-clamp activity. Its -60 dB
absolute limit is retained. The existing 1.5 dB improvement guard applies
to the upper half of that interval; near onset both methods are already
quieter than the limit, and smoothing need not improve the almost-unclipped
reference. The old overdriven cases remain finite-output circuit checks;
their residual is not labelled aliasing. Removing the antialiasing correction
fails the improvement guard. Independent continuous-circuit tests remain.

The updated string and bridge response can saturate the existing physical
9 V piezo preamp at maximum-velocity, exact-bridge Pick extremes. Across
four Shapes, three Woods, three Touch values and two sample rates, all 72
cases remain finite and within the physical follower bounds; 45 exceed the
linear output-stage headroom and 41 briefly hit the input stage. The worst
Dreadnought/Maple/Touch 0 case has -1.676 dB predicted linear headroom and
a -29.57 dB total nonlinear stage residual relative to a verified linear
half-drive reference. This includes intended saturation and is not an
aliasing measurement. The preceding model already overloaded mildly at
some extremes, but these new cases are stronger. Output loudness trims
cannot remedy internal saturation. The circuit and physical force units
are preserved; the listening review includes these extremes explicitly.

Construction calibration also assumed the default reference gain could
never change in any capture, although its own fitter recalibrates the
default Piezo gain. Relative strum predictions now include both the cell's
gain change and the default capture's gain change. The existing ±1 LU
phrase and ±9 dB strum limits, stereo reference gain and headroom budget
remain. Invalid constraint runs save their measurements without writing
the generated header, and generating and verifying a header require
separate rebuilt-renderer runs.

The audit does not justify an arbitrary extra nut/fret low-pass or a claim
that every fretted note must decay faster. Neck admittance can change
individual partial decay and requires measured input: see
[Paté, Le Carrou and Fabre, 2014](https://www.lam.jussieu.fr/Membres/LeCarrou/Articles/A8_Pate_PredictingDecayTime.pdf).
The existing approved decay calibration is retained. Finite-width damping
regions are another supported direction, but require a distributed contact
and separate calibration rather than enlarging an interpolator. The
[DAFx articulated slide-string model](https://dafx.de/paper-archive/2020/proceedings/papers/DAFx2020_paper_68.pdf)
provides a useful physical reference.

The curved strum's local speed could affect edge-release time. Reintroducing
absolute hand speed was previously rejected in listening/reference work,
and the correct relative-motion projection is not established by the sweep
geometry alone. No speculative speed correction is included here.

## Reproducible listening

`Tools/RenderMaterialGestureReview.cpp` supplies nine eight-second scores:
same-pitch open/fretted comparisons, plain strings up the neck, wound
controls, mixed-fret hand motion, short endings, phrases with rests,
continuous release velocity, slides to/from the nut and fretting vibrato.
The renderer uses the real Performer at 48 kHz, 64-frame blocks, dry stereo
microphones and native gain. Each pair compares the complete baseline and
candidate; it is not an isolated mechanism ablation.

`Tools/BuildMaterialGestureReview.py BASELINE_SOURCE CANDIDATE_SOURCE NEW_OUTPUT`
freezes both DSP trees and the same renderer, saves raw floats, hashes and
common-gain PCM audition files, and writes a local review page. No independent
normalization, EQ, compression or alignment is used. Measured difference is
not evidence of listener preference.

`Tools/BuildWoodReview.py PRE_WOOD_SOURCE CANDIDATE_SOURCE NEW_OUTPUT` uses
the renderer's `--woods` mode for the same notes and chord on each Wood,
through stereo, mono and piezo captures. The pre-Wood source is `26b5cef`,
so the hand and string model is shared. Native before/after also includes
the updated construction level calibration. Optional matching removes one
whole-clip RMS factor by constant attenuation; it is not an isolated
mechanism ablation. Source, executable and audio hashes plus exact gains are
included. Neither mode applies EQ or compression.

## Validation

The frozen final source passes the complete Release build, the C++17 DSP
smoke target and **80/80 CTest suites** (174.74 seconds). This includes seven
new physical contract suites and the existing construction, continuity,
allocation, sample-rate, capture-circuit and performance contracts. The
[receipt](validation-material-gestures-20261011/receipt.json) links the source
hashes, complete build/test logs, negative controls and listening manifests.

Independent checks include the steel section and attack-tension laws;
metre-based shared contact travel; continuous posture and contact energy;
brush traversal and force budget; stopping and vibrato geometry; and Wood
tensor integration, coupled-mode projectors, bridge residues and passivity.
Off-tree mutations reinstate old wire geometry, old stopping eligibility or
rounded damping contact, or remove material bridge conductance. The relevant
new tests reject them. Nominal release-brush and plain-string paths match
their baseline hash; Rack's missing release velocity therefore retains its
nominal behavior.

Fresh calibration with rebuilt renderers passes all 108 Shape/Wood/playing
technique/capture phrase cells within ±1 LU and all 180 native strum cases
within the existing ±9 dB range (observed -4.654 to +3.230 dB). The default
Dreadnought/Spruce/Finger microphone gain remains exact; the corresponding
Piezo trim increases 0.320 dB. The [calibration summary](validation-material-gestures-20261011/calibration-summary.json)
records both the valid checks and remaining output-headroom limits: all
microphone stress cases retain at least 1 dB below the safety knee, while
12/12 hardest Piezo Finger and 5/12 hardest Piezo Pick constructions cross
it. The existing Pick policy spends at most 0.9 LU on headroom and passes;
it does not promise every maximum-force output remains below the limiter.
This is separate from the internal preamp saturation described above.

Rack SDK 5 analysis and forbidden-global checks pass, with only the same
pre-existing analyzer warning as baseline. Wrapper parity, allocation traps,
48 construction-history cases, all 15 quality/rate combinations and eight
factory patches at five sample rates pass. In paired quiet measurements at
default 1x quality, 48 kHz and 64 frames, each version completes 92,250
callbacks with zero thread-CPU budget misses. Candidate mean workload cost
changes range from -0.4% to +3.6%; its worst thread-CPU callback is 1158.9 µs
against the 1333.3 µs period. Scheduler-preempted wall times exceed the period
on both versions, so this is not an absolute host-deadline guarantee.
SDK allocation grows 1872 bytes. The previous optional 4x deadline failures
remain open. The [Rack integration report](../../Docs/MATERIAL_GESTURES_2026-10-11.md)
retains module hashes and all raw measurements.

The [local listening review](http://127.0.0.1:8771/) contains nine final
gesture pairs, eighteen Wood clips with native/matched playback, and the
paired extreme-pluck microphone/piezo captures. The gesture pairs have native
peaks 0.068–0.183 and one common unity gain per pair; Wood clips peak
0.145–0.194, with matching gains 0.897–1.0. Both manifests' candidate source
hashes exactly match the tested DSP. Extreme playback uses one common
0.092 dB attenuation across all files and retains the instrument's existing
limiter. No listening preference or inaudibility verdict is asserted.
