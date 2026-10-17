# Natural performance without extra playing controls

Four changes connect the sound to the contact and the player's recent gesture:
Finger attack noise follows the contact release time, neighbouring strokes
share a slowly changing hand posture, the damping hand touches a local point
on the string, and Classical 78 includes a small fixed player-contact loss.
These are restrained authored approximations, with the measurements and
remaining limitations distinguished below. No listening preference has been
inferred from a descriptor score or a regression test.

The production comparison starts at `9bf1cff`, after the Classical 78
microphone correction.
There are no new saved parameters, panel controls, added lookahead or input
latency. MIDI velocity remains the player's accent control. The existing
global 1/32-note join window, sustain rules, string allocation and controller
mapping remain in place. These sound changes intentionally affect existing
performances, while saved parameter IDs remain compatible.

## Contact, damping and body

The [Finger contact report](finger-contact-release-2026-10-07.md) traces the
excessive bass attack to the existing noise burst. Its two existing filters
now use a corner derived from the actual force-release time, bounded by the
old colour law. No new filtering stage is added. In isolated known-Finger
bass recordings, the median paired high-band attack-share change is
−6.93 dB for Original and −5.44 dB for Classical 78. Native first-15-ms RMS
changes by −0.105/−0.292 dB. Upper Finger notes and Pick remain exact in this
isolated comparison. The whole-note descriptor result is mixed: Original
Finger is 0.12% worse, Classical 78 Finger 1.20% better, and the unknown-tool
Martin transfer is worse. The attack gap is reduced, not eliminated.

The [note-ending report](gesture-damping-2026-10-07.md) explains the local
relaxing hand contact. It damps travelling-wave increments at the point the
hand lands, allowing partials near a node there to survive longer. The
existing release duration still governs the ending. A stateful passive
contact avoids the late displacement-release tick produced by a rejected
memoryless prototype. Held notes and notes joined before damping starts are
unchanged by this mechanism. The location and compliance are authored;
the sustained-note recording corpus cannot validate them.

The damping hand withdraws smoothly over its final 6 ms before the existing
return-to-open deadline. This brings its stored displacement offset exactly
to zero before the contact is cleared. A moving hand can do bounded work;
the fixed-contact passivity proof does not apply to the withdrawal as a whole.
The mechanism report quantifies the local energy limit and native audio
effect. A repeated note retains its previous string while this physical
contact is active, even when the level observer crosses its quiet threshold.
Ownership, harmonic eligibility, panic and the hand-back deadline remain
authoritative.

Integration review found that a supported slide past fret 20 could place the
pad using the decay table's clamped fret instead of the actual string length.
The pad now uses the existing physical slide geometry. A regression covers
conventional, MPE manager and MPE member bends at three rates, including both
ends of the decay table; member bends retain their fixed physical length.

The [player body report](player-body-loading-2026-10-07.md) describes the
small structural loss added only to Classical 78. Its source measurement had
a supported neck/end block and free back. Original already contains fitted
recording damping, so it is left alone. A single loss law updates radiation,
bridge coupling and tuning mobility consistently. The radiation update
preserves the continuous input coupling, including the zero-order-hold
integral. No moving body poles, random Q, extra room-feedback loop or extra
per-sample modal stage is introduced. The magnitude represents one authored
held posture, not a measured player or a physically reconstructed room.

## A coherent picking hand

The hand has two bounded state variables, position and pressure, each in
`[-1, 1]`. A separate deterministic random generator moves them by reflected
diffusion at actual contact or stroke setup. Movement scales with elapsed
seconds (`0.70 * sqrt(min(dt, 1))`), independent of host block size. Same-sample
contacts do not advance the hand again. The first contact and the first after
a rest longer than two seconds retain the previous independent contact law;
they establish posture for the following notes. Reset and panic clear usable
gesture history. There is no periodic oscillator, pitch wobble or added MIDI
timing jitter.

