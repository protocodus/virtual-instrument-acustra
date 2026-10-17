# Acustra CPU optimization — 2026-10-05

Acustra now caches unchanged delay calculations and processes paired bridge
resonances together. On this cloud machine, median DSP callback time falls
14.3% with GCC and 4.8% with Clang. Every tested output
sample retains its exact bits, including the continuing-string tremolo and
global 1/32-note release window.

## Implementation

`FixedDerivative` caches the clamped whole/fractional reference delay using
the sample-rate ratio's exact float bits. Its history still advances on every
sample. A history reset can retain this rate-only geometry; a new rate causes
a miss. Both history reads and their original interpolation stay intact.

`StringLoop` caches the delay tap and Thiran coefficients using the delay
input's exact float bits. Every filter/history update keeps its original
arithmetic. Reset invalidates the key; whole-loop copies carry the key and
coefficient tuple together. Bends, retuning and automation still recalculate
when the actual input changes, with no parameter quantization.

On x86-64 GCC/Clang SSE2 builds, one bridge mode's heave and rocking histories
advance in two double-precision lanes, each with its own coefficients. The
original multiply/add/subtract grouping and serial residue-sum order remain.
Inactive rocking states stay untouched. Other targets retain scalar mode
processing. No fast-math flags, reduced mode counts, altered model parameters,
extra allocations or skipped simulation samples were introduced.

## Timing protocol and results

The machine exposes five x86-64 CPUs on an AMD EPYC 9V74, Debian 13,
Linux 6.18.44. Compilers are GCC 14.2 and Clang 19.1. Both frozen versions use
`-std=c++20 -O3 -DNDEBUG -fPIC`, with the same warning flags.

Baseline and candidate run in separate namespaces in one executable, on the
same authored event stream. Callback order alternates after at least 200 ms
of untimed preroll. Only processing is timed; construction, preparation,
comparison, hashing and JSON output are excluded. Final timings run serially
with other builds, audio checks and tests idle.

Each run requests up to 2048 callback pairs per configuration, ending when its
fixed performance ends. Three GCC runs contain 54,315 pairs across 18
configurations: 48 kHz/64 and 128 frames, and 44.1/96 kHz/127 frames. They cover
idle, six strings, tremolo, queued strums, exact automation, room, both guitar
models, the capture/picking modes and enabled observers/Piezo output.

For each configuration, the table uses the median of three run medians for
each version. At 48 kHz and 128 frames:

| Scenario | Before, µs | After, µs | Reduction |
| --- | ---: | ---: | ---: |
| Silent idle | 141.67 | 115.07 | 18.8% |
| Six held strings with bends | 165.46 | 141.70 | 14.4% |
| Same-string tremolo and release grace | 149.91 | 124.48 | 17.0% |
| Queued six-string strums | 231.58 | 198.73 | 14.2% |
| Parameter automation and cache stress | 185.56 | 158.97 | 14.3% |
| Room/capture ring-out | 154.66 | 127.89 | 17.3% |

Across all 18 configurations, GCC's per-configuration median reductions range
from 9.5% to 19.2%; their median is 14.3%. The independent Clang run
has 18,105 pairs and a 4.8% median reduction, ranging from
2.2% to 7.1%. All configuration medians improve. Tail timings are more
variable, including some Clang p95 regressions; these cloud wall-clock results
do not establish a host deadline guarantee or a speedup on other processors.
Raw paired timings and p95/max values are retained.

A separate body-filter tiny-value flush prototype passed exact audio checks
but regressed most measured medians, so it was discarded. The final change
contains only the two delay caches and paired bridge modes.

## Audio and build validation

GCC's full comparison passes 320 configurations and compares 218,473,200 raw
Main L/R and auxiliary Piezo float values over 72,824,400 frames. It covers
8/44.1/48/96/192/384 kHz and blocks of 1/17/31/32/33/127/511 frames. All sample
bits, frame counts, public states and synthetic partition comparisons match.
Non-idle cases must remain finite and produce nonzero audio. The battery
includes sustain, MPE, string-per-channel, Gather, construction changes,
retuning, release/tempo boundaries and panic.

Clang independently recompiles both versions and passes 35 configurations,
comparing 18,088,200 raw float values with zero audio/state/count mismatches.
Every final timing run also checks exact audio and public-state equality.

All ten standard demos freshly rendered from the optimized library match
the pre-optimization `acustra-demos-release-join.zip` byte-for-byte: PCM16
stereo at 44.1 kHz, 5,721,600 frames total. Recuerdos is included. Existing
production archives and listening packs remain intact.

Twenty targeted DSP/demo/repertoire suites and the native host-tempo release
suite pass. Release VST3 and standalone builds pass, as does the C++17 DSP
build. The broader native processor suite was not rerun.

At the CPU validation checkpoint, the Reason mirror's six canonical files
matched the main DSP byte-for-byte.
Strict Clang C++17 syntax checks with `-Wall -Wextra -Wshadow -Werror` and the
mirrored ReleaseJoin suite pass. Extension checks 1–10 pass; its existing
release guard rejected the dirty Instrument submodule. The submodule
HEAD and parent gitlink are unchanged. The genuine Reason SDK is unavailable,
so SDK wrapper tests remain uncompiled and unrun.

## Frozen provenance and reproduction

The baseline includes the preceding uncommitted global 1/32/repeated-string
work beyond repository HEAD `8dccaa5`; that commit alone is not the baseline.

| Input | SHA256 |
| --- | --- |
| Baseline engine CPP | `8feceddbfd96ff5ab514f51c59972de646453001937d2f4804ad64eaa4728f7b` |
| Optimized engine CPP | `87e427617873713c76bd8f699c9cb6c8d3d7b9138d81bcebfc9703479f646f77` |
| Optimized engine header | `d966c8afe1b87f27cd7ac11ed1bf0dba259e2e2b04371b3035e6ad4aec1563a2` |
| Baseline DSP library | `fc73cbf32cd9b34f5742bb3b4ebfabe28da97420a7c739c164a97d859eef3abe` |
| Optimized DSP library | `993ffd294f01d6b8d387f221e34b08fb15d13ad5a564914fe6812c9fccbc1a94` |

`acustra-cpu-optimization-evidence.zip` contains the frozen baseline and
candidate inputs, isolated comparison harness, build manifests, raw timing
reports, audio comparisons, demo hashes and build/mirror receipts. Its README
provides GCC/Clang rebuild commands. Workspace evidence is under
`.cloud-setup/acustra-cpu`; the earlier production WAV archive is preserved
separately. Numerical equality establishes preservation of the tested audio;
this change makes no new listening-preference claim.
