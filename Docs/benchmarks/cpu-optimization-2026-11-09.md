# Exact axial-design guard — 2026-11-09

Final DSP source is `5553984`, acoustic source `0b835e8`, and the shipping
comparison is `8e195a9` (1.9.34). The only CPU transform separates the existing
axial-force design cache check from its large redesign body. The four float
inputs are evaluated and rounded in the same order; the exact-bit key,
miss-side state ordering, coefficient arithmetic and invalidation lifecycle
are unchanged. No mode count, sample rate, numerical precision, physical
model, RNG draw or output path is reduced.

The small guard is forced inline using the project's existing attribute;
there is no C++ `inline` linkage change. The cold redesign receives the
already-rounded float inputs. In particular, its old length/scale keys are
compared before overwrite so that stretch-reference changes remain intact.

## Evidence and method

Measurements ran serially in the team-coordinated quiet window, on one pinned
AMD EPYC 9V74 x86-64 logical CPU, GCC 14.2.0, Linux, with FTZ/DAZ enabled.
The four-core container quota does not describe a four-core audio engine:
all measured DSP callbacks use the calling thread. These are native tool
measurements, not Reason, plug-in host or ARM execution claims.

Eight complete shipping Performer factory performances were measured at
48/96 kHz and 64/256 frames. Three rounds alternated build order; every
callback retains steady wall and calling-thread CPU readings. MIDI dispatch
is inside both clocks; prepare, file I/O, hash computation and export are
outside. These are fresh prepared performances, including initial attacks,
not restored steady-state snapshots. The separate warm run warms the machine,
not each later process's engine state.

| Factory comparison | Aggregate thread CPU ratio | Three round ratios |
| --- | ---: | --- |
| Acoustic / shipping | 1.00704 | 0.99651, 1.01942, 1.00523 |
| Exact guard / acoustic | 0.99269 | 1.00323, 0.98663, 0.98845 |
| Final / shipping | 0.99968 | 0.99972, 1.00580, 0.99362 |

Wall ratios are nearly the same. The guard improves 21 of 32 case medians;
its median case ratio is 0.98827. Individual full-phrase measurements vary
more than the aggregate gain, so this supports a modest saving rather than
a universal per-case speedup or a precise population estimate.

A separate hot callback benchmark restores identical prepared state outside
both clocks and alternates baseline/current order on each pair. Three rounds
cover Finger, Pick and Thumb natural gestures plus control transitions:
348 case-runs, 128 measured pairs and 16 warm-up pairs per case. Natural
callback pair-median CPU ratios have median 0.99164, with 266/324 case-runs
faster; aggregate measured CPU ratio is 0.99505. Transition ratios have median
0.99408 and aggregate 0.99735. Thread CPU includes the nested steady-clock
reads; their overhead is not subtracted.

All 96 acoustic/guard full-phrase pairs have identical sample hashes and
peaks. Every paired callback hash is also identical. The independent exact
comparison checks Main, Piezo and public state, rather than hashes alone:
123 configurations plus 67 alternate partitions match 165,634,428 sample
values and 8,728,538 state words exactly. The final-source audition render
also matches the acoustic file byte for byte. Full unit/audio and focused
sanitizer results are in the [validation audit](../validation/realism-audit-2026-11-09.md).

Callgrind's first two seconds of Finger/Pick rendering identify axial stretch
quadrature as 51.3%/42.2% of instrumented instructions. The final guard reduces
whole-loop counts from 3,427,780,108 to 3,414,488,385 and from 2,798,496,980 to
2,788,876,577, respectively (0.388%/0.344%). These totals include render-loop
hash and observation bookkeeping, unlike the native callback clocks.
Two adjacent-load prototypes were rejected after they increased instruction
counts despite exact output. The retained change has measured support from
both instruction counts and the independent native protocols.

## Limits that averages do not hide

Both existing performance gates pass unchanged. The eight factory median
real-time factors are 0.09265–0.18661 at 48 kHz/256 frames, under the 0.28
budget. This average gate is not a guarantee that every callback meets its
period.

The Palm Muted factory phrase has a real inherited deadline limitation.
At 48 kHz/256, 113 callback positions exceed 5.333 ms of calling-thread CPU
in at least two of three rounds in **all three builds**. They cluster after
four CC2 changes, including roughly the first 0.18 seconds. Final median CPU
within those affected ranges is 5.35–8.38 ms. Most affected callbacks contain
no new incoming MIDI, and CPU approximately equals wall time; descheduling
is not an adequate explanation. FTZ/DAZ is already enabled. A first-second Palm4 Callgrind capture attributes 70.42% of instructions to
`coupledPolarisationDetune` (71.31% to `updateControlState`). Smoothed hand loss
changes the damped-pole calibration inputs at control updates. Six diagnostic
FTZ on/off renders are sample-hash identical and show no consistent denormal
penalty. This patch does not solve that inherited limit.

Removing loss from that calibration key would not be an exact optimization:
the solver finds damped complex poles through frequency-dependent bridge and
resting-string impedances. A shared loss factor can therefore move the
frequency of the pole, not merely its decay. An intrinsic/contact-only tuning
reference or an approximate real-time solver needs separate model, tuning,
trajectory and listening qualification; it is not introduced late in this audit.

The deliberate 24-event Gather burst also exceeds the 96 kHz/64-frame period:
worst measured p95 calling-thread CPU is 2.963 ms against 0.667 ms (4.445 times
the period), versus 3.145 ms in the acoustic build. Live control transitions
can exceed one period too. No universal real-time safety claim follows from
the small average CPU improvement.

Raw timings, immutable binaries, manifests, exact-comparison reports and
profile counts are retained under `/workspace/scratch/acustra-20261109/`:
`factory-timings`, `callback-timings`, `profiling`, `parity-axial-guard`, and
`timing-environment.json`. Build helpers remain in `Tools/`; the standalone
factory harness and run/analysis scripts accompany those local receipts.

The independent repeated-callback receipt, including hashes of all nine
Palm/48 kHz/256 timing CSVs, is
`/workspace/scratch/instrument-realism-20261109/acustra_palm_repeated_callback_audit.json`.
