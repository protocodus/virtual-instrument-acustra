# Five physical-hand improvements — 2026-10-11

Implemented against Acustra `41f2c67`, the canonical instrument submodule used
by the active Rack Extension. The older standalone checkout was left intact,
including its existing repertoire-renderer edit. This deliberately retains
the newer approved string-loss, body, dynamics and gesture work.

The five proposals below are implemented without changing any saved parameter,
default, range, control mapping, layout, host latency, factory patch or body
coefficient table. The sound of existing performances intentionally changes.
These are physical consistency improvements and bounded performance models;
the user has now approved the final continuous candidate. This approval is
separate from the numerical validation and does not establish an independent
perceptual realism score.

## Implemented changes

| Change | Expected listening effect | Physical basis and limits |
| --- | --- | --- |
| 1. Continuous curved strum | Less uniform spacing inside a chord, while its beginning and total traversal remain intact | A projected wrist sweep follows `x = A sin(theta)` across the actual string positions. The existing measured overall speed is preserved. The 45 mm sweep amplitude is an authored assumption, not a measured player fit. |
| 2. Position and force move together | Repeated notes vary in level and colour as one consistent contact gesture | At fixed force, string deflection follows `L p(1-p)/T`. Existing small hand-position variations now change that compliance instead of silently changing the force. No additional randomness is introduced. |
| 3. Bent strings resist the hand | Pre-bent notes retain expression without acquiring excess excitation | The same modeled force produces less deflection as steel tension increases. The correction uses the existing core/stretched-string tension model. It never boosts a virtually slack string under extreme downward MIDI bends. |
| 4. Thumb roughness follows its release | A rounded soft-pad attack, especially on quiet strokes | The independent contact-noise bandwidth is bounded by the actual finite `r/u` release time. The approved Thumb periodic release and reference normalization remain unchanged by this coupling. |
| 5. The right hand stops at the right place | Picked open strings end under the heel; slide releases retain their physical contact character | Finger and Thumb return to their pluck point. Pick uses the existing modeled palm position, 28 mm from the saddle. Fretting and palm release noises now use actual speaking length after slides. Existing passive damping strength, release duration and the 1/32-note join rule remain intact. |

The strum curve moves interior arrivals by at most 1.39% of total traversal
(about 1.47 ms at the slowest nominal full-string sweep before the existing
common speed variation). The first and last arrivals stay exact. No runtime
trigonometry, additional per-sample filter or new delay line is required.

For change 2, the nominal point retains the authored force/velocity response;
the correction only couples the existing take offset to its local compliance.
The current bounded position range keeps the ratio finite. Change 3 applies
`T_nominal / T_bent` only when tension increases, rather than allowing the
model's numerical minimum tension to amplify a pluck.

For change 4, a sampled one-pole contact with pole `b` has mean delay
`b/(1-b)` samples. Two equal roughness-filter stages with pole `q` have mean
`2q/(1-q)`. Equating them gives `q=b/(2-b)`. The coupling only lowers the
existing corner and preserves DC gain; it adds no force or noise draws.

## Acoustic sources and authored assumptions

Jim Woodhouse's [plucked-string synthesis explanation](https://euphonics.org/5-3-synthesising-plucked-strings/)
describes the triangular displacement, contact rounding, and dependence of
the resulting harmonics and level on plucking position. The static small-slope
balance at a point is `F = T y (1/a + 1/(L-a))`, which gives the compliance
used here. The instrument's existing steel core and winding model supplies
the material-dependent tension and loss; this change does not invent new wood
or string measurements.

