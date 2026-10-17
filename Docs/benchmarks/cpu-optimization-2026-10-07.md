# Acustra CPU optimization — 2026-10-07

This pass reduces redundant work in the published natural-performance model
without changing its musical behavior. The baseline is `3d5b7c7`; final DSP
is frozen at `99fce22`. There are no new controls, changed quality settings,
extra input latency, reduced mode counts or skipped simulation samples.

## Changes and their scope

**Pitch geometry.** The existing voice configuration cache was reached only
after repeated exponential, pitch and string-geometry calculations. A small
fixed per-voice cache now reuses those pure values when all their exact inputs
match: stopped/open MIDI, physical string, host-rate float bits, performed and
member bend, actual vibrato interval, and attack-pitch bits. Slides and
fixed-length tension bends remain separate inputs even at equal final pitch.
The original configuration cache still owns model, calibration, bridge-anchor,
filter and transition decisions. Speaking length, speaking fret and contact
period are refreshed before that original cache lookup; clear/reset and Pick
reference paths still run. Copies carry the new key and values together.

**Continuing plucks.** The work calculation and merge previously performed
the same fractional alignment of the fresh wave twice. They now share the
exact float results. Both fresh planes are completed first; their work and
merge passes then reuse the existing two Pick-release scratch buffers. Bounds,
accumulation order, energy-derived force, retained waves and filter histories
are unchanged. This adds no delay-sized storage or audio-thread allocation.

**Pick Gaussian tails.** Profiling the 24-event Gather stress fixture found
repeated attack construction, especially Gaussian contact evaluation, to be
the main cost. Chord allocation was negligible in that profile. The Gaussian
edge still uses the original libm calculation wherever its value can matter.
For IEEE binary64 under nearest rounding, `erfc(x)` for `x >= 28` is less than
`7e-343`, below half the smallest double subnormal; the opposite tail rounds
to two. The existing half-scaled edge therefore returns exactly zero or one
there without calling libm. Its image sum and near-edge arithmetic retain
their original order. Other rounding modes and other double formats use the
original path. The caller's rounding/flush modes are never changed by the DSP.

Avoiding redundant libm calls can change underflow/inexact exception flags or
`errno`; those are outside the audio/public-state equivalence claim. The new
standard-library `fegetround` reference is checked by native builds, with
actual Reason SDK linkage subject to the platform limitations below.

## Validation

All 69 DSP/tool suites and both native host suites pass, with no skips.
Linux VST3 and Standalone builds pass, as do GCC and Clang strict C++17
builds. No actual Reason SDK package/link, Windows or macOS build was run.

The final dual-build comparison passes 624 configurations, including 200
alternate block partitions: 85,745,000 frames, 245,868,600 audio sample pairs
and 10,516,584 public-state words match exactly. GCC runs the full matrix in
both inherited and FTZ/DAZ modes. Clang independently compiles both versions
and runs the quick matrix in both modes; GCC also runs directed rounding
up/down/toward-zero checks. Both trees use strict C++17 Release flags. This is
same-toolchain equality, not a promise of identical samples across compilers
or platforms; private padding and cache storage are intentionally not compared.

All 24 previously delivered listening performances were freshly rendered from
the optimized model. Their raw floats, WAVs, MIDI scores and physical-string
allocations match the published `3d5b7c7` files exactly. Existing listening
comparisons remain valid for this optimized model.

Focused tests cover every pitch-cache dependency, signed zero, equal pitch
with different physical bend geometry, copied state and clear-delay behavior.
Forced recomputation compares Main L/R, auxiliary Piezo and relevant internal
transition/reference state. Re-pluck tests cover fractional alignment limits,
wrapped indices, energy sums, scratch canaries, gains and retained loop state.
Gaussian tests compare native libm bits around the cutoff and through the
contact edge, in all four rounding modes with FTZ/DAZ enabled and disabled.
An intentionally incorrect always-nearest shortcut fails its negative control.

The separate dual-build harness compiles each source tree against its own
headers. It compares every Main/Piezo sample bit and serialized public state,
including alternate block partitions. Its authored performance battery covers
both models, all picking/capture styles, construction/tuning/master-tune
changes, age and hand controls, MPE, conventional bends, Gather, repeated
strokes, release/tempo boundaries, tails, panic and prepare/reset. Corrupting
one audio bit or one public-state word makes the comparison fail.

## CPU protocol and results

On this AMD EPYC 9V74 cloud machine with GCC 14.2, median paired callback
cost falls **1.53% in inherited native mode** and **1.32% with JUCE's FTZ/DAZ
settings** across the full matrix. The gain depends strongly on the workload:

| Scope | Cases per mode | Native inherited reduction | JUCE FTZ/DAZ reduction |
| --- | ---: | ---: | ---: |
| Complete callback matrix | 1,030 | 1.53% | 1.32% |
| Preserved Pick matrix | 112 | 12.50% | 1.90% |
| Pick matrix at Room 0.5 | 56 | 12.30% | 1.82% |
| Chord held for two seconds | 88 | 1.23% | 1.13% |
| Dense Gather bursts, all styles | 88 | 3.99% | 3.89% |
| Dense Gather bursts, Pick | 24 | 34.36% | 3.94% |

