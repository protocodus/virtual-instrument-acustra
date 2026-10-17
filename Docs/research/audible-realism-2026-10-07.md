# Audible guitar gestures — 2026-10-07

This report describes the frozen gesture-only checkpoint, before the
subsequent [recording-based Finger release change](recording-realism-2026-10-07.md).
Its byte-parity and CPU results apply to that checkpoint; the audition ZIP
preserves those exact versions.

Automatic return strokes now reverse the fresh sideways string release,
and attacks under pitch slides and MPE tension bends follow the string's
actual geometry. The changes reuse the existing two string planes, contact
transport, bridge and body. They add no filters, modes, random draws or
audio-thread allocation.

## What changes

The performer already alternated the strings' traversal order. It now also
passes the stroke direction to the engine. An upstroke reverses the fresh
parallel displacement, release velocity and parallel contact sources while
retaining the normal static-force release. Each scheduled string captures
its direction. Existing waves, packets in flight and retained refret tails
keep their original signed source gains. A held-note repeat and an ordinary
single pluck both capture their own direction too.

This is an authored approximation to a returning hand, not a measured
upstroke force trajectory. It changes interference between heave and rocking
radiation and between a new release and a continuing string. It does not
establish a listener preference.

A fresh attack now uses the speaking length and fractional fret already
computed for the performed pitch. A longitudinal slide moves the picking
point relative to the shortened string; an MPE member tension bend keeps
the fret and hand position fixed but changes the release speed. Natural
harmonics retain the full string length. Virtual downward slides longer than
the open string cannot amplify the nominal release displacement.

Queued attacks refresh geometry when the pick actually arrives, including
controller changes between the engine's regular control updates. Pick
releases at rates other than 48 kHz also refresh their 48 kHz release grid
when their attack configuration changes. An exact configuration key reuses
unchanged reference geometry, and a retained repeat keeps its old loop and
filter histories.

## Audible comparison

The frozen baseline is commit `3d37a38`; the candidate is the final working
source, with individual source/library/renderer hashes retained. The same
renderer plays sixteen eight-second pairs: slow and rapid Pick strums, and
Finger, Pick and Thumb slid attacks, on Original and Bellido, dry and with
Room at 50%. All pairs use identical event scores and common unity gain,
with no independent normalization or external processing.

The first 0.6 seconds of every pair are exact, including the initial
downstroke or unslid reference. First nonzero output is frame 9608 in both
versions. All renders are finite, nonsilent and below unity; the largest
candidate peak is 0.6384.

The candidate-minus-baseline signal in the slow/rapid strum sections is
3.07–4.31 dB below baseline RMS. Whole-strum loudness changes by +0.1–1.0 LU.
The Original Pick high-string slid attack changes 3–8 kHz power by −5.55 dB
dry and −6.09 dB with Room. These are substantial changed-signal observations,
not perceptual realism ratings. Relative native levels remain audible in
the comparison.

The generated `build-realism-oct07/listening/index.html` offers both versions
at the same playback position. The ZIP contains the page, lossless PCM16
audio, exact scores, analysis, provenance and reproduction instructions.
Browser decoding, playback switching, seeking and desktop/mobile layouts
passed, with no page or console errors. The ZIP passes CRC checks.

## Regression evidence

The focused suites check signed fresh-plane energy, delayed direction,
continuing arrivals and tails, strum alternation/rest/reset, block partition
invariance, physical-fret/slide equivalence, tension-bend release speed,
late controllers, extreme requests and reference-cache reuse. Retained Pick
releases are compared with independently refreshed reference geometry.
Disabling only their attack refresh causes 36 failures in the negative
control at 44.1 and 96 kHz; unchanged-controller and 48 kHz controls pass.

All 162 final ordinary unbent renderer configurations match the frozen
baseline byte-for-byte. They cover three rates, both models, all picking
styles, three Touch and Pluck Position settings, six physical strings,
velocity layers, release and upper-fret attacks. The exact report is retained
at `build-realism-oct07/attack-unbent-parity-final/report.json`.

