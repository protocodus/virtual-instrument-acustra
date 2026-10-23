# Exact collocation anchor trigonometry, 2026-10-23

The independent candidate, based on `ebd23f5` (accepted retune math and loss
magnitudes), computes each of the three collocation anchors' sine/cosine basis
once per solve. The former SDK-generated residual evaluator recomputed four
trig values for each anchor on every Newton, finite-difference and backtracking
evaluation. Actual candidate SDK IR has no trig calls in that anchor loop.

Only `collocateDispersion` uses the new phase/read overloads. Their double
expressions, two `atan2` calls, subtraction and wrap loops are copied unchanged
from the original helpers. Solver bounds, iteration counts, early exits,
coefficient arithmetic and accumulation order remain unchanged. Three local
32-byte bases have fixed stack lifetime; no persistent cache, state layout,
allocation or history/reset behavior changes. Other phase callers retain their
original helpers. No approximation, trig identity or quality change is used.

## Exactness evidence

- Native C++17 comparison passes nearest, upward, downward and toward-zero
  rounding separately. Each mode compares 37,123,200 Main L/R/Piezo sample
  values and 2,532,428 public-state words, across 23 configurations and 23
  alternate partitions.
- The actual SDK private oracle compares 357,796 phase/read/residual cases
  and 3,168 complete collocation solves across all four rounding modes:
  1,101,900 double words are bit-identical. Inputs include signed zero, phase
  wraps, unit-circle coefficient boundaries, adjacent representable values,
  fixed random inputs, eight rates and near-integer delay anchors. Its reference
  collocation function is copied verbatim from frozen `ebd23f5`, renamed and
  marked noinline in a diagnostic-only SDK translation unit. Phase and complete
  solve corruption controls both fail as intended.
- Fourteen SDK-native configurations at 48/96 kHz compare all three buses and nine
  motherboard values/write flags exactly. The module is
  `b4205f2e10d8925babdf9295f8f6a163e0eb6b8408bc5481d87b65749aeb7cdd`.
  Every SDK-staged DSP file matches the frozen source snapshot.

These are output/state bit checks. They do not claim identical floating
exception flags or enabled-trap invocation counts. The basis is recomputed
within each solve, so it does not reuse values across caller rounding-mode
changes. The implementation uses existing C++17 scalar math and fixed local
storage; it adds no architecture-specific intrinsic or new SDK API.

## Incremental actual-SDK CPU screen

The matched SDK 5.0/500_028 arm64 Deployment screen uses the accepted loss
module as baseline, default 2x, 64-frame callbacks, eight measured seconds,
three repetitions, alternating fresh-process variants. All 54 joint runs and
all callbacks/outliers are retained; other task-owned compute was terminal.
Uncontrolled desktop activity remains in the record. This excludes Reason's
scheduler/UI and other hardware; it is not a host deadline certificate.

| Host rate | Strums mean saving | Bends mean saving | Attack-bends mean saving |
| --- | ---: | ---: | ---: |
| 48 kHz | 0.337% | 1.917% | 0.518% |
| 96 kHz | 1.391% | 0.455% | 0.405% |

The equal-case geometric mean saving is **0.839%**. At 96 kHz, the 32 timed
attack-bend note dispatches have mean 604.30 to 581.02 us and p99/maximum
939.54 to 896.54 us; charged misses fall 26 to 22. The four ordinary bend
note dispatches instead have mean 582.09 to 589.27 us and p99/maximum 751.00
to 792.04 us, with misses 3 to 2. Away-note p99 is 377.29 to 367.42 us,
with misses 4 to 3. The whole 96 kHz bends cell worsens from **8 to 52** charged
deadline misses; 49 candidate controller misses concentrate in repetition 0.
Those measurements are retained without attributing them away as noise.

The consistent six-cell mean reduction and bounded local code justify final
combined confirmation. They do **not** establish a clean deadline pass or prove
that pitch-bend/setup spikes are fixed. Final integration requires matched
normal-SDK sound/state proof and a fresh note/controller-tail comparison.

## Frozen receipts

All evidence is under
`/Users/vojta/Dev/reason-rack-extension/diagnostics/acustra-cpu-fix-190d2-20261023/`:

- `anchor-basis/screen-decision.json`: consolidated source/module, exactness
  and timing receipt; adjacent manifests freeze source snapshots and oracles.
- `anchor-basis/sdk-IR-witness.json`: candidate evaluator activation witness.
- `bench/anchor-parity01/manifest.json`: fourteen exact SDK comparisons.
- `bench/setup-time01/manifest.json`, `summary.json` and
  `event-audit-verified.json`: all raw runs, cells and dispatch/controller tails.

Diagnostic probe exports are absent from the audio/timing candidate. Rejected
Bridge/Body layouts and additional gain-sharing proposals are not included.
