# Acustra CPU optimization, 2026-10-23

Six exact-output changes, one commit each, from baseline `45d6626` (1.6.3).
Every rendered sample keeps its bits; no model, control, precision, rate,
quality setting or event timing changes. Version 1.6.4 carries them.

| Commit | Change | GCC instructions | Clang instructions |
| --- | --- | ---: | ---: |
| `f3baf66` | Room early reflections: masked ring index, contiguous arrival reads | −1.32% | −2.6% |
| `de24c8b` | GCC inlines `StringLoop::advance`, `bridgeVelocity`, `FixedDerivative::process` | −4.73% | — |
| `4d65f6d` | GCC inlines `readDelay`, `StringLoop::write`, `BridgeMode::processPast` | −3.43% | — |
| `258f433` | Bridge modes read packed coefficient pairs and double residues | −2.30% | −2.30% |
| `eeceb6c` | Contiguous arrival read also across the ring's wrap | −0.22% | −0.21%¹ |
| `facb1e4` | Room late-field Hadamard butterflies written out | −0.97% | ¹ |

¹ Measured together for Clang. A dash: the change is GCC-only, because Clang
and MSVC already inlined those functions through `ACUSTRA_ALWAYS_INLINE`.
Each figure is relative to the previous commit.

## Workload and protocol

An off-tree driver plays a fixed performance through `acustra::Performer` at
48 kHz in 128-frame blocks, with the plug-in's new-instance defaults (Room 10%,
Release Noise 10%, Piezo Mix 20%, Output 0 dB) and every shipped realism
mechanism. Two 7.8 s performances, Finger/Dreadnought/Spruce and
Pick/Auditorium/Mahogany: a six-string held chord with vibrato, eight queued
six-string strums with alternating direction, a twelve-note legato melody, a
bent note and the release tail. Preparation is outside the measurement.

- Instructions: `valgrind --tool=callgrind`, collecting only inside
  `Performer::endBlock` and `Performer::handleMidi`. GCC 13.3 and Clang 18.1,
  both `-std=c++20 -O3 -DNDEBUG -fPIC` (the `build-dsp` flags), x86-64 SSE2.
- Time: thread CPU time (`CLOCK_THREAD_CPUTIME_ID`) of the block calls,
  baseline and candidate binaries alternating order in each pair.

The machine is a 4-core 2.8 GHz Xeon VM shared with five other agents' builds
and tests (load average 10–20 throughout). A baseline-against-itself run
spread about ±2%, so single changes under about 2% are not resolved in time
and none is claimed for them.

## Overall result

| Build | Instructions, before → after | Thread CPU, 24 pairs |
| --- | ---: | ---: |
| GCC 13.3 | 15,702,651,782 → 13,761,402,113 (−12.36%) | median paired −8.19%; medians 2.589 → 2.369 s |
| Clang 18.1 | 13,429,450,042 → 12,747,033,932 (−5.08%) | median paired −4.08%; medians 2.183 → 2.117 s |

Linux plug-in builds use GCC and gain most. macOS (AppleClang, arm64) and the
Rack Extension (SDK Clang) take only the room and bridge changes; arm64 uses
the scalar bridge path, which gains only the packed residues. Neither arm64
nor the SDK was measured here, so no figure is claimed for them, and no
measurement here is a host deadline guarantee.

## Exactness

- `Tools/CompareCpuOptimization.cpp` (GCC, its own `-Werror` build of both
  frozen trees): all 123 configurations plus 67 alternate partitions pass,
  165,634,428 Main/Piezo sample values and 8,728,538 public-state words, 8 to
  384 kHz, blocks 1–511, all techniques, captures, shapes, woods, Gather, MPE,
  construction changes, Room 0–100%. Its negative control fails as intended.
  `--quick` also passed after the first, fourth and final commits.
- An off-tree null test after every commit: 12 performances (4 shapes,
  3 woods, 3 techniques, 3 captures, velocities 30–118, Room 0/10/60%, Gather)
  at 48 kHz/128, 48 kHz irregular 1–511-frame blocks, 44.1 kHz/127 and
  96 kHz/64. Every Main L/R and Piezo float is identical to the baseline
  build's.

## Rejected and not attempted

- The ordered body-pressure sum, the bridge residue sums and the string
  recursions are latency chains that only reordering would shorten; that
  changes rounding, so it was not attempted (compare the rejected regrouping
  of 2026-10-13).
- Moving the bridge mode *states* into packed pairs would remove the
  remaining gathers, but every state reader (several test suites) addresses
  them through `heaveModes`/`rockModes`; not attempted.
- Pitch-bend calibration (`solvedDispersion`, about 420,000 instructions per
  miss) dominates bend-time spikes but has no exact shortcut for continuously
  changing inputs.
