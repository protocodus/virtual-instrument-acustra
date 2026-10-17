# Acustra CPU optimization, 2026-10-13

The final matched iteration-09 comparison reduces the declared aggregate
charged callback CPU metric by **7.2642%**, exceeding the user's five-percent
minimum. All **nineteen** SDK-native audio and written-flag comparisons are
bit-identical. Both modules include the [chorus repair](../research/chorus-repair-2026-10-12.md),
±20-cent Detune range and [Age-transition precision fix](../research/age-transition-precision-2026-10-13.md).
The baseline reverts only the three retained CPU changes, so the comparison
measures optimization at equal repaired sound quality and default 2x quality.
It does not compare overall performance with the earlier 1x-default release.

Iteration 09 revalidates the same combined optimization after the shared sound
repair; it is not an additional gain on top of iteration 08. The original
**6.9071%** iteration-08 checkpoint, all rejected probes and failed checks remain
below and in their frozen receipts. The final all-target build, C++17 target
and **all 90 canonical CTests pass** in 667.67 seconds, with Python tool tests
required and present. Package and host validation remain pending.

## Retained implementation

Only `Source/DSP/AcustraEngine.cpp` differs between the matched iteration-09 modules.
The header, Performer, calibration tables, audio/state layout and controls are
unchanged. The single retained iteration combines three previously isolated
components; their pilot gains are not added together or treated as separately
admitted iterations.

1. Resting-string feedback/load is computed once per other string instead of
   repeating the identical calculation for its two planes. Inputs are immutable;
   normal `g00`, `g01`, `g11` additions still precede the parallel `g11` addition
   with their original expressions and order. Cache keys and generations do not
   change. This saves work on common-calibration misses during contacts/bends.
2. Ordered body-pressure Left/Right additions use the low two lanes of a
   16-byte vector. Full groups scale each unchanged contribution once, interleave
   modes 0/1 and 2/3, then add in original mode index order. Upper accumulator
   lanes are defined but never returned. Partial ordered groups retain valid-count
   handling, and the parallel tail retains its original four-lane reduction and
   final addition. Scoped Clang contraction/reassociation exclusions preserve
   accumulator rounding. Per-mode recurrence, products, state stores and final
   capture filtering stay unchanged; this does not regroup modal sums.
3. The body vector's tiny-state predicate compares IEEE binary32 magnitude bits
   against `0x0da24260`, exactly the float `1e-30f`. Positive magnitude bit order
   is monotonic, both zero signs flush to +0, strict threshold equality remains,
   and Infinity/NaN sign/payload bits are retained. The original scalar fallback
   stays intact. A scalar/original-vector/candidate-vector checker passed
   **16,814,080** deterministic and dense-boundary patterns. State/output bits
   match for every binary32 pattern by the bit-order argument; this is not full
   floating-environment equivalence: integer comparisons omit the old signalling
   NaN comparison's possible `FE_INVALID` flag. Valid instrument dynamics remain
   finite. See [predicate source/checker receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-07/source-receipt.json).

No bridge SIMD broadening, stereo CaptureFilter SIMD, settled-glide fast path,
reordered pressure sum, reduced mode count, lower precision, new audio filter,
fast-math flag, skipped sample/history update or allocation is retained.

## Measurement scope and protocol

Measurements ran on an Apple M1 Max, MacBookPro18,2, macOS 26.5.1 (25F80).
Both frozen modules use JukeboxSDK_500_028, TargetVersion 5.0, and its actual Deployment pipeline: SDK Clang
`-ccdsp -fno-builtin -ffreestanding -target-cpu phdsp64`, the project's `-Wall -O3`
and extended-rate definition, SDK libc/libc++ bitcode, instrumentation, and
`llc -O2` for `apple-m1`, linked with the SDK Rack wrapper. The scratch builder
skips static analysis unless requested; final package checks are separate.
This measures the SDK-built arm64 module and Rack adapter in a simulated
motherboard host, **excluding Reason's scheduler/UI and a portal-built target**.
It makes no speed guarantee on other hardware, quality settings or hosts.

