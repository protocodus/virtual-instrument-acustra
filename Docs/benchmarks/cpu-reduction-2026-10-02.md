# Acustra CPU reduction, 2026-10-02

The retained change reduces bridge-derivative overhead without changing audio:
a 16-slot history replaces the 10-slot ring, wrapping indices with a mask,
and the short derivative and bridge-velocity recurrences are inlined. The
interpolation order, delay range, coefficients and active physical tails stay
the same. The extra history costs about 992 bytes per SDK instance.

The DSP change is `06cd050`, against release-preparation revision `52c7ef0`.
This report's [machine-readable record](cpu-reduction-2026-10-02.json) records
module hashes, compiled DSP source hashes and every final-module result. UI
changes after that revision do not modify the DSP.

## Final local45 native module

The actual SDK Deployment module, not a stand-in JUCE build, was compared on
an Apple M1 Max running macOS 26.5.1 (25F80), with JukeboxSDK_500_028 targeting
5.0. The native ABI bench invokes the 64-frame Rack callback in fresh processes.
Each case renders eight seconds, alternates the two modules over three runs,
and reports the median per-run mean thread CPU time. The body starts in the
Original or Classical 78 construction. Rates are 44.1, 48, 88.2, 96 and 192 kHz.

| Starting model / performance | CPU savings at the five rates (%) |
| --- | --- |
| Original / held chord | 5.94, 6.34, 5.81, 6.08, 6.86 |
| Original / tail | 5.89, 5.96, 5.53, 5.27, 2.20 |
| Classical 78 / held chord | 6.23, 3.65, 5.88, 6.20, 6.07 |
| Classical 78 / tail | 5.66, 5.28, 5.97, 5.84, 5.63 |

All 20 final-module performances are byte-identical to the baseline, including
the written flags and both audio channels. No realtime allocation or nonfinite
output was reported. These short repeated measurements support the requested
>5% improvement in many cases; they are not a uniform guarantee across hosts,
architectures, sample rates or workloads. The host application's UI CPU is
outside this DSP benchmark.

## Broader checks and limits

Before the final-module check, the isolated SDK candidate ran 420 timing runs:
two starting models, seven scenarios, five rates, three repetitions and two
modules. All 70 paired renders were byte-identical. Held chords saved about
6–7%, tails about 5–6%, with smaller savings in some changing/bending passages.
The seven scenarios were idle, held chord, strums, picking run, construction
changes, bends and release tail. The changes scenario can switch models during
the performance. Idle cost is too small to interpret as a useful percentage.

A separate native plug-in callback comparison tested both models at 48/96 kHz,
32/64/128-frame blocks, and four attack/repick/harmonic/rolled-chord cases. All
48 paired output checksums matched. Those callbacks include note construction,
so their gains are smaller and do not establish a uniform VST >5% improvement.
High-rate transient deadline misses remain in the SDK matrix; this optimization
does not establish glitch-free operation at every rate.

Raw runs and rendered comparisons are retained in the task's ignored
`build-cpu-2026-10-02/` directory. The repeatable runner is
`Examples/Acustra/Tests/cpu/run_cpu_matrix.py` in the Rack repository; it forwards
explicit `--set` construction overrides to both timing and render runs. The
source engine and Performer tests, native/Rosetta checks and strict plug-in
validation are reported with the release candidate, separately from these CPU
measurements. Rejected bridge-bank vectorization experiments are not included.
