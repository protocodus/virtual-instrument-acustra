# Finger release and real-recording benchmarks — 2026-10-07

Finger releases now apply the existing finite release slip at full force,
retaining the displacement's AC-spread normalization. Previously, an inverse
of the full-velocity slip cancelled that smoothing. Removing the inverse
produces a rounder upper-partial balance. The existing 0.2 mm release radius
is an authored approximation, not a newly measured fingertip dimension.
Thumb retains its relative release, and Pick is unchanged. The earlier
directional-strum and slide/bend geometry changes remain in place.

This isolated change improves the known Finger recordings and the aggregate
performance comparison below. It has a real tradeoff on brighter Martin
recordings. Descriptor losses establish closer measured features, not a
listener preference or universal realism ranking.

## Recorded references and fixed protocol

The experiment freezes the current working tree and binaries before the
Finger change. Original uses Dreadnought/Spruce and Bellido 1978 uses
Auditorium/Mahogany, dry Stereo Mic, with identical controls and shipping
calibration. No parameter grid or per-row velocity fit is used.

The verified CC0 Eastman E1D takes provide 49 Pick and 55 Finger rows; the
CC0 Martin HD28 samples provide eleven rows with unknown picking tool.
The embedded bank supplies 54 picked training and fourteen picked development
rows at their declared velocities, plus eight Finger anchors. Those anchors
reuse Eastman material and are not a second independent recording family.
External velocity 91 and string assignment are assumptions.

Six GuitarSet microphone recordings cover six players, comping and solos,
with 315 annotated notes over six twelve-second excerpts. The replay preserves
the same actual string, pitch and onset events, with assumed Finger technique
and velocity 91. It does not validate automatic strum direction. Source
archive integrity checks, licenses, target hashes, controls, score-term counts
and renderer/scorer hashes are retained in `build-real-recordings-oct07`.

These recordings have informed earlier development. Later takes are a
within-master guard, not an untouched independent test set. Martin's attacks
are cropped: the frozen scorer includes their attack term, but it cannot
establish release timing. Spectral windows use each detected onset; an onset
guard prevents the reference's 20 ms pre-roll from rewarding a slower model.

## Matched measurements

Lower descriptor loss means closer features on the same files.

| Recording set | Rows | Original before → after | Bellido before → after |
| --- | ---: | ---: | ---: |
| Eastman Finger | 55 | 6.37473 → 6.04030 (−5.25%) | 6.85825 → 6.50806 (−5.11%) |
| Eastman Pick | 49 | 5.48827 → 5.48827 | 6.07381 → 6.07381 |
| Embedded Finger anchors | 8 | 6.59230 → 6.48188 (−1.67%) | 7.23307 → 6.89525 (−4.67%) |
| Martin, assumed Finger | 11 | 6.26548 → 6.48597 (+3.52%) | 7.43399 → 7.57079 (+1.84%) |

Both picked bank splits retain every score, term count and target hash.
Static Pick and Thumb controls remain byte-identical. The final production
check preserves all 550 original static arrays plus four explicit Pick/Thumb
controls against the scored Finger candidate. Six released performance
arrays intentionally change with the transition repairs and are re-scored
against the same recordings and events; their difference from that candidate
is below −83 dB relative RMS. The 560-case proof is retained as
`final-production-render-parity-v3.json`.

The median paired change in Finger H5–H12 versus H1–H4 balance over 80–250 ms
is −2.28 dB for Original and −2.31 dB for Bellido. Absolute recording error
improves on 45/55 and 43/55 rows, including 10/12 and 8/12 later takes.
Fixed-velocity 64/91/112 probes retain the harmonic excess at all six real
takes of the four audition pitches; no best velocity is selected per row.

The first-15-ms 2–12 kHz share shifts by median −2.22/−1.92 dB across all
Finger rows. The large bass attack discrepancy remains: its median gap is
still about +21 dB. This change improves the release spectrum without
resolving the bass burst. Real repeated takes vary substantially: median
paired changes are 4.11 dB in attack share and 5.27 dB in harmonic balance.
The systematic paired improvement should be read alongside that variation.

GuitarSet mean log-magnitude error changes 14.047535 → 13.894463 dB,
spectral convergence 0.808704 → 0.807009, and chroma distance
0.226028 → 0.224850. Individual clips remain mixed. Its existing 5–10 kHz
energy deficit worsens by 0.41–2.70 dB despite the aggregate improvement.

Martin's regression extends beyond its cropped attacks: body-term errors
increase 2.12/3.71%, and Original's harmonic error increases 11.48%.
Its unknown tool and different instrument/capture limit attribution, but
do not erase that tradeoff. Maximum fitted-partial tuning change is
1.635 cents; attack pitch-trajectory change is at most 0.805 cents, and
detected onset shifts stay below 0.15 ms. Native level falls by less than
0.825 dB in the measured notes.

## Listening and reproduction

`build-real-recordings-oct07/reference-audition/index.html` compares Real,
Before and After. The fixed isolated-note playlist uses the first admitted
Eastman Finger takes at E2/E3/E4/F5; each clip receives one constant RMS
matching gain and an onset-relative crop for audition only. After inherits
Before's gain, so its native level change remains audible. The six performance
comparisons retain the same event files. Benchmark targets and renders are
raw; there is no EQ, denoising or time warping.

Build `AcustraExternalCorpusRenderer`, `AcustraPhysicalFitRenderer` and
`AcustraPerformanceRenderer` in Release. With the prepared corpora, reproduce
the paired external comparison using a fresh output directory:

