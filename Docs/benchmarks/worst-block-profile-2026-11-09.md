# Where the worst blocks go, 1.9.37 — 2026-11-09

Source `1e26fc4` (1.9.37-build.93). One development machine: 4-thread Xeon
at 2.1 GHz, gcc 13.3, Linux, Release (`-O3`), 48 kHz in 256-frame blocks.
These are not Reason or DAW CPU figures. No source change is kept: this
records a profile against the portfolio's target of under 10% of the period
on worst-case blocks, and the rewrites tried against it.

## Method

`Acustra.CpuBudget` (three repeats) gives each demo's median real-time
factor and the median of its worst block. Its worst block is one block in
one repeat, so it carries scheduling noise; a per-block harness (the
renderer's block loop, best of three repeats for each block, events listed
per block) separates real cost from noise. Callgrind on the factory
Dreadnought demo, whole and with instrumentation toggled on one block,
attributes the instructions.

## Figures

| Demo | Real-time factor | CpuBudget worst block | Median block | Worst block, best of 3 | Worst block's events |
| --- | ---: | ---: | ---: | ---: | --- |
| 01 Dreadnought | 0.110 | 32% | 11% | 26-30% | 6 note-ons |
| 02 Parlor fingerstyle | 0.089 | 66% | 8% | 28% | 5 note-ons |
| 03 Mahogany thumb | 0.093 | 54% | 8% | 25% | 3 note-ons |
| 04 Jumbo pick | 0.111 | 55% | 11% | 28% | 5 note-ons |
| 05 Palm muted pick | 0.094 | 36% | 8% | 30% | CC2 and 2 note-ons |
| 06 DADGAD mono mic | 0.109 | 27% | 12% | 22% | 1 note-on |
| 07 Open G slide | 0.105 | 46% | 9% | 22% | 7 events, 6 note-ons |
| 08 Piezo stage | 0.108 | 39% | 11% | 26% | 6 note-ons |

Every real worst block is an attack. The 54-66% CpuBudget readings are not
repeatable per block; the best-of-three worst is 22-30%.

## What costs what

A held block (Dreadnought, block 860) is 8.0 M instructions. Axial stretch
quadrature (`evaluateStringAxialStretch`, the cell sum) is 49%, body modes
11%, bridge modes 10%, room 3%. Over the whole demo the shares are nearly the
same (46%, 11%, 10%, 3.6%).

An attack block (block 865, four note-ons) is 15.7 M. The extra 7.6 M is
`noteOn` (5.5 M) plus a costlier axial sum while the new shapes' retarded
reach is short (+0.9 M). Inside `noteOn`: `coupledPolarisationDetune` 2.9 M
(about 0.7 M a note, the damped-pole solve on a cache miss),
`calibrateDispersion` 1.2 M, `captureTail` 1.0 M and the delay-line clears
in `StringLoop::reset` and `captureTail` 0.85 M. One note-on alone adds
about 10% of the period (DADGAD, block 234).

So the target needs both halves: held blocks are already 8-12% of the
period, mostly the axial sum, and each chord attack adds 10-20% more,
mostly the tuning solve and dispersion calibration.

## Tried and not kept: paired loads in the axial sum

Each returning tap reads two adjacent samples. Loading each pair as one
8-byte vector and shuffling replaces eight scalar loads and their inserts
in the SSE cell loop. All eight demos rendered bit-identical sample hashes,
but per-block CPU did not move beyond noise (medians 10.9/11.0%, 7.7/7.6%,
10.9/10.8%; worst blocks within 1-2 points either way), so it was reverted.
The loop is not bound by its loads on this machine.

A second rewrite ran each run of 16 groups in three passes: tap indices and
fractions as vectors, the reads and interpolation lane by lane, then the
slopes from the runs shifted by one, with no per-group shuffles. It was also
bit-identical on all eight demos, and slower (Dreadnought mean 11.4% against
10.2%), so it was reverted too.

## The attack's design work is already cached as far as it can be

Counting every `coupledPolarisationDetune` call over the eight demos, the
solve misses 13-20 times a demo (84 in Open G, whose slides move the stop),
and no miss repeats an earlier key: the eight-entry history already catches
every revisit. The dispersion design (`solvedDispersion`) misses 47-122
times a demo, 37-109 of them on new inputs, because its design frequency
carries each note's fretting detune (`noteDetuneCents`); a longer history
would catch only the 10-30 repeats. The delay copies and clears already
cover only the ring the sample rate uses (`DelayHistory`).

Solving fret tunings ahead was considered and not built. Reason's adapter
prepares the engine with the default controls and receives the song's
values in its first batch, so a solve at prepare would key on the wrong
controls; and the adapter does not run the engine while it sleeps, so there
is no idle time on the audio thread to solve in either.

## Coarser axial quadrature: a sound change, put to the ear

The one lever left is the axial sum's cell count, a numerical convention
(four cells per period of the cut-off). At two cells (the cut-off's Nyquist
rate) the eight demos' mean CPU falls from 9.44% to 7.61% of the period and
their mean worst block from 25.6% to 22.7%; at three cells, to 8.49% and
24.5% (best of three runs per block, best of two rounds). Neither is a
null: against the shipping engine the demos' residual is 28-65 dB below the
signal at two cells, and on full-velocity pick strokes 24.8 dB below at two
cells and 37.3 dB at three. A level-matched A/B/C set on those strokes went
to the owner on 2026-11-09, who kept A, the shipping four cells
([decisions.md](../decisions.md)).

## What the target would take

Held blocks already take 8-12% of the period, and each chord attack adds
10-20% more for its per-note design work. Under 10% on worst-case blocks
needs both halves reduced well beyond what the exact rewrites above found,
so it is not reachable without changing the sound.
