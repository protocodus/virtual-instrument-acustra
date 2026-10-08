# CPU optimization — 2026-10-08

Desktop version 1.1.2 retains one canonical DSP change: delay-ring indexing uses
an unsigned mask instead of repeated addition/subtraction. A compile-time
assertion requires a positive power-of-two capacity. Unsigned conversion makes
negative indices wrap to the same slot, and the active-history mask still
composes with the physical ring mask. No filter recurrence, event timing,
excitation, parameter, history length or tuning formula changes.

The baseline canonical source is `3c6b01d9b9847944da6344164ae12845ca1cf998`.
The retained `AcustraEngine.cpp` SHA-256 is
`a4f2fe7f8ae57c67b13f1792af995b68df1ebe9cd4ad6edb2a7fd99b81ada14e`.
The header and Performer remain identical to that baseline.

## SDK screening, not final release timing

The Reason adapter additionally avoids work on an unrequested Piezo output,
uses exact scalar classification builtins, and checks ownership before decoding
a deferred quality choice. Those changes belong to the Rack repository. The
following measurements combine that wrapper with the canonical ring change;
they do **not** establish a desktop plug-in speedup percentage.

The baseline Rack revision is `7839f09c877c624ba538277a76c06af863b74500`
(version 1.2.0f1). Both modules use the same SDK-native benchmark, on Apple
Silicon/macOS 26.5.1, with 64-frame callbacks, a one-second warm-up and two
measured seconds in each fresh process. Three repetitions alternate candidate
order. Our builds, tests and renders were idle; unrelated desktop work was
uncontrolled. These short runs select a candidate; final release timing and
protected-host validation are separate gates.

Times below are microseconds. Each statistic is the median of its three
per-process measurements, not a pooled percentile.

| Scenario | Baseline mean | Candidate mean | Mean change | Baseline p95 | Candidate p95 | Baseline p99 | Candidate p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Held chord, 48 kHz, 1x | 49.789 | 47.715 | -4.16% | 55.333 | 52.584 | 61.666 | 60.250 |
| Strums, 48 kHz, 1x | 74.445 | 70.978 | -4.66% | 88.459 | 83.875 | 138.167 | 140.333 |
| Held bends, 192 kHz, 4x | 297.165 | 285.880 | -3.80% | 360.916 | 347.584 | 406.667 | 395.541 |

The 48 kHz cases had no thread-CPU deadline misses. The demanding 192 kHz/4x
case still exceeded its 333.333 microsecond callback budget in 1,512 of 18,000
measured candidate blocks, versus 2,230 baseline blocks. Lower mean cost does
not establish deadline safety at that setting. The strum p99 also rose slightly;
the improvement is not uniform across every tail statistic.

The frozen baseline module SHA-256 is
`c485bd82becef0a3b505d10925979963f7b0ceca5ed565ac5cbd356f810c5d53`;
the selected screening module is
`9cf8982f5e62dd5c9b26b8b8c3e596c8a22824aef2dc2048635cf1f99dd910ce`.
Raw plans, module/source hashes, all timings and parity exports remain in the
Rack workspace's `Release/cpu-optimization-20261008/`, including
`ring-screen-plan.json`, `ring-screen-runs.jsonl`, `ring-sdk-parity.json` and the
preceding `screen*` evidence. An earlier run overlapped other work and is retained
under `ring-screen-overlap*`; it is not the selected serial comparison.

## Rejected experiments

Extending the existing x86 paired heave/rock bridge-vector path to the SDK and
ARM64 preserved native output bits but increased screened mean CPU by 4.21%
for the held chord, 1.09% for strums and 2.84% for high-rate bends. It was removed.

Hoisting the exact two sample-rate ratios once per process call also preserved
output bits, but its combined bridge/mask variant did not improve on the same
variant without the hoist. It was removed. The experiments do not justify
changing calculation order, filter precision or event scheduling. Capture
stereo vectorization and complex-zero predicate changes were inspected only;
neither is included.

## Validation

Final canonical validation is recorded alongside the frozen probe and raw
outputs in `build-cpu-optimization-20261008/`. The native comparison serializes
Main L/R and Piezo samples plus explicit live/fading bridge coefficient and
history fields. It covers both models, all three picking techniques, all woods,
shape/model transitions, MPE bends, re-plucks, palm mute, release and reset over
512 blocks of 64 samples per case. It does not serialize every unrelated
private field and is not a cross-toolchain bit-equality claim.

There are 48 normal-layout cases at 44.1, 48, 88.2, 96, 176.4, 192, 352.8 and
384 kHz, and 60 extended-layout cases adding 705.6 and 768 kHz. The SDK-side
comparison independently covers 20 scenario/rate/quality cases, including a
16-second quality-switching sequence: Main L/R and output-written flags match
the baseline byte for byte. Separate wrapper tests cover Piezo and reconnection
semantics. The final ring-only native comparison passes all 108 cases against
the frozen baseline: normal export SHA-256
`5d99ce60c160fe7f23df8f5f818bad89206578f6131fe01fbb23eed1eaa19f89`, extended
export `9148c74a5cfa34a3c8d702f6783f8c0b1db4d28a325fc9b4499294dab941ca39`.

The complete applicable JUCE-free suite passes 72/72 in 153.59 seconds with
two concurrent test workers. All targets, including normal and extended C++17
compatibility builds, compile successfully. The distribution metadata tests
pass 20/20. These checks use Apple Clang 21, Release settings and Python 3.11
with the required NumPy/SciPy tests registered. Final universal macOS formats,
host integration and packaging are validated separately against the published
clean source identity.

These are numeric and functional checks. No listening preference or sound
improvement is claimed for this optimization.