The primary matrix has seven audible workloads at 44.1/48 kHz host rate,
64-frame batches, default 2x oversampling (88.2/96 kHz engine), Room 10% and
Detune zero. Each module/workload/rate has three fresh-process repetitions with
eight seconds measured; baseline/candidate process order alternates. Ordinary
workloads have one second of excluded preroll; the tail scenario measures from
its first batch. Setup, schedule creation, rendering exports, comparison and
reporting are outside the timed call. Each sample remains simulated.
Heavy task-owned builds/tests/renders were idle during timings.

`CLOCK_THREAD_CPUTIME_ID` records charged callback CPU and
`CLOCK_MONOTONIC_RAW` records elapsed wall time. The per-workload statistic is
its median of three run means, not its per-batch median. The declared aggregate
is `100 * (1 - geometric_mean(candidate_mean / baseline_mean))` across the
fourteen active cells; silent idle is excluded because the wrapper elides its
DSP. All raw runs and distributions remain in
[runs.jsonl](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/timing/runs.jsonl); the refreshed
[source/retention receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/source-receipt.json) and
[decision](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/timing/decision.json) retain the declared gate.

| Workload | Host kHz | Baseline mean µs | Candidate mean µs | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Held chord | 44.1 | 129.181 | 116.838 | +9.56% |
| Held chord | 48 | 132.855 | 126.507 | +4.78% |
| Queued strums | 44.1 | 164.020 | 166.376 | -1.44% |
| Queued strums | 48 | 163.817 | 153.297 | +6.42% |
| Picked run | 44.1 | 163.831 | 161.953 | +1.15% |
| Picked run | 48 | 184.162 | 164.821 | +10.50% |
| Repeated notes | 44.1 | 133.345 | 119.404 | +10.46% |
| Repeated notes | 48 | 132.344 | 116.927 | +11.65% |
| Construction/control changes | 44.1 | 144.017 | 151.877 | -5.46% |
| Construction/control changes | 48 | 136.329 | 130.681 | +4.14% |
| Bends | 44.1 | 369.547 | 317.867 | +13.98% |
| Bends | 48 | 371.139 | 318.047 | +14.31% |
| Released tail | 44.1 | 101.497 | 92.729 | +8.64% |
| Released tail | 48 | 95.104 | 84.942 | +10.69% |

Twelve cells improve, but **44.1 kHz strums are 1.44% slower** and
**44.1 kHz construction/control changes are 5.46% slower** by the declared
median-mean metric. The aggregate passes; this is not a universal workload gain.

Across all repetitions each module has **246,072 measured callbacks**.
Baseline has **49 charged deadline misses**, with maximum **1858.667 µs**;
candidate has **eight**, with maximum **1722.500 µs**. The host periods are
1451.247 µs at 44.1 kHz and 1333.333 µs at 48 kHz. Elapsed wall maxima are
**19661.834 µs baseline / 32871.208 µs candidate**. The candidate has fewer
charged misses and a smaller charged maximum, but a larger worst wall callback.
These results do not establish zero misses or a hard realtime guarantee;
charged spikes and all repetitions are retained rather than attributed away
to scheduling. Performance on alternate settings was not admitted to this
fourteen-cell timing metric.

## Sound evidence and final provenance

All fourteen primary renders compare every Main Left/Right float and written
flag exactly. Five further exact comparisons cover maximum Detune at 44.1 kHz
host/1x and 96 kHz host/4x, plus 24-second tail/idle/reattack sequences at 48 kHz
host/2x with Room 0%, 10% and 100%. Their reattack at 20 seconds follows over
13 seconds of unwritten host silence; nonzero replay and output-write timing
match. False flags establish write elision, not internal zero state; the later
written replay supplies external parity evidence.

The [nineteen-case quality receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/quality/receipt.json)
retains source/module hashes, all commands, settings and byte comparisons.
These quality renders ran separately from the declared performance comparison;
any concurrent quality-run clocks are excluded. Exact output on these event
streams establishes unchanged sound for them, rather than a new listening
preference or an exhaustive all-state/host proof. The final canonical broad
run also passes all physical, chorus, energy, capture, room and idle checks.

The refreshed baseline contains the final sound source with only the feedback
hoist and BodyBank ordered-stereo/tiny-state spans reverted. All eighteen DSP
inputs and the four Rack wrapper inputs are hashed in the
[final source receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/source-receipt.json).
Canonical Engine.cpp/header match the final candidate values below.

