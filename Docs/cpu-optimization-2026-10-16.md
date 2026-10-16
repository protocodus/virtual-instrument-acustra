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