Each percentage is one minus the median of per-case paired time ratios,
not a promised reduction in a DAW's overall CPU meter. Across all cases,
median absolute p50 differences are −1.092/−1.602 microseconds for
inherited/FTZ modes. The separate median p50 times are
142.68→141.50 / 142.06→140.44 microseconds, and median p95 times are
199.15→196.21 / 187.56→189.06 microseconds. These independent medians do
not reconstruct the paired ratios. Tail timings do not uniformly improve;
for Room 0.5 Pick cases, FTZ median p95 changes from 449.60 to 463.00
microseconds even though paired median cost improves.

Not every case improves in the first sweep: its worst paired median changes
are +2.98% in inherited mode and +8.77% in FTZ mode. The latter is Classical
78 Thumb ordinary key-up at 96 kHz/64 frames (p50 69.905→75.584 microseconds).
Two additional quiet runs of the 72-case Thumb/stereo matrix, each with 512
pairs after 64 warmups, did not reproduce that increase: the same case's
paired medians improve 0.96% and 0.87%, with p50 69.014→68.704 and
69.655→69.144 microseconds. All output hashes still match. These follow-ups
document the variability; they do not replace the original matrix or erase
its slower observations.

Deadline limits remain. Cases whose p95 exceeds the block budget change
from 69→57 in inherited mode and 58→57 in FTZ mode; over-deadline trials
change from 6,467→5,336 and 5,627→4,984 respectively. The worst current FTZ
p95 is still 3.858 ms against a 0.667 ms budget for an extreme 24-event Pick
burst at 96 kHz/64 frames (previous 4.112 ms). This is an improvement to
existing work, not a complete solution to that extreme queue-burst deadline.
No event is dropped or deferred to obtain these savings.

The preserved 1,030-case callback matrix includes the previous 112-case Pick
matrix at Room 0 and 0.5 plus 918 mechanism, capture, force, Touch and transition
cases. Source, tools and binaries are frozen before measurement. Each case
uses 16 warmup pairs and 128 measured pairs with alternating old/new order on
one pinned CPU. State proofs and output-hash comparisons run outside timing;
construction, preroll, snapshot restoration, export and reporting are excluded.
Final timing starts only after builds, tests and rendering jobs are idle.

The native benchmark inherits the caller's denormal mode by default. JUCE's actual
callback uses `ScopedNoDenormals`, so a second matched matrix explicitly enables
x86 FTZ/DAZ for both versions. These settings can substantially change absolute
Pick costs; results must be compared within the same mode. Both are hot native
DSP measurements on a shared cloud machine, excluding host overhead and cold
cache scheduling. Median improvements alone do not establish deadline safety.

## Reproduction and retained evidence

```sh
# Freeze source trees first; the helper refuses an existing build directory.
bash Tools/BuildCpuOptimizationComparison.sh old-source new-source parity-build
parity-build/AcustraCpuOptimizationComparison parity.json
parity-build/AcustraCpuOptimizationComparison parity-ftz.json --flush-denormals

bash Tools/BuildAudibleRealismBenchmark.sh old-source new-source cpu-build
python3 Tools/RunCpuComparison.py --binary cpu-build/AcustraAudibleRealismBenchmark \
  --output cpu-checks --untimed-check
python3 Tools/RunCpuComparison.py --binary cpu-build/AcustraAudibleRealismBenchmark \
  --output cpu-checks-ftz --untimed-check --flush-denormals
# Coordinate an idle machine before these timed commands:
python3 Tools/RunCpuComparison.py --binary cpu-build/AcustraAudibleRealismBenchmark \
  --output cpu-times
python3 Tools/RunCpuComparison.py --binary cpu-build/AcustraAudibleRealismBenchmark \
  --output cpu-times-ftz --flush-denormals
```

The timing runner rejects unequal old/new output hashes, incomplete trials,
wrong transition states and binary changes. It preserves old output directories.
The sample comparison uses direct bit comparisons rather than relying on hashes.
Build helpers retain compiler flags, source/tool hashes and binary identities.

Local evidence is under `build-cpu-oct07/`:

- `final-validation.json` indexes the frozen Source, all 71 tests, native
  binaries, audio evidence and timing receipts by hash.
- `parity-final-receipt.json` summarizes the full GCC and independent Clang
  comparisons, directed-rounding runs and comparator negative controls.
- `published-audio-parity.json` verifies the 24 existing listening performances.
- `cpu-summary.json`, `timed-gradual/` and `timed-ftz-final/` retain summaries,
  every paired observation, exact output hashes and command/build provenance.
  The interrupted `timed-ftz/` directory is preserved and excluded.
- `thumb-ftz-followup-summary.json` retains the two longer checks of the
  short-callback regression, with raw results in the adjacent numbered files.

The previous natural-performance listening packs and validation directories
remain untouched. Large outputs are local artifacts rather than repository assets.