| Frozen iteration-09 input | SHA-256 |
| --- | --- |
| Baseline Engine.cpp | `7902cae9e6f48598441386b10017ccb1e982b969979970f42c70a49479234a45` |
| Candidate Engine.cpp | `1d086c4052e9509529adc4e526409778d81a28e8067b049b6f3fefff54e45a27` |
| Shared Engine.h | `9c1b15f2e18882cd7d40b9f0f19c92f0a9b826cfbb011dd0f152d03c62229140` |
| Baseline SDK arm64 module | `7171a87ce307654cce755de53d239d00af8ec9ccb72d8a85b5e9be84f1160e5a` |
| Candidate SDK arm64 module | `5235965419491e7d158d306f69d27520ff4b68c55dbe3910abeb9b9287ed1707` |
| Primary benchmark | `54a3c83ef7f49f5dd6eef404f5518a8b41b9633d608b3fc6b86af5345d42bc5f` |
| Extra quality benchmark | `51300f93ace66f95a0c1523c95e0b027d1bdaa877616f3356d3530f01dd4f275` |

[Frozen baseline DSP](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/baseline-dsp),
[frozen candidate DSP](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/candidate-dsp),
[baseline native stage](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/baseline-native) and
[candidate native stage](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/candidate-native)
retain the matched build inputs and outputs. Source changes require another
matched quality/performance comparison.

## Historical iteration-08 checkpoint

Iteration 08 used the pre-Age-fix sound baseline and the same three CPU spans.
Its subsequent full-suite run passed 89/90 checks and identified the shared
Age-transition defect. The defect is absent from the corrected iteration-09
baseline and candidate; the original failed log and investigation are retained.
These old numbers are preserved for provenance and are not the final source's
performance result.

The [historical quality receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-08/quality/receipt.json),
[historical timing decision](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-08/timing/decision.json) and
[all historical repetitions](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-08/timing/runs.jsonl)
retain its original fourteen-case parity and timing evidence.

| Workload | Host kHz | Baseline mean µs | Candidate mean µs | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Held chord | 44.1 | 118.087 | 110.353 | +6.55% |
| Held chord | 48 | 122.076 | 113.632 | +6.92% |
| Queued strums | 44.1 | 179.534 | 167.025 | +6.97% |
| Queued strums | 48 | 153.129 | 161.411 | -5.41% |
| Picked run | 44.1 | 141.961 | 134.160 | +5.50% |
| Picked run | 48 | 141.476 | 134.116 | +5.20% |
| Repeated notes | 44.1 | 98.588 | 90.749 | +7.95% |
| Repeated notes | 48 | 98.299 | 90.688 | +7.74% |
| Construction/control changes | 44.1 | 111.860 | 103.661 | +7.33% |
| Construction/control changes | 48 | 112.719 | 105.314 | +6.57% |
| Bends | 44.1 | 327.389 | 278.392 | +14.97% |
| Bends | 48 | 281.293 | 253.626 | +9.84% |
| Released tail | 44.1 | 73.225 | 67.225 | +8.19% |
| Released tail | 48 | 75.120 | 69.730 | +7.18% |

Thirteen cells improve, but **48 kHz strums are 5.41% slower** by the declared
median-mean metric. The aggregate passes; this is not a universal workload gain.

Across all repetitions each module has 246,072 measured callbacks. Baseline
has **three charged deadline misses**, all in 44.1 kHz bends, with maximum
**1651.875 µs**; candidate has **one**, also in 44.1 kHz bends, with maximum
**1851.584 µs**. The host periods are 1451.247 µs at 44.1 kHz and 1333.333 µs at
48 kHz. Candidate's worst charged callback is larger despite fewer misses.
Elapsed wall maxima are **19518.208 µs baseline / 7961.166 µs candidate**.
These results do not establish zero misses or a hard realtime deadline guarantee;
charged spikes are preserved rather than attributed away to scheduling.

