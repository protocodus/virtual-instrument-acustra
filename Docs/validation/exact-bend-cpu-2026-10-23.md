# Exact SDK retune math, 2026-10-23

This candidate targets continuous pitch-controller retuning. It changes only
scalar/complex math inside `coupledPolarisationDetune` and the loss-magnitude
helpers; physical coefficients,
controller cadence, voice state, delay transitions, and model quality stay the
same. Other toolchains keep their own standard-library implementations.

The SDK-native bend profile (7 seconds, fixed Rack wrapper `8ac97ba`, canonical
DSP `8fd6752`) attributed 12.4% of awake self samples to newlib's software
`__ieee754_sqrt`. Complex division and its exponent-scaling calls were another
visible control-path cost. This is qualitative attribution, not CPU timing.

The double complex division fast path preserves libc++'s exponent scaling and
arithmetic order. It replaces scaling calls only when each input/output is zero
or stays normal after an exact exponent-bit change. Subnormal, nonfinite,
underflow, and overflow cases run the complete SDK division instead.

The magnitude fast path preserves newlib's compensated two-branch expression
only in its unscaled normal range. It uses the existing correctly rounded
hardware square-root intrinsic. The finite complex square root keeps libc++'s
`polar(sqrt(abs(z)), arg(z)/2)` expression and SDK trigonometric routines.

The initial unguarded square-root experiment was rejected: the SDK's compiled
software square root keeps nearest rounding under directed runtime rounding,
whereas the hardware instruction follows the caller's mode. The accepted path
must check `__builtin_flt_rounds() == 1`; other modes retain the SDK library.
The intrinsic queries the mode without changing it or performing an arithmetic
probe that would introduce floating-point exceptions.

Verification and paired measurements are recorded in the task artifacts at
`reason-rack-extension/diagnostics/acustra-exact-cpu-20261023/bend/` and the
independent SDK bench under `diagnostics/acustra-cpu-fix-190d2-20261023/bench/`.
No failed experiments or callback outliers are discarded.

The guarded candidate passed these checks:

- Native dual-build parity: 23 configurations and 23 alternate partitions,
  37,123,200 Main/Piezo sample values and 2,532,428 public-state words exact.
- Independent actual-SDK parity: 36 cells at 48/96 kHz, captures 0/1/2,
  chords, strums, continuous bends, subframe bends, wide bend edges, and
  cable/reset cases. All 69,984,000 samples, 3,280,500 MOM values, and
  1,093,500 bus write flags are exact (333,517,500 bytes per version).
- Actual-SDK math oracle: 7,400,832 input cases across all four rounding
  modes; all 37,004,160 division, magnitude, and complex-square-root result
  doubles match bit for bit, including signed zero, subnormals, threshold
  neighbors, all exponent ranges, infinities, and NaN payloads. The mode
  query always reports the correct mode. One-bit corruption fails the test.

The oracle calls the baseline SDK operators at their verified symbol offsets
and a diagnostic-only candidate probe compiled with the SDK. The probe calls
these production helpers directly; no copy of their algorithm acts as the
candidate. `Tools/SdkRetuneMathOracle.cpp` is its standalone driver. The probe
source and exact relink argument list are retained in the task artifacts.

The SDK paired screen uses three repetitions at default 2x, host blocks of
64, 48/96 kHz, and the same chord/strum/bend workloads. The summary reports
medians across repetitions, preserving every callback:

| Workload | Baseline mean CPU | Candidate mean CPU | Baseline p99 | Candidate p99 |
| --- | ---: | ---: | ---: | ---: |
| Bends, 48 kHz | 294.69 us | 257.68 us | 471.17 us | 434.33 us |
| Bends, 96 kHz | 267.48 us | 237.33 us | 444.13 us | 407.29 us |

This is 12.56%/11.27% lower charged bend CPU; the equal-case geometric mean
saving over six cells is 3.65%. Steady chord/strum differences are around
one percent, including small regressions. The external Recon process used
roughly 60% of one core and was not controlled or paused. These measurements
do not establish a real-time pass: 96 kHz bend CPU deadline misses were
4 baseline versus 12 candidate across the three runs, and p99.9 was
622.63 versus 662.71 us. A repeated chord-replacement event remains costly;
other outliers move between runs. The independent controller-only audit
compares the same callback indices, excluding note dispatch. Its median
p99.9 improves from 765.83 to 669.71 us at 48 kHz and 622.29 to 601.33 us at
96 kHz, but the 96 kHz controller-only maximum increases from 665.29 to
735.00 us and CPU misses from 1 to 8. Wall-clock misses and maxima are retained
in `math-time01/manifest.json` and raw CSVs rather than excluded.

The divide-only intermediate candidate was exact but saved less (1.41%
equal-case aggregate). The guarded combined candidate was retained for the
clear continuous-retune improvement; final integration needs the complete
SDK package and expanded regression gates.

A subsequent conservative predicate candidate returned false when a finite
component exceeded twice the tiny rejection threshold, retaining SDK hypot
for all near-boundary and nonfinite cases. Its 15,065,376 threshold booleans
matched the SDK across four rounding modes, and actual-module audio/state
parity passed. The adjacent three-repeat SDK screen saved only 0.303%
aggregate CPU over the accepted math candidate (48/96 kHz bend savings
1.46%/0.20%, mixed steady results). It was rejected as marginal; its source
snapshots, modules, oracle results, and raw timing remain in the artifacts.

The final two-call-site extension reuses the same proved magnitude helper for
`bendingLossMagnitude` and `constantLossMagnitude`. The former wraps the same
real/imaginary pair in a complex value; the latter already has a complex
response. No additional algorithm or cache is added. Independent actual-SDK
parity passed all three buses and nine MOM values in twelve eight-second
cells (six fixtures at 48/96 kHz, default capture).

A separate adjacent three-repeat six-cell SDK screen compared this extension
to the accepted math-only module. Every cell improved; aggregate incremental
saving was 1.04%. Bend means improved 243.06 to 237.38 us (2.34%) at 48 kHz
and 228.67 to 222.81 us (2.56%) at 96 kHz. Corresponding p99 improved
392.71 to 387.21 us and 380.25 to 368.83 us. These are separate paired runs,
so their absolute timings should not be combined with the earlier table.
Rare tails remain: 96 kHz bend CPU misses were 3 in both versions, and the
48 kHz bend p99.9 increased from 430.04 to 533.08 us. All callbacks remain
in `bench/loss-time01`; final combined/package timing is a separate gate.

The standalone oracle driver was reformatted and rerun with
`-Wall -Wextra -Werror`: all 7,400,832 math cases pass. Both math-bit and
predicate-boolean deliberate corruption controls exit with failure.