For nearby contacts, the existing independent draws are blended with the
hand position or pressure. The position blend is 70% shared / 30% independent;
angle is 65% / 35%. Both retain their previous hard bounds. Actual release
radius varies at most ±6% with pressure. Explicit MPE CC74 retains its previous
position law; explicit member pressure takes precedence over the added angle
and compliance variation. The hand does not overwrite the played accent.

Each strum captures one posture for all its queued strings. Its force draw is
35% shared pressure / 65% independent, inside the existing ±7.74 dB limit.
Stroke speed is 80% independent / 20% pressure, with the existing traversal
bounds and repeat-interval cap. No far string is repeatedly prevented from
being reached by a fast return stroke. A queued-chord reshaping correction
preserves both the captured posture and stroke direction, including an
attack that fires immediately when moved to its new voice.

This correlation deliberately reduces marginal scatter; it does not preserve
the old uniform distribution. Force standard deviation changes from 4.469 to
3.299 dB. Under a stationary uniform-gesture approximation, mean linear force
gain changes by −0.500 dB and its RMS by −0.886 dB. Position/angle standard
deviations become 0.762/0.738 of their previous values. These are distribution
calculations, not audio loudness measurements or measured universal human
statistics. There is no compensating per-note gain. Single-note repeated-force
variation retains its previous ±1 dB law; explicit velocity accents remain.

`PickingHand` checks bounds, distribution, correlation, rests, reset,
same-sample contacts, rate/block invariance, force/accent preservation,
queued snapshots and deterministic reprepare. `PickingHandBoundary` covers
early key-up, actual queued reshaping, immediately fired moved notes,
explicit late MPE controls, channel/global panic and 8–384 kHz boundaries.
Removing the snapshot transfer makes all eight reshaping assertions fail.

## Retuning without a coefficient step

The quieter attacks exposed a live-tuning transient caused by replacing the
string's bending-loss and dispersion coefficients while their histories
still held the previous wave. Seven coefficients now change over the existing
one-roundtrip retuning deadline. A new discrete retune starts from the applied
state; continuous revisions keep the remaining deadline; unchanged targets
preserve exact step bits. Each sample is evaluated from the fixed target and
remaining count in double precision before conversion to float, avoiding
accumulated numerator/denominator drift. Coefficients remain between their
endpoints and land exactly on the target. There is no added MIDI lookahead
or output latency.

`RetuneContinuity` covers endpoints, near-completion revisions, copied and
reset state, coefficient stability and model/rate/bypass cache comparisons.
Removing the unchanged-target guard fails its negative control. Disabling
this setup-only mechanism reproduces the preceding DSP over 422,400 stereo
frames of tuning reversal, bend, Touch, refretting and release tests.

## Validation and level balance

Production DSP and calibration are frozen at `a69627c`. All 68 DSP/tool suites
and both native processor/tempo-release suites pass, with no skipped tests.
Linux VST3 and Standalone builds succeed, and the engine and Performer also
compile as C++17 with `-Wall -Wextra -Werror`. This does not establish a
macOS AU, Windows or Reason SDK package build.

The scoped calibration changes the 12 Classical 78 Pick construction cells.
Its microphone gain corrections are at most +0.565 dB; Original, Finger and
Thumb table entries remain exact. Compensating pickup factors retain the
absolute piezo calibration product within one float32 ULP. The generator now
accepts `--pickings` alongside its model selector, with preservation tests.

The complete final check renders 216 construction/style/capture loudness
combinations and 72 hard-Pick cases. All 216 loudness values pass the unchanged
±1 LU tolerance: stereo −0.900009…+0.316379 LU, mono −0.899992…+0.351673 LU,
and piezo −0.795724…+0.302966 LU. The strict combined check still exits 1 for
39 retained headroom-compensation policy flags, versus 41 previously; no
policy flag is new. Actual sub-1-dB-margin cases change from 51 to 52
(four new, three resolved). Above-knee cases remain 32 with identical
identities. All four new margin shortfalls already use the permitted
approximately 0.9 LU level reduction. These extreme-picking limits are
retained in the evidence rather than described as a clean headroom pass.

