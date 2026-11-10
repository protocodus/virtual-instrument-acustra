# CPU after the fix-only pass, 1.9.41 — 2026-11-10

Sources 1.9.40 (`ed26dd8`) against 1.9.41, at 48 kHz in 256-frame blocks
(5.33 ms) through `acustra::Performer`, Dreadnought Steel. One development
machine (4-thread Xeon at 2.1 GHz, gcc 13.3, clang 18.1, valgrind 3.22) that
five other builds shared, at load averages of 3 to 25 on its four cores. No
wall-clock figure was taken as evidence at that load: every figure below is
instructions counted by callgrind, and every change was kept or dropped on
output hashes. They are not Reason or DAW CPU figures. It continues
[cpu-consolidation-2026-11-09.md](cpu-consolidation-2026-11-09.md), and only
a change whose output is bit for bit the old one was kept.

## Method

- **Counts.** A probe plays a phrase through the Performer in the
  renderer's block loop and turns callgrind's collection on for one block
  (`valgrind --tool=callgrind --instr-atstart=no --collect-atstart=no`, the
  `totals:` line). For a whole demo, collection is on inside
  `Performer::endBlock` (`--toggle-collect`). Load does not move either.
- **Two builds.** gcc `-O3`, the repository's Release build, and clang
  `-std=c++17 -O2 -fno-builtin -DJUKEBOX_SDK -msse3`, the closest stand-in
  here for the Rack device's build (the Jukebox SDK compiles with clang at
  `-O2` with no builtins, to SSE3 or NEON); its lanes are the engine's
  `JUKEBOX_SDK` path. Called "Rack flags" below.
- **Blocks.** Of the stress phrase: block 375, its worst, in which a
  six-note chord is struck over a ringing chord under the pedal; block 656,
  a chord struck again on the frets it was just played on, so its tunings
  come from the cache; block 700, a held chord. Of the first-time chords
  phrase: block 18, the first chord from rest, and blocks 93 and 168, new
  chords over ringing ones.
- **Hashes.** FNV-1a over the float output of the eight demos, the stress
  phrase, the first-time chords and the preset walk, in both builds.

## Shipped: the tuning solve stops repeating itself (exact)

A fret played for the first time solves its coupled tuning in about 140
evaluations of the bridge's modal sum and the five resting strings' loops
(`coupledPolarisationDetune`). Each evaluation converted every mode's
damping, frequency and three port weights to double again, and raised z to
each resting string's whole delay by its own chain of squarings. The mode
terms are now made once a solve, and one evaluation makes the squares z,
z², z⁴, … once for the longest resting string; each string multiplies the
ones its delay needs, in the order it always did. Same values, same
operations in the same order, so the same doubles.

| Block, instructions | 1.9.40 | 1.9.41 | Change |
| --- | ---: | ---: | ---: |
| Stress 375 (worst), gcc | 23,784,801 | 23,239,627 | −2.3% |
| Stress 375 (worst), Rack flags | 23,902,555 | 23,264,128 | −2.7% |
| Chords 168, gcc | 22,388,477 | 21,875,602 | −2.3% |
| Chords 168, Rack flags | 22,802,707 | 22,202,679 | −2.6% |
| Chords 93, gcc | 21,367,609 | 20,881,726 | −2.3% |
| Chords 93, Rack flags | 21,683,877 | 21,116,225 | −2.6% |
| Chords 18 (from rest), gcc | 10,954,049 | 10,729,716 | −2.0% |
| Chords 18 (from rest), Rack flags | 10,486,310 | 10,224,134 | −2.5% |
| Stress 656 (same frets again), gcc | 12,641,997 | 12,641,997 | 0 |
| Stress 656 (same frets again), Rack flags | 12,746,132 | 12,746,132 | 0 |
| Stress 700 (held), gcc | 8,649,376 | 8,649,376 | 0 |
| Stress 700 (held), Rack flags | 9,330,735 | 9,330,735 | 0 |

It lowers the peaks, not the mean: a demo is mostly held blocks, and a fret
played again finds its tuning in the cache.

**Null test.** All eleven hashes are 1.9.40's, in both builds:

| Render | gcc | Rack flags |
| --- | --- | --- |
| Demo 1 | `237634162b0c54f6` | `0cb53111fcc92818` |
| Demo 2 | `00eae61ce2bdaf61` | `5e6d303cc0f3b326` |
| Demo 3 | `06960e56362fc8bb` | `9e93829ad0682ce8` |
| Demo 4 | `a314fc3aaac4ab91` | `89cd6f8735fba9e1` |
| Demo 5 | `622f4d0ddff0cf9b` | `fa8e76874b2ee3ad` |
| Demo 6 | `dcefa47b353fb637` | `e4a0a13d55cdbd2b` |
| Demo 7 | `66e6a28103564fde` | `a2bdcb2eece0b477` |
| Demo 8 | `92fdef005b0b08df` | `d15bfff2dc94a643` |
| Stress phrase | `a2ce53d367ec24b9` | `211d321f5a40ba25` |
| First-time chords | `55a3a685898ba91c` | `d0435ee5bc5792df` |
| Preset walk | `e8df7a9981226198` | `13b8fbc5f6b429e4` |

