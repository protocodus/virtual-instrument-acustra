# Five physical-hand improvements — 2026-10-11

Implemented against Acustra `41f2c67`, the canonical instrument submodule used
by the active Rack Extension. The older standalone checkout was left intact,
including its existing repertoire-renderer edit. This deliberately retains
the newer approved string-loss, body, dynamics and gesture work.

The five proposals below are implemented without changing any saved parameter,
default, range, control mapping, layout, host latency, factory patch or body
coefficient table. The sound of existing performances intentionally changes.
These are physical consistency improvements and bounded performance models;
listener preference has not been established.

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

Complete-suite and Rack build/performance results are recorded below when
the final validation finishes. Generated logs stay under
`build-physical-hand-20261011`; the Rack repository records its own SDK build,
wrapper, factory-patch and native timing evidence separately.