The final DSP freshly reproduces all 166 previously scored held-note arrays
at common **old** trims: 83 unique arrays per model cover the same 115 rows
per model. Only the exact `9bf1cff` construction-gain header is substituted
in this explicitly labelled analysis copy. The production gain change above
is evaluated separately; no per-note compensation or refitting is applied.

| Common-trim descriptor error change | Original | Classical 78 |
| --- | ---: | ---: |
| Known Eastman Finger | +0.12% | −1.52% |
| Known Eastman Pick | exact | −0.42% |
| Unknown-tool Martin | +5.00% | +2.32% |

Lower error is better. Original's bass attack correction has a mixed overall
tradeoff. Classical 78 improves on both known styles, while its Finger high
register and G4 have small regressions (+0.026% and +0.042%). The unknown-tool
transfer gets worse in both models. These development recordings are not an
independent listening panel or untouched validation set. Isolated held notes
cannot validate gesture endings, repeated strokes or mix preference.

The corrected listening validation contains 168 isolated renders and 24
production-native renders. All 24 all-five-disabled cases match actual
`9bf1cff` bytes at common old trims; every variant verifies the 426 authored
string attacks. Production-native maximum peak is 0.717896, with no dropped
events or non-finite/silent clips. Whole-passage loudness changes range from
−0.390 to +1.193 LU; the largest is Classical 78 Pick strums. Primary A/Bs
therefore use the actual old build and the final production build with
explicit whole-passage level matching. Their shared audition gain may raise
playback level, while the raw native evidence remains untouched.

## CPU cost and remaining deadline limits

The frozen production model and `9bf1cff` were compiled with the same GCC
14.2 toolchain and `-O3 -DNDEBUG`, using each version's own headers. A quiet
window covered 1,030 configurations with 128 alternating paired trials after
16 warmups, pinned to one CPU. Untimed state checks verify that attacks,
release contacts and retuning transitions actually occur; output hashes are
consistent across trials. Rendering, builds, exports and reporting were
outside the timing window. These are native DSP measurements on a shared
cloud machine, excluding host overhead.

The median of per-case paired cost ratios is **+1.23%**; the median per-case
absolute p50 difference is **+1.67 microseconds**. Across cases, median p50
times are 138.08→142.46 microseconds and median p95 times are
190.39→194.35 microseconds. These independent medians should not be divided
to reconstruct the paired ratio. The 918 dry mechanism/control cases show
+1.44%; the preserved 112-case Pick matrix shows −0.09%. Its 56 cases at the
default 50% room setting show −0.21%, effectively unchanged. The largest
paired median regression is +7.32% for Classical 78 firm-Pick release at
96 kHz/128 frames (135.75→145.71 microseconds). Retune handoff and live tuning
show +3.30% and +3.42%; settled controls show +0.36–0.61%.

This does **not** establish deadline safety. Cases with p95 beyond their
block deadline change from 76 to 75; over-deadline trials change from
6,414 to 6,403. All 22 extreme 24-event Gather burst cases at 96 kHz/64 frames
miss every trial in both builds. The worst current p95 is 9.036 ms against
a 0.667 ms deadline (previous 8.065 ms); its paired median cost changes by
only +0.49%. The modest typical overhead does not solve those existing
queue-burst limits, and timings are not a guarantee for other hosts.

Raw samples, source/build hashes and scope summaries are retained under
`build-natural-performance-oct07/cpu-production-a69627c-timed/`, especially
`cpu-run-manifest.json`, `cpu-summary.json` and `cpu-final-scopes.json`.

## Reproducible comparisons