The gcc column is the 2026-11-09 document's. The solve has no new test: its
results are the old ones to the bit, which these hashes and the suites that
pin tunings (`Acustra.ModalTuning`, `Acustra.PitchGeometryCache`,
`Acustra.Engine`) hold.

## Built and measured, not shipped: the retained wave's bounded copy

A note on a string that is still sounding moves the string's two loops into
the retained branch (`captureTail`), each with its whole 32 KB delay line. A
retained loop's delay is never set again and no hand lands on it again, so it
reads no further back than its delay and the damping hand's two frozen ages,
plus two samples (a 110 Hz string: about 440 of 8192). Aculele 0.4.63 copies
only that much; the same was built here.

- **Output: exact.** With it and the solve change, all eleven hashes were
  1.9.40's in both builds. A poison test (each preset's demo, the stress
  phrase, a body change, bends with the bridge hand, a hand landed on a
  string bent far down that is then thrown up, a String Age sweep; `3.0e6`
  written over every line sample the copy left out) rendered equal to the
  bit, failed on all 48 phrase and preset pairs with the bound two samples
  short, and failed on the hand-then-bend phrase with the hand's ages left
  out.
- **Cost.** 0.40 M instructions off each first-time chord block in either
  build (stress 375 with both changes: 22,841,195 with gcc, 22,865,900 with
  the Rack flags, 4.0% and 4.3% under 1.9.40), and 2.1% off a chord struck
  again on the same frets (12,376,373 and 12,480,644). Demo 1 whole, inside
  `Performer::endBlock`: 17,302,541,095 to 17,301,167,711 with gcc and
  18,462,573,024 to 18,461,091,720 with the Rack flags, 0.008% fewer.
- **Why it is not in 1.9.41.** `Acustra.ChordJoinPlan`,
  `Acustra.StrumDirection` and `Acustra.RepeatedPluckRealism` compare a
  retained loop's whole line with the string's line it was taken from, and
  fail on the samples nothing reads. `Acustra.Engine`,
  `Acustra.ConstructionHistory` and `Acustra.BodyShape` timed out on the
  loaded machine in that run, so whether they hold the same comparison is
  not known. Those fixtures would have to compare the reach, each with its
  reason beside it; that is a pass of its own, not a late edit.
- **One thing to keep from it.** Aculele assigns the state by making it a
  base of the loop. Built that way here, the held block with the Rack flags
  was 9,348,582, 0.19% more, all of it inside `AcustraEngine::process`,
  which runs no new code on a held chord: the layout was the same to the
  byte (33,520 bytes, the line at 748), and clang 18 compiled the function
  differently for the changed type (30,336 bytes of code before, 29,436
  after), 0.23% more over the whole of demo 1. Copying every byte of the
  loop but its line, with the type left as it was, gave the held block its
  old count to the instruction.

## Where the worst block goes

Stress block 375 in 1.9.40 with the Rack flags, 23.9 M instructions:

| Part | Instructions | Share |
| --- | ---: | ---: |
| Stretch sum (`evaluateStringAxialStretch`), six strings and their retained waves | 6,892,653 | 28.8% |
| Tuning solve: the resting strings' loop matrix | 6,344,166 | 26.5% |
| Tuning solve: complex division and the centre terms | 861,508 | 3.6% |
| `process` (strings, bridge junction, capture) | 2,897,769 | 12.1% |
| `atan2` in the dispersion designs | about 1,120,000 | 4.7% |
| Body bank | 735,232 | 3.1% |
| Bridge modes | 658,176 | 2.8% |
| `memcpy` | 403,865 | 1.7% |

A held block (9.33 M) is 56.3% the stretch sum and 19.7% `process`.

## Considered and not built

- **The solve's modal sum in two lanes.** About 4% of the worst block. It
  would be a second code path beside the scalar one, and it stays exact only
  if both sum in the same association with no fused multiply-add; that has
  to be shown per compiler and per target (arm64 contracts by default).
  Not built in a pass that ships exact changes only.
- **Putting held strings that have come to rest to sleep.** A held chord at
  rest still costs 7.7% of a block. Suspension is exact for released strings
  because their state at rest is known to be zero; a played string keeps
  transitions, contact and hand state that would have to be proven
  untouched. The case is a key held longer than its string rings.
- **Solving a fret's tuning before it is played.** Considered and not built
  on 2026-11-09 (`cpu-consolidation-2026-11-09.md`); nothing new.
- **The stretch sum.** Half of every held block. The 2026-11-09 pass's two
  exact rewrites were bit-identical and no faster, and skipping quiet
  strings was rejected by ear. Not repeated.
- **The fresh loops' clear.** A note on a sounding string still clears its
  two new lines whole (64 KB) and the contact transport's history (32 KB).
  A fresh loop's delay can be set anywhere afterwards, so it has no bound
  like the retained one's.

The 10% target on a chord's first block is not reached and no exact change
found here reaches it: the README's Known gap stands.