Two test fixtures were corrected during the final regression run. The
broad-contact Fourier reconstruction now recovers expected aperture and
gain from the performed speaking fret and length. Its independent Fourier
calculation and `2e-6` tolerance are unchanged: the stale helper produces
27 failures with maximum error `2.83382e-6`, while the corrected helper
passes all twenty domains with maximum error `2.83667e-8`. Uniform-contact
and signed-wrap coverage remain intact; the original helper also passes
against the frozen original DSP.

The native strum probe reports onset in 1 ms hops. Its first downstroke
lands exactly at 6 ms in both the original setup and the current diagnostic,
so the first-onset bound is now inclusive. Other median and relative
assertions are unchanged. Off-tree mutations starting upward, disabling
alternation and disabling the rest reset each fail the repaired checks.
Their source, output and library hash are retained in
`build-realism-oct07/processor-strum-negative-controls`.

All 62 DSP/tool suites pass: 61 in the full final run and the full Engine
suite on its fixture-only rerun. Release DSP, tool and strict C++17 builds
pass. The Linux VST3 and Standalone targets build, and both native
processor/tempo-release suites pass after the onset-fixture repair. Logs
and JUnit results are retained in `build-realism-oct07/final-all-tests.log`,
`build-realism-oct07/engine-fixture-repair-tests.log` and
`build-realism-oct07/final-host-fixture-tests.log`. Production source stayed
unchanged through these fixture repairs and still matches the frozen
source used for the audio comparisons and CPU measurements.

Build the DSP, tests and listening renderer without JUCE:

```sh
cmake -S . -B build-dsp -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON \
  -DACUSTRA_REQUIRE_PYTHON_TESTS=ON
cmake --build build-dsp
ctest --test-dir build-dsp --output-on-failure -j 1
build-dsp/AcustraRenderAudibleRealism NEW_OUTPUT_DIRECTORY
```

Compile that identical renderer against the frozen baseline's own headers
and DSP library for the other half of the comparison.

## CPU measurements

The paired native Performer/DSP harness runs both frozen versions in one
executable on an AMD EPYC 9V74, pinned to CPU 4, with GCC 14.2 and matching
`-std=c++20 -O3 -DNDEBUG -fPIC` flags. It covers 112 cases: 48/96 kHz,
64/128 frames, both models, Room 0/50%, and seven attack/held scenarios.
Each case has 32 warmup pairs and 128 measured pairs, with alternating
baseline/candidate order. Preparation, preroll, fixed-storage snapshot
restoration, output checking and file writes are outside timing. Restoring
the snapshot warms instance memory; these are hot native callback costs,
excluding host/JUCE overhead. Rendering, builds and other tests were idle.

| Scenario | Median paired CPU change across its 16 cases |
| --- | ---: |
| Initial downstroke | −0.12% |
| Ordinary return strum | −0.59% |
| Held chord | +0.15% |
| Fresh chord after downward slide | −0.01% |
| Fresh chord after upward slide | +0.15% |
| Queued contact after a changed bend | +8.0%, +23.2 µs |
| Retained return stroke after a changed bend | +5.8%, +26.7 µs |

The two changed-controller costs are concentrated at 96 kHz, where a new
reference-rate Pick solution is needed: median additions are 47.7 µs
(+19.1%) for the queued contact and 54.1 µs (+13.6%) for the retained return.
At 48 kHz those same scenarios remain essentially flat. These are attack
costs; held processing gains no new filter or simulation stage.

P95 tails vary on this cloud processor. Dense new Original chord attacks at
96 kHz/64 frames already exceed their nominal deadline in the baseline, so
these results establish neither a host CPU ceiling nor a deadline guarantee.
Across all observations, baseline/candidate deadline exceedances are
1270/1283. Initial downstroke and held audio are exact in all sixteen
configurations each. Full raw timings, quantiles, checksums, compiler/source
hashes and the timing affinity are retained under `build-realism-oct07`.
`Tools/AudibleRealismBenchmark.cpp` documents the seven-object frozen build;
the run takes a new JSON path, measured-pair count and warmup count.
