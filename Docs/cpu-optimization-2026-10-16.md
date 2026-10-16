# Acustra CPU optimization, 2026-10-16

This series starts from the seven-realism revision `0bb6e327da202fa246504a18537e293738997c67`.
The existing Rack Extension 1.6.0f1 package remains its own frozen build; these
optimization commits are not included in that earlier package.

Evidence root: `/Users/vojta/Dev/reason-rack-extension/diagnostics/acustra-cpu-20261016`.
The baseline, each candidate, SDK-native modules, tools, logs, raw render bytes
and per-callback timing CSVs are retained there. No timing attempt was discarded.

## 1. Ordered room FIR ring spans

Split each circular convolution into the two contiguous spans around its wrap.
Coefficient order, products, double accumulation, coefficients and every state
update remain unchanged. This removes a wrap choice from each tap without
regrouping the sum. No filter is shortened and no rate or default is changed.

The first SDK-native stage comparison measured **0.9423% aggregate charged CPU
reduction** for this change alone. This is a small local measurement, not a
universal workload or host-performance guarantee; some cells regress.

Validation before this commit:

- Strict C++17 build and Engine, RoomCapture and RoomBassCoherence tests pass.
- Baseline/candidate Room comparison: 24 configurations at 48/88.2/96/192/384/768
  kHz across both direction/conversion flags, exact audio and all room state,
  every ring head/phase, reset, tail and reattack; corruption negative control passes.
- Actual SDK-native Main L/R samples and write flags match exactly across ten
  cases: chord, strums, bends, explicit hammer/pull cues and wound slides at
  48/96 kHz, default 2x quality/Room 10%.

## Measurement protocol and limits

`stage-01/manifest.json` freezes the SDK500028 arm64 Deployment modules and their
complete wrapper/DSP build-stage sources. The diagnostic builder uses the SDK
pipeline but skips static analysis. The simulated motherboard excludes Reason
scheduling/UI and target translation. Module setup, CSV export and parity renders
are outside timed callbacks. Raw render mode performs no clock reads.

Ten equally weighted cases, three repetitions, six measured seconds plus one
excluded second, 64-frame callbacks. Module first order rotates across cases and
repetitions and traversal direction alternates; it is approximately balanced
overall, not exactly per rate. The statistic is the geometric mean of ratios of
per-case median run-mean charged CPU. All repetitions and callback outliers stay
in the evidence. Wall time uses CLOCK_MONOTONIC_RAW, thread CPU uses
CLOCK_THREAD_CPUTIME_ID; both include timer overhead.

Each of four stage modules has 202,500 measured callbacks. Baseline has 22 CPU /
220 wall deadline exceedances; ring spans has 46 / 436. These overlapping counts
must not be added. The result is not a clean realtime pass. Do not infer the cause
of spikes from the aggregate CPU change. Task builds/tests/renders were idle;
other desktop activity and core residency were uncontrolled.

`stage-01/timing/summary.json` and `SUMMARY.md` retain every per-case result;
`stage-01/parity/receipt.json` retains all 30 comparisons, including subsequent
cumulative candidates. The baseline-to-cumulative03 quick canonical comparison
also matches 37,123,200 sample values and 2,532,428 public-state words, but it is
not a substitute for the independent candidate01 Room and SDK checks above.

## 2. Exact scalar builtins in new room/winding paths

Use the existing `exact::floor` and `exact::abs` helpers for the newly introduced
winding phase and room-side state-floor calculations. Under the SDK's freestanding
build these avoid three software `floor` calls per active winding sample and two
`fabs` calls per room-side update. The operands, precision, formulas and thresholds
are unchanged. The helpers retain their existing standard-library fallback.

The first stage measured **0.9719% further aggregate CPU reduction** relative to
ring spans; the two changes together measured **1.9050%** relative to baseline.
These gains are not additive and remain machine/workload-specific. This cumulative
module has 12 CPU / 145 wall deadline exceedances across its 202,500 callbacks.
Every one of its ten SDK-native Main/write-flag comparisons matches the original
baseline exactly; actual activation witnesses confirm hammer/pull and wound motion.
Room states/audio also match in the cumulative03 oracle (including `coherentSide`).
Finite-value/sample equivalence is claimed; floating-point exception flags are
outside the instrument's contract and were not compared.

The proposed stationary-contact early return (`candidate-03`) is **not retained**:
its aggregate CPU was 0.2277% slower than this cumulative module in the preserved
stage, despite exact audio/state. No improvement is claimed for that shortcut.

## Retained revision and broader measurement

The retained DSP revision is `0d2c01a9078a36e7da465c056fca7fe32d60a57c`.
The two improvements are separate commits: ring spans `f256dfa`, scalar builtins
`0d2c01a`. Comparator coverage is in the preceding test commit `17c0f2a`.

`stage-02-final` compares the original baseline, retained candidate02 and a new
ordered stereo-double SIMD experiment. There are 22 cases, three repetitions
and 459,000 measured callbacks per module. The additional cases cover idle,
Pick runs, changes, release tails, dry/wet Room and 1x/4x quality. The retained
changes reduce charged CPU by **0.7065%** across the 20 non-idle cases,
**0.7147%** across the 16 default-quality/Room non-idle cases, and **0.7426%**
across the 14 active default cases. These are different workload selections,
not confidence bounds. The earlier ten-case result was 1.9050%; the broader
result confirms only a modest local benefit, with individual regressions.