```sh
python3 Tools/BenchmarkOpenCorpora.py \
  build-real-recordings-oct07/corpora/eastman/rows.json \
  build-real-recordings-oct07/corpora/martin/rows.json \
  --renderer BUILD/AcustraExternalCorpusRenderer \
  --shape dreadnought --body-material spruce --guitar-model original --room 0 \
  --output NEW_OUTPUT --keep --jobs 2 \
  --compare build-real-recordings-oct07/external-original
```

Bellido uses Auditorium/Mahogany, `--guitar-model bellido1978`, and comparison
directory `external-bellido`. `benchmark-summary.json` retains the exact bank
and performance commands, primary signed diagnostics, fixed-force range,
real-take variation, provenance and every cross-set result.

## Runtime and regression verification

The darker Finger floor exposed two pre-existing discrete transition ticks.
Returning a released string to its open state stepped its intrinsic feedback
loss; changing Tuning stepped the spring stiffness behind the saddle. Each
now moves over one current string round trip, with bounded intermediate
values and exact final targets. Hand-loss gains and excitation arriving from
the contact retain their existing behavior. The spring's physical target
still feeds tuning and configuration caches; only its applied stiffness
moves gradually at the junction.

The original 25 dB hand-back bound remains intact: the pre-repair Finger
peaks at 44.1/48/96 kHz fall from 26.52/27.02/26.75 to
20.01/17.36/16.78 dB above their preceding floors. The existing 6 dB
live-tuning bound remains intact: pre-repair 44.1 kHz Finger falls
8.14 → 4.74 dB. All nine tested rate/style combinations
pass; their steady references stay exact. Added tests cover bounded loss,
finite completion, interrupted retunes, reset and attacks during a spring
transition. Independent negative controls fail eight contracts before the
edge-case repairs and pass afterward.

All 62 registered DSP/tool suites and both native PluginProcessor/PluginTempoRelease
suites pass on the final source. Release VST3 and Standalone builds pass,
as does the C++17 warnings-as-errors compatibility build. A strict stereo
byte-parity check found that redundant configuration recalculated an active
ramp's slope; exactly unchanged targets now preserve the existing slope and
deadline. Independent repeated-configuration probes match every intermediate
value in 24 cases. Final source, test and binary hashes are saved in
`final-production-validation-v3.json`.

Finger smoothing adds no sustained filtering stage or allocation. Medium-force
attacks remove an inverse pass, while full-velocity/default-Touch attacks now
perform smoothing and normalization that were previously cancelled. The
transition repairs add inactive counter checks and temporary ramp arithmetic.
Final CPU measurements cover 300 paired cases on an AMD EPYC 9V74, pinned to
CPU 4 with builds, tests and renders idle. Baseline and final source use the
same GCC 14.2, C++20, `-O3 -DNDEBUG -fPIC` toolchain without LTO or fast-math.
Prepared snapshots, setup, checksum checks and reporting stay outside timing;
the measurements describe hot native Performer/DSP callbacks, excluding
JUCE/host and cold-cache overhead.

The main 192 cases use Finger velocity 91, 44.1/48/96 kHz, 64/128-frame blocks,
both guitar models, Room 0/50% and eight attack/held/bend scenarios. Each has
128 alternating pairs after 32 warmups. Smaller controls, maximum-force and
transition grids use 32 pairs after 32 warmups.

| Workload | Cases | Median paired callback change | Median paired time ratio |
| --- | ---: | ---: | ---: |
| Finger v91 initial strum | 24 | −2.58 µs | 0.9871 (−1.29%) |
| Finger v91 held chord | 24 | +0.26 µs | 1.0031 (+0.31%) |
| Exact Finger v127 single low E | 24 | +3.67 µs | 1.0385 (+3.85%) |
| Automatic v127 strum | 24 | −3.26 µs | 0.9802 (−1.98%) |

The columns separately summarize case medians. The exact single-note cases
add 0.27–12.13 µs per case median, with ratio changes +0.04–13.53%. Their
current p95 uses at most 39.78% of the callback deadline, with no observed
misses in either version. Assertions outside timing prove all 24 cases reach
the intended path: baseline slip/reference poles are zero, and the final
absolute slip pole is positive. Maximum MIDI velocity alone is insufficient:
automatic strums vary force and can still run the baseline inverse, so their
faster result is not evidence for the formerly skipped single-pluck path.

The 24 release-hand-off/live-tuning and settled cases have per-scenario median
ratios 0.9737–1.0104; their current p95 uses at most 59.41% of the deadline.
All 36 small Pick/Thumb control callbacks retain identical audio hashes and
near-flat medians. Main Finger p95 stays below 87.32% of the deadline, but
isolated timing outliers occur: both versions have 19 main-grid misses in
24,576 observations, and final live tuning has one isolated miss. Dense Pick
onsets at 96 kHz/64 frames exceed the deadline in both versions; final p95
reaches 151.55%. These observations do not establish universal real-time safety.

`finger-cpu-summary-v3.json` retains all six raw reports and workload details.
`finger-cpu-final-build-v3/manifest.json` records the first five runs' frozen
source/harness, compiler flags and binary/result hashes;
`finger-cpu-single-build-v3/manifest.json` records the separately frozen
single-note extension. Their source hashes match the final tested production
source after timing. No additional DSP changes follow the freeze.
