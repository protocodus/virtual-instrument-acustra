# Acustra CPU optimization, 2026-10-24

Six exact-output changes, one commit each, reduce the native engine's mean
callback CPU by **14.8%** (GCC, x86-64, 48 kHz / 64 frames) and **6.6%**
(Clang) against 1.6.3 (`6254d8d`). Every compared sample and public-state
word is bit-identical. The sound is unchanged; there is no version bump and
no listening claim, as with the [2026-10-16 work](cpu-optimization-2026-10-16.md).

## The changes

| Commit | Change | Mean CPU reduction against the previous commit |
| --- | --- | ---: |
| `91980a0` | Take cos/sin of omega and 2 omega once per frequency in the tuning and dispersion solves (`AllpassTrig`) | 1.6% (bends 6.6%) |
| `2ce3205` | Reuse the unchanged delay or section lag in each collocation Jacobian column and accepted step | 1.3% (bends 4.9%) |
| `346af28` | Build `AcustraDSP` with `-fno-semantic-interposition` under GCC | 7.7% |
| `80611af` | Give GCC the existing always-inline request for `StringLoop::advance`, `bridgeVelocity` and `FixedDerivative::process` | 2.8% |
| `d373dea` | Store each bridge mode's heave and rock lanes side by side (`BridgeModePair`) | 2.3% |
| `3bbc218` | Run the capture filter's left and right sections as one two-wide vector (x86-64 SSE2 only) | 1.1% |

Figures are the geometric mean over the five workloads below of each
workload's mean-callback ratio, three repeats each (the capture filter
eight). Self-against-self runs of the same harness read 0.1 to 0.4%.

The first two are algorithmic and help every compiler. A dispersion design
is re-solved whenever a bend or vibrato moves inharmonicity by 0.2%; its
instruction count fell from 1.67e9 to 0.72e9 over the five-workload run
(callgrind), which is why bends gain most and their callback tail shrinks.

The next two are GCC code generation. `AcustraDSP` is `-fPIC`, and GCC never
inlines an interposable exported function, even inside
`AcustraEngine.cpp`: 599 intra-engine calls stayed calls. The library is
linked statically, so nothing interposes it. `ACUSTRA_ALWAYS_INLINE` was
empty under GCC because GCC rejects any call it cannot inline into a
function declared so, and tests call these members; the attribute now goes
on the three definitions only, with hidden visibility so it also holds in a
`-fPIC` build without the flag (the comparator's). Clang and MSVC are
unchanged.

The bridge layout keeps every lane's arithmetic and the serial residue sums;
tests read sections through `BridgeLoad::heaveMode(i)` / `rockMode(i)`.
The capture filter path and the bridge's vector path are x86-64 only; the
Rack Extension's arm64 build keeps the scalar loops, now over the paired
layout, and was not timed here.

## Measurement

Intel Xeon @ 2.10 GHz, 4 vCPUs shared with other agents' builds (load
average about 10), Ubuntu 24.04, GCC 13.3 and Clang 18.1, `-std=c++20 -O3
-DNDEBUG -fPIC` as `AcustraDSP` builds, plus `-fno-semantic-interposition`
on the candidate side under GCC. `Tools/SteadyCallbackBenchmark.cpp` holds
the protocol: baseline and candidate trees in one executable, the same
authored MIDI through Performer with the Piezo bus connected, 200 ms untimed
preroll, order alternating per callback, thread CPU time per callback (so
preemption is not charged), and a memcmp of every callback's Main L/R and
Piezo output. Build it with `Tools/BuildSteadyCallbackBenchmark.sh` and
summarise with `Tools/AnalyzeSteadyCallbackBenchmark.py`.

Workloads (six seconds each): a held chord re-struck every 2 s (Finger),
alternating Pick strums every 250 ms with Room 10%, a picked scale at eighth
notes, a held chord under a continuous pitch-bend sweep every 5 ms, and a
chord released after 1 s ringing out with Room 10%.

Baseline 1.6.3 against the final source (median callback in µs, mean
reduction, 95th percentile in µs):

| GCC, 48 kHz / 64, 3 repeats | Base p50 | Final p50 | Mean reduction | Base p95 | Final p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Held chord | 80.13 | 68.31 | 14.6% | 106.3 | 93.3 |
| Pick strums, room | 101.20 | 89.69 | 11.2% | 165.1 | 147.3 |
| Picked run | 82.92 | 70.56 | 14.7% | 143.1 | 122.6 |
| Bends | 99.54 | 87.62 | 19.5% | 366.2 | 256.5 |
| Released tail | 72.97 | 62.34 | 13.9% | 111.8 | 97.7 |
| Geomean | | | **14.8%** (repeats 14.7–14.9%) | | |

A separate five-repeat run measured 14.7%. At 96 kHz / 128 frames GCC
measures **13.7%** (11.2–16.4% per workload; repeats 13.6–13.9%). Clang at
48 kHz / 64 measures **6.6%** (3.2–13.7%; repeats 6.0–7.5%), bends' p95
392 → 286 µs. Thread CPU on a loaded shared machine still carries cache
and frequency effects; these are not host deadline guarantees, and the
SDK-native arm64 module was not measured.

## Exact output

The repository comparator (`Tools/BuildCpuOptimizationComparison.sh`, full
battery) compared 1.6.3 with the final source under GCC (candidate with
`-fno-semantic-interposition`) and, separately, under Clang 18: each passes
123 configurations plus 67 block partitions, 165,634,428 Main/Piezo sample
values and 8,728,538 public-state words, bit-identical. Each commit passed
the same battery against its predecessor; the bridge commit also passed the
quick battery with `__SSE2__` undefined on both sides (the scalar path,
37,123,200 values). Every timed callback matched as well.

Engine sources: baseline `AcustraEngine.cpp` `0b73c928…`, header
`423bf920…`; final `AcustraEngine.cpp` `84a40418…`, header `9f9386bc…`.

## Tests

The full `build-dsp` Release build (all DSP targets, the C++17 and extended
C++17 libraries included) has no warning from `Source/DSP`, and a strict
Clang C++17 `-Wall -Wextra -Wshadow -Werror` check passes. The full CTest
suite passes 84/84: `Acustra.VelocityRealism` exceeded its 60 s limit under
`-j2` on the loaded machine and passed alone in 45.7 s. The Python tool
self-tests were not registered (no NumPy/SciPy on this machine); no Python
tool changed. The plug-in build was not run.

## Tried and not kept

- GCC always-inline for `StringLoop::readDelay`: 0.3%, within noise.
- Always-inline declared in the header for GCC: GCC rejects the tests' own
  calls (`function body not available`), so the attribute went on the
  definitions instead.
