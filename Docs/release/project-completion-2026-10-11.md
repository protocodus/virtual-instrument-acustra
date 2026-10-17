# Existing-instrument completion — 2026-10-11

The ACUSTRA finishing pass is complete within the existing-instrument scope
of the [DSP finalization register](../research/dsp-finalization-2026-10-11.md). The final
DSP commit is `2decfdfbe8fdf69ab8a0885de8fd095668d6f174`, based on canonical
`main` `a534d774aa85f31acac6e74e6efebc18221ec748`, fetched again without a
change. Each finalized implementation has its own commit. This report
records one full validation of the final combined model; earlier targeted
checks establish specific corrections and preservation properties.

The final native suite passes **88/88 tests**. Universal AU, VST3 and
Standalone builds and strict ad-hoc signature verification pass. The Rack
integration is committed as `316db04605cafd7d325e5dc265bf5046cb871bbd`, with
all seven contracts and the SDK-native build passing. The remaining
moving-fret approximation and extreme native callback limit are explicit
below. Completion does not mean an exhaustive physical guitar model or
release/host qualification.

## Finalized changes

| Commit | Result | Evidence |
| --- | --- | --- |
| `977bfa7` | Repeated-pluck contact conditioning visits occupied sparse events instead of rescanning contact taps at every wave sample. | 3,456 exact state-parity cases, including wrapped, fractional and overlapping contacts; unchanged reduction order. |
| `ae0ba92` | Prepare builds the twelve Shape/Wood body coefficient sets before playback. Construction automation retains its existing state, coalescing and fades. | Exact coefficient/render parity, all cells/rates, calibration and reprepare, copying and interrupted fades. The extracted builder's 297 lines match after identifier renames and metadata writes. |
| `e7b3cc0` | Each Room delay line preserves the accepted 48 kHz decay at DC and a fixed physical absorption frequency across sample rates. | 128 line/rate cases; stable/passive sections and relative decay error below 2e−6 at the matched frequencies. The 48 kHz reference coefficients are exact. |
| `2decfdf` | Piezo observes the released static saddle load already applied to the bridge/body, using the same per-string sensitivity. | Independent signed-load units, complete reaction identity, circuit/headroom and antialiasing controls. Circuit, gain, construction trims and the accepted release share are unchanged. |

The [piezo derivation](../research/piezo-release-load-2026-10-11.md) retains the initial
full-band relative-error test failure and its resolution. The unchanged
absolute −60 dB ceiling applies through 0.45 fs; the established circuit
passband at 0.375 fs defines the 1.5 dB relative-improvement check, and the
full band must still improve. The worst identified case improves 4.0312 dB
in the passband and 1.1167 dB over the full band. A deliberate BLAMP bypass
fails the relative guards. Minimum tested headroom is 0.851039 dB. No
pickup gain or circuit adjustment was made to pass these checks.

The optimization-only `ae0ba92` comparison against the immediately preceding
completed model `56d4678` is sample-exact over 42 callback/continuation cases
with Room zero and the separate 48 kHz Room-one construction/tail reference.
That preservation claim does not cover the intentional later Room and
piezo changes. [Parity and build receipts](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/optimization-parity.json)
retain the exact sources and hashes.

## Final native and acoustic checks

The universal Release build ran once with three build jobs and two CTest
workers. All 88 tests pass in 220.65 seconds; both C++17 DSP targets also
build. AU, VST3 and Standalone contain arm64 and x86_64 slices; runtime
tests ran on arm64. [The full build/test log](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/native-final-build-tests.log)
and [artifact receipt](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/receipt.json)
record commands, architecture/signature checks and executable hashes. This
is not an Intel runtime or real-DAW save/reopen/automation test.

Fresh final-source construction measurements retain the existing gain
header. All **108 construction × picking × capture cells** remain within
±1 LU of the −27.52 LUFS reference:

| Capture | Minimum deviation | Maximum deviation | Hardest Pick pre-limiter peak |
| --- | ---: | ---: | ---: |
| Stereo microphone | −0.23 LU | +0.36 LU | −2.02 dBFS |
| Mono microphone | −0.24 LU | +0.84 LU | −2.63 dBFS |
| Piezo | −0.90 LU | +0.23 LU | −1.01 dBFS |

The existing loudness/headroom tradeoff passes; these Pick fixtures do not
promise limiter-free output for every Finger stroke. All **180 native
Finger strum rows** are finite, with maximum absolute relative level
4.79437 dB against the existing 9 dB bound and maximum sample peak 0.376532.
[Measurements and the unchanged-gain check](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/loudness-check.log)
are retained alongside the strum rows. All 202 recorded source inputs and
all four rendering binaries were verified; source hashes match the final
commit and current files.

The [fresh capture assessment](../research/capture-assessment-2026-10-11.md) confirms
matched observation and Mono recovery (maximum relative residual energy
4.552e−15). It retains cross-recording spectral discrepancies and source
weight sensitivity. A separate Mono contour offers only 0.023 dB RMS on its
own fit metric. No new observation/state fault was isolated, so the accepted
capture contour, authored microphone balance and level calibration remain.
These cross-guitar recordings do not establish a universal EQ correction or
an end-to-end realism ranking.