At iteration 08, the baseline was the settled, uncommitted sound repair beyond `1d1f432`, not
that commit alone or the earlier `8ea51f9` instrument. Its source snapshots and
all eighteen input hashes are retained. Canonical Engine.cpp matched the
candidate hash below before the subsequent Age-transition precision repair.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline Engine.cpp | `77a321c56105a856468507f8a85ec0a880ca4922e6ea7eadbb12d51997907c0f` |
| Candidate Engine.cpp | `d7d5705ac2d465a68ec9f23b49165f7cc6d8c7d78b1af247d7c1606aa656d534` |
| Shared Engine.h | `5c5329f3bfbc06dede75632bd71c44ca65f4500c4b747551134885ed3f16b9ff` |
| Baseline SDK arm64 module | `2e205c98377ff30b56120dc26a8306d9188c7caee45e660c38fa9a49b7de80c0` |
| Candidate SDK arm64 module | `744efb6a2ded57ab856ef471ef868802338e8a92e7b3989a59f0fa5c9defa07b` |
| Benchmark executable | `54a3c83ef7f49f5dd6eef404f5518a8b41b9633d608b3fc6b86af5345d42bc5f` |

See [complete source receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-08/source-receipt.json),
[frozen baseline DSP](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-01/base-dsp),
[frozen candidate DSP](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-08/combined-dsp), and
[candidate SDK stage/module](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-08/combined-native).
The build and matrix tools are the Rack project's `Tests/cpu/build_native.py`
and `Tests/cpu/run_cpu_matrix.py`; the event/measurement host is
`Tests/cpu/AcustraNativeCpuBench.cpp`. Build outputs and clocks must be refreshed
if source changes.

## Rejected and diagnostic experiments

All older source snapshots, native modules, failed gates, audio, clocks and
receipts remain under the Rack validation directory. No failed gate was widened,
no run was replaced until it passed, and the retained code excludes rejected
components.

| Probe | Recorded result | Disposition |
| --- | --- | --- |
| Bridge vector guard only | Exact seven-scenario pilot; 48 kHz chord mean gain about 2.23% | Below 5%; excluded |
| Iteration 01: bridge + capture SIMD + finite settled glide | Exact audio; active aggregate **1.625% slower**; baseline/candidate charged misses 12/8 | Rejected |
| Iteration 02: all-mode pressure sum regrouping | 44.1 kHz strums peak error **1.0728836e-6 > 1e-6**; relative RMS error -129.05 dB, absolute RMS 5.930e-8 | Rejected by declared peak gate |
| Iteration 03: feedback hoist alone | All fourteen audio comparisons exact; aggregate gain **0.849%**; charged misses 23/10 | Rejected as standalone stage; reused within the measured combined stage |
| Iteration 04: generic paired stereo additions | Exact single chord pilot; mean **0.948% slower** | Diagnostic only; packing overhead remained |
| Iteration 05: generic paired stereo + hoist | Frozen source, no module/retention measurement | Unbuilt refinement; not admitted |
| Iteration 06: static/interleaved paired stereo siblings | Zipped chord pilot exact; mean gain **4.645%** | Pilot only; not a fourteen-cell retention result |
| Iteration 07: integer predicate | All 16,814,080 state-bit classifications match | Untimed component; admitted only within the combined measured stage |
| Iteration 08: hoist + zipped stereo + integer predicate | All fourteen raw comparisons exact; aggregate **6.9071%** | Historical checkpoint; superseded by matched sound-source recheck 09 |
| Iteration 09: same three optimizations, repaired sound baseline | All nineteen raw comparisons exact; aggregate **7.2642%**; all 90 canonical tests pass | Retained; package/host validation pending |

The rejected records are
[bridge pilot](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-guard-probe/pilot-timing/summary.json),
[iteration 01](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-01/timing/decision-data.json),
[iteration 02](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-02/quality/receipt.json),
[iteration 03](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-03/timing/decision.json),
[iteration 04](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-04/pilot/receipt.json),
[iteration 05](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-05/retention-criteria.json),
[iteration 06](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-06/zipped-pilot/receipt.json) and
[iteration 07](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-07/source-receipt.json).

The final [all-target build log](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/final-canonical/build.log)
confirms C++17 compatibility, and the [full CTest log](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/final-canonical/ctest.log)
records **90/90 passed in 667.67 seconds**, including required Python tool
checks. These checks used final Engine.cpp/header hashes `1d086c4…` / `9c1b15f…`
from the iteration-09 source receipt. Package, compatible host creation and
interactive host testing remain separate pending release checks.