[Evangelista and Smith, DAFx 2010](https://www.dafx.de/paper-archive/2010/DAFx10/EvangelistaSmith_DAFx10_P21.pdf)
describe compliant finger/string interaction and passive pluck modeling.
They support using contact mechanics as the source of attack character; they
do not validate this instrument's authored effective radius or Thumb mapping.

[Freire et al., Sensors 2020, 20(19), 5722](https://doi.org/10.3390/s20195722)
study oscillatory guitar strumming with movement capture. The simple sinusoidal
sweep here is a bounded approximation. Neither the 45 mm amplitude nor the
existing 28 mm palm/18 mm finger offsets are claimed as universal anatomy.

## Before/after listening

Run the same renderer against two frozen DSP trees:

```sh
python3 Tools/BuildPhysicalHandReview.py BASELINE_SOURCE . NEW_OUTPUT_DIRECTORY
```

The generated local review is
`build-physical-hand-20261011/listening-review/index.html`.
It contains five eight-second pairs, 48 kHz stereo, dry microphone capture,
and the identical authored score on both revisions. Each study emphasizes one
mechanism, but each comparison is the complete baseline against the complete
candidate, not an isolated one-change ablation. The final study explicitly
sets existing Release Noise to 50%; its geometry correction does not create
release noise when that control is zero.

The package retains frozen DSP source, the common renderer, raw float WAVs,
PCM16 listening files and a hash manifest. One common gain is used per pair;
all five use unity gain. No independent normalization, EQ, compression, timing
alignment or limiting was applied.

| Study | Candidate RMS change | Largest raw sample peak across pair |
| --- | ---: | ---: |
| Curved strums | +0.206 dB | 0.514 |
| Contact position and force | +0.236 dB | 0.138 |
| Pre-bent string | -2.327 dB | 0.180 |
| Thumb contact | +0.008 dB | 0.125 |
| Picking-hand release | +0.106 dB | 0.158 |

All ten renders are finite and nonsilent. The pre-bent study is quieter
because the same force does less work against increased tension; its level
was not restored by an output gain. Every pair has a nonzero waveform change.
These measurements establish changed signals and headroom in these scores,
not a perceptual realism score or a preference. Browser verification found all
ten files decoded with eight-second durations and no media errors.

## Validation

Focused Engine, VelocityRealism, ContactRelease, PhysicalHandRealism,
Performer, StrummingRealism and ReleaseRealism checks pass, including the
strict C++17 compilation used by Rack. The regression witnesses are checked
against the old implementation as negative controls.

- Across 648 seeded contacts at 44.1/48/96 kHz, reconstructed force variation
  from changing only contact position stays below 0.001%; baseline worst-case
  force ratio was 1.43316.
- Positive pre-bend force error is at most `2.09164e-7` relative; the baseline
  fixture reached approximately 101% excess force at six semitones.
- Thumb roughness slows by at least 10% in 21 of 24 contact cases. Its periodic
  waves are exact against the coupling ablation, and 17/127-sample block
  partitions produce identical audio.
- Release contact tests cover picking techniques, physical slides and
  independent transport-delay geometry. The baseline fails these new cases.
- The previous uniform strum scheduler fails seven new trajectory assertions;
  existing ownership, skipped-string, rest/reset and fast-repeat checks pass.

The full required-Python Release build, tools and strict C++17 target pass.
All **73 CTest suites** have passing final evidence: the full serial run
passed 71/73 in 339.69 seconds; `StringPitchRealism` and
`RepeatedPluckRealism` exposed their old fixed-displacement assumptions.
Those two test fixtures now independently reconstruct force from actual
held displacement and string geometry. Their original tolerances and all
headroom, passivity, scheduling, merge-energy and extreme-slide checks remain
unchanged. Independent review approved the corrections. The affected rerun
passed 2/2 in 9.89 seconds; production DSP did not change.

The corrected member-force error is `1.61181e-7` and the finite release-time
identity error is `2.3772e-7` (original `2e-5` bound). Repeat-force recovery
error is `3.32962e-7` (original `3e-6` bound); its gain distribution remains
mean 1.00221, standard deviation 0.0664329, range 0.889473–1.11881.
Both revised fixtures fail against the baseline. The original failing full
run is retained alongside the successful affected rerun; it is not presented
as a single 73/73 run.

Rack SDK `JukeboxSDK_500_028`, TargetVersion 5.0, builds the shared source as a
Deployment arm64 module and passes the analyzer/forbidden-global stages.
It emits the same preexisting `radiationPole(anchor)` analyzer warning as
the baseline, with no new warning. The complete seven-stage Rack contract
suite passes, including all eight factory patches at 44.1/48/88.2/96/192 kHz.
The wrapper, properties, panel, patches and product version are unchanged.
This is a local implementation and build validation, not an installed-host,
universal package, portal release or listener-preference claim.

Generated logs stay under `build-physical-hand-20261011`. The committed
[listening manifest](physical-hand-listening-2026-10-11.json) records the exact
source and audio hashes. The Rack repository records its own
[SDK, wrapper and native CPU evidence](../../Docs/PHYSICAL_HAND_REALISM_2026-10-11.md).

## Follow-up: continuous dynamics

The user liked these changes and requested a continuous playing model rather
than four velocity layers. The listening page's phrase "Four velocity layers"
described four selected demonstration velocities; it was misleading. The
production engine has no sample-layer selector. Its force, contact radius,
release speed and contact-noise response already take floating-point velocity.
MIDI 1 supplies 127 nonzero input values, while direct engine callers can use
values between those steps. The offline recording bank's measured layers are
reference data, not the sound-generation mechanism.

The audit did find an actual discontinuity: repeated-note force variation was
enabled only when the current velocity exactly equalled the preceding one.
An arbitrarily small velocity change removed that variation and also changed
which random draw a later note received. This equality test has been removed.

The same bounded, mean-one force residual now fades continuously with elapsed
hand memory and the relative change in commanded force. Both weights use
`1 - smoothstep(x)`, so the value and slope join the nominal response smoothly.
The force-change scale is the existing residual's full two-dB span; the time
scale is the existing hand-memory duration. These are authored performance
choices that preserve accents, not newly measured physiological constants.
The residual enters as `1 + weight * (draw - 1)`, preserving its mean and
headroom bounds. Every eligible repeat consumes one draw even at zero weight,
so varying velocity does not switch the later random sequence. First notes,
simultaneous duplicates, separate notes/channels and strums retain their
existing ownership rules. No control, layout, saved parameter or latency changes.

The Thumb audition now follows one smooth crescendo and diminuendo, sampled at
each note and rounded only for MIDI 1 transport. It stays on one pitch/string
so a register change cannot disguise the dynamic response. The original
listening pack remains intact; the new pack and its source snapshots are in
`build-physical-hand-20261011/listening-continuous/`.

Continuity validation includes 771 fractional-velocity probes across Finger,
Pick and Thumb, with maximum independently reconstructed force-law error
`2.9473e-7`. Around an identical preceding velocity, shrinking the perturbation
to `2^-20` reduces relative force difference to at most `5.8633e-6` and emitted
audio difference/RMS to `4.5266e-5`. The previous implementation fails 16 new
checks and retains differences of 0.106956 in force and 0.0393418 in audio at
that tiny perturbation. Dense first-stroke checks pass on both implementations,
isolating the defect to the old repeat gate.

Independent repeat checks recover string force at both extreme residual draws
and verify that it rises throughout the fade, with a minimum relative step
of `0.000663332`. Equality and fade-endpoint residual errors are at most
`1.19209e-7`; memory expiry and changed velocity preserve the random stream.
The existing force bounds, mean, passivity and scheduling checks still pass.
The focused Velocity and Repeated suites pass in 10.57 and 5.83 seconds, and
the strict C++17 target builds.

All ten new audio files remain finite and nonsilent, use unity common gain,
and peak below 0.514. The browser decodes every file as eight seconds without
media errors. The [new source/audio manifest](physical-hand-continuous-listening-2026-10-11.json)
is separate from the original pack. This verifies continuity and render safety;
it does not itself establish a listening preference. The user subsequently
approved the final candidate and requested push and merge to main.

The final required-Python Release build and complete CTest run pass **73/73**
in 332.45 seconds on the frozen follow-up source. No test fixtures were
changed after that run. The SDK 5 Deployment ARM64 build and Rack wrapper
contracts also pass; paired default-quality timing for the follow-up is
recorded with the Rack evidence. The earlier optional 4x deadline failures
remain a known qualification limit; this follow-up does not requalify 4x.