Four six-second before/after WAVs for Piezo and Room are available locally
under `build-audit/project-finalization/listening/`. They use identical
sample-indexed events and native output levels, with no normalization. The
[manifest](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/listening-manifest.json)
records the score, controls, hashes and measured differences. **They have
not been listened to in this pass.** Numerical validation is not a claim
of listener preference.

## Final native performance

One quiet, serial run compared `56d4678` and final `2decfdf`: 42 scenarios,
four warmup pairs and 32 measured alternating pairs per scenario (1,344
measured callbacks per version). Preparation and output checking are outside
timing. Wall time and thread CPU time are recorded together; all within-build
determinism checks pass. Builds, renders and downloads were stopped; ordinary
desktop applications remained open. [All trials](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/callbacks-final.json)
and the [summary](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/callback-summary.json)
are retained without discarded outliers or reruns.

| 96 kHz, 32-frame callback; 333.333 µs deadline | Baseline wall median / p95 | Final wall median / p95 | Baseline → final wall and CPU misses |
| --- | ---: | ---: | ---: |
| Six-string re-pick | 347.042 / 486.416 µs | 305.750 / 431.625 µs | 22 → 11 of 32 |
| Held construction/capture transition | 453.667 / 515.125 µs | 55.458 / 66.042 µs | 32 → 0 of 32 |

The corresponding final CPU median/p95 values are 304.583/429.250 µs for
re-picks and 52.541/64.958 µs for construction changes. The repeated-chord
case remains a measured CPU limit at 96 kHz/32 frames. There is also one
final wall-only miss during held Master Tune at 48 kHz/32 frames: wall
1391.208 µs, thread CPU 576.083 µs, deadline 666.667 µs. The noncharged
interval is established; its external cause is not identified.

Across the complete matrix, baseline wall/CPU misses are 57/54; final
misses are 12/11. All other final cases have no observed deadline misses.
Median paired wall/CPU ratios are 0.9840/0.9836; the medians of the
case-median ratios are 0.9931/0.9930. The specific transition improvement
is substantial, but the matrix does not support a broad speedup or a
universal callback-deadline guarantee.

## Final Rack integration

Rack commit `316db04` pins the same `2decfdf` source. The four Guitar choices
and two visible woods (Spruce/Mahogany), hidden legacy values, eight factory
patches, default 1x quality and 49-host-frame latency remain compatible.
Rack retains its dry Room policy. All seven contract steps pass, including
15 host-rate/quality routes, 48 construction histories, tuning/reset,
output routing, and all eight patches at five rates.

The actual Jukebox SDK `500_028` native ARM64 Deployment module builds with
static analysis and forbidden-global checks. One existing analyzer warning
about the fully initialized constexpr body anchor remains a reviewed false
positive; zero-warning status is not claimed. The final module SHA-256 is
`2bdb18a761e51a39527c6fea40bb6098c0d97f691ff17c77a768cc34f961b545`.

A separate final serial matrix ran six scenarios × three repetitions × two
modules, at 48 kHz/64 frames and default 1x quality. Across **110,250
callbacks per module**, the candidate has **zero CPU or wall deadline
misses**; worst CPU/wall times are 727.500/727.666 µs against 1333.333 µs.
The baseline has no CPU misses but wall overruns in all three bend runs
(at least three wall misses; the benchmark stores maxima, not exact counts).
Worst baseline CPU/wall times are 1117.250/4254.833 µs. Median-run mean cost
changes range from −4.5% to +0.8%, with most near 1% or less.

The eager body storage raises SDK peak allocation from 42,858,000 to
43,088,736 bytes (+230,736 bytes across the wrapper's prepared engines).
The Rack repository's `Examples/Acustra/Docs/PROJECT_COMPLETION_2026-10-11.md`
and adjacent `../validation/project-completion-2026-10-11/` contain all 36 runs,
source/module hashes and commands. These SDK adapter measurements do not
establish interactive Reason or portal-target behavior.

## Established boundaries

- The [moving-stop witness](../research/moving-stop-limit-2026-10-11.md) demonstrates that
  conventional slides alter the return path before a physical fret disturbance
  arrives. Final-source confirmation matches all twelve witness cases. A
  read-offset patch would leave contacts and captured tails inconsistent;
  a complete displacement/velocity-field transport model is not implemented.
- Full nonlinear string/contact motion, absolute friction and tool-force
  calibration remain reduced or unidentified. Existing bends, slides,
  natural harmonics and contact behavior are retained. Tapping, slapping,
  pinch harmonics, body knocks and other new techniques are outside this pass.
- Room preserves its accepted mono send and 48 kHz reference. Pair-mid
  cancellation, its late onset and heuristic stereo field remain. A one-pole
  rate mapping matches DC and 8 kHz (fs/4 below 32 kHz internal rate), not
  the entire frequency-dependent decay curve.
- Shape/wood morphs and capture comparisons retain their measured-bank and
  cross-instrument limits. There is no matched complete-guitar/pickup corpus
  or controlled expert-listening result proving overall acoustic realism.
- The native 96 kHz/32-frame re-pick deadline limit remains. No universal
  real-time guarantee, installation, release package, publication or real
  DAW/Reason host qualification is claimed.

The [receipt and reproduction commands](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/commands.md)
identify the exact final source, evidence and local artifacts. No gain
regeneration or new playing technique was needed to complete this scope.