| Default-quality example, 64 frames | Original CPU mean | Retained CPU mean |
| --- | ---: | ---: |
| Strums, 48 kHz | 152.591 µs | 151.573 µs |
| Strums, 96 kHz | 151.499 µs | 150.516 µs |
| Bends, 48 kHz | 288.152 µs | 282.956 µs |
| Bends, 96 kHz | 268.981 µs | 263.683 µs |
| Slides, 96 kHz | 129.848 µs | 132.639 µs |

Each number is the median of three run means. Non-idle case changes range from
2.1497% more CPU to 1.9696% less CPU. Across all 22 cases, baseline has 67 CPU /
253 wall deadline misses and candidate02 has 28 / 268. In the 18 default cases,
the counts are 14 / 141 versus 16 / 237: **default deadline safety did not
improve**. All callbacks and spikes are retained. Independent recomputation of
all 198 runs / 1,377,000 callbacks is in `retained02-audit/summary.json`.

The machine is an Apple M1 Max, MacBookPro18,2, 32 GiB RAM. These remain native
arm64 SDK500028 simulated-motherboard measurements, including timer overhead.
An unrelated Electry Recon process was open but idle at the initial snapshot;
desktop activity and core placement were uncontrolled. This is not a Reason
host or target-chip realtime guarantee.

The stereo SIMD experiment is also **rejected**: it was 0.3672% slower than
candidate02 across the broader non-idle matrix (0.1693% slower at default
quality/Room). Its audio/state comparisons passed, but that does not establish
a speedup. Its source, Clang-only guard variant, parity and timing evidence stay
preserved in candidate04 directories. No rejected shortcut is in the retained
source.

## Final retained-source validation

`final-candidate02-validation/summary.json` binds the clean source revision and
reverified hashes to the full Release build, strict C++17 build and **108/108
tests**, with no skipped tests. The focused ASAN/UBSAN run passes **4/4**:
Engine, WoundSlide, RoomCapture and RoomBassCoherence. LeakSanitizer is disabled
because it is unsupported in this macOS setup.

`parity-02-run-01/summary.json` records **123 configurations plus 67 alternate
block partitions**, 165,634,428 exact Main/Piezo sample values and 8,728,538 exact
public-state words. Audio and state corruption controls both fail correctly.
The actual SDK-native all-bus proof in `sdk-allbus-proof-02/result.json` adds six
cases / 12 renders, with exact Main L/R and separately connected Piezo samples
and write flags; every bus produces meaningful finite audio.

The 24-case private Room oracle covers 18,343,517 samples and 18,145 complete
state checkpoints, all ring heads/phases, reset, natural tail shutdown and
reattack. `retained02-audit/room-evidence-reuse.json` proves that its candidate03
Room methods, helpers, coefficients and headers are identical to retained02;
the only whole-source difference is the rejected shortcut outside Room.

## Sound-changing options for user review

These options remain experimental; no quality setting, FIR length or Room
default has changed. `tradeoffs/matrix-02` freezes the retained source and an
isolated order32 variant, then measures six cases at 48/96 kHz, three repetitions
and four variants: **72 runs / 729,000 measured callbacks**. All timed event
scores match their audition renders. The six-case aggregate includes complete
release tails with an idle portion; per-case results remain available.

| Option | Aggregate CPU saving | Per-case range | Measured sound change / expected cost |
| --- | ---: | ---: | --- |
| Room FIR order 64 → 32 | 4.7526% | 3.073–5.875% | Smallest tested difference; native difference RMS is −110 to −140 dB relative to reference at Room10%. Input-filter stopband rejection weakens from about 75 to 39 dB. |
| Existing quality 2x → 1x | 51.7818% | 48.864–55.402% | Changes whole-engine processing and antialiasing margin; largest waveform difference (−21.41 to −11.92 dB relative). |
| Existing Room10% → 0% | 15.5686% | 11.532–17.362% | Removes early reflections, stereo room space and late ambience; external reverb cost is not included. |

For Pick strums, the median callback CPU at 48/96 kHz is 168.394/164.652 µs for
the retained default, 163.220/156.549 µs for order32, 79.863/84.195 µs for 1x,
and 148.974/141.509 µs dry. Baseline/order32/1x/dry deadline counts across their
182,250 callbacks each are respectively 46/272, 49/293, 7/115 and 43/83
(CPU/wall). Lower average cost does not imply a clean deadline pass.

Final timing ran after all task tests, builds, renders and witnesses completed;
no Acustra, CTest, CMake or Recon process matched the pre-run inventory. The
user could use the audition player; desktop/core placement stayed uncontrolled.
The earlier complete `matrix-04-clang` tradeoff experiment used the subsequently
rejected SIMD parent and is preserved, but its figures are superseded here.

The [local A/B player](http://127.0.0.1:8851/) serves `tradeoffs/audition-02`:
20 aligned comparisons / 28 native renders, native levels plus optional fixed
RMS matching, bright Pick strums, Finger hammer/pull gestures, 13-second release
tails and additional Room50% witnesses. WAV samples match raw callback floats;
all 226 checksums pass. Playback, switching, seek, level matching and reveal
were functionally checked. Full provenance and metrics are in its manifest.

The user reported hearing no difference for **room order64 versus order32**.
`tradeoffs/user-feedback.json` records that statement; the scene/rate was not
specified. This supports trying that option but is not a universal audibility
claim or an instruction to adopt it. Detailed mechanism-based sound estimates
are in `tradeoffs/SOUND-IMPACT-02.md`; no agent claims a listening preference.