`PerformanceRealism` provides five setup-only switches for offline ablation:
the four mechanisms plus intrinsic-filter retuning continuity. The product
enables all five. Changing them resets sounding state and is
not a supported automation gesture. The listening renderer uses identical
written MIDI for baseline, contact, hand, damping, body, continuity and combined variants,
on both models. Native raw floats and float WAVs retain their levels. Primary
A/Bs use one whole-passage RMS gain per variant, followed by a shared peak
safety gain; there is no per-note normalization. Accompaniment examples use
an explicitly synthetic bass/drum bed and do not establish real-band mix
preference.

The controlled passages explicitly enable string-per-channel mode with
CC126 value 6 and verify the physical string and fret at each attack. Earlier
development renders incorrectly sent value 0, allowing automatic allocation
to place the same MIDI note on different strings as release history changed.
Those renders are retained as superseded prototypes, not controlled
before/after evidence.

The piezo overload reference interpolates the free preamp drive before the
input-range knees that are hidden within the output stage's saturated
interval. Interpolating the already-clipped intermediate signal invents a
different continuous corner. The test verifies that those hidden knees do
not change the host-sampled clipped result, retains the original −60 dB
absolute and 1.5 dB relative guards, and is backed by the independent
physical circuit reference tests. Removing BLAMP fails both kinds of guard.
No piezo DSP change is needed for this reference correction.

An exact shape-retuning comparison now gives its fresh reference the same
picking-hand history as the engine being retuned. Otherwise the contacts
have different geometry and physical attack-pitch compensation. Its exact
target-delay equality remains intact, and a stale-shape negative control
still fails.

```sh
cmake -S . -B build-natural -DCMAKE_BUILD_TYPE=Release \
  -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON \
  -DACUSTRA_REQUIRE_PYTHON_TESTS=ON -DBUILD_TESTING=ON
cmake --build build-natural --parallel 2
ctest --test-dir build-natural --output-on-failure
build-natural/AcustraRenderNaturalPerformance production-renders --variant combined
python3 Tools/PackageNaturalPerformance.py --renders analysis-renders \
  --output new-listening --source-tree frozen-analysis --build-dir analysis-build \
  --renderer-binary analysis-build/AcustraRenderNaturalPerformance \
  --previous-renders previous-renders \
  --production-renders production-renders \
  --production-source-tree frozen-current --production-build-dir build-natural \
  --production-renderer-binary build-natural/AcustraRenderNaturalPerformance
bash Tools/BuildAudibleRealismBenchmark.sh \
  frozen-baseline frozen-current new-cpu-build
new-cpu-build/AcustraAudibleRealismBenchmark checks.json \
  --natural-performance --untimed-check
```

Freeze both source trees, tools, event scores, controls and binaries before
rendering. The previous-render comparison must be a native `9bf1cff` build
using the same renderer and toolchain. Separately build `frozen-analysis`
from the current snapshot with only `ConstructionLoudnessData.h` restored
exactly from `9bf1cff`; use it for the seven common-trim variants in
`analysis-renders`. The packager checks that this is the only Source
difference from `frozen-current`. Production renders use the delivered
gain header; primary A/Bs compare these with the actual previous build.
CPU timing follows only after builds,
tests and render jobs are idle; it excludes exports, hashes and reporting.
The benchmark includes actual repeated gestures, physical damping after the
join deadline, sustain release, queued bursts, Touch/velocity edges and
capture/rate/block combinations, with untimed state checks before timing.

The local evidence root is `build-natural-performance-oct07/`:

- `production-build-validation.json` records all 29 production Source
  hashes, final test inputs, strict C++17 objects, native binary hashes and
  the complete DSP/host test logs.
- `calibration/final-a69627c/` retains all 288 native calibration renders,
  loudness/headroom comparisons and checksums.
- `final-corpus/final-a696-oldtrims-native-parity.json` proves the final
  common-trim held-note parity without attributing it to production levels.
- `withdrawal-independent-review/receipt.json` records the independent
  19,200-case local energy and endpoint audit.

Large native outputs are local evidence, not repository assets. Listening
derivatives retain their parent native hashes and explicit level gains.
