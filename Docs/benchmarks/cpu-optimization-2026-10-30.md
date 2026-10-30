# Acustra CPU optimization, 2026-10-30

From `bcf5c5d` (1.9.5) to `372279d` (1.9.6): six engine commits, each one
mechanism, plus one Rack-side commit (the output decimator, in the Rack
repository). Five are exact or last-bit (the float output of every demo is
byte-identical); one, the body's radiation sum, is measured inaudible.

## Workload and build

The eight preset demos (`Docs/audio/midi`) through the Performer, as the audio
tier plays them, at the Rack's shipped 2x quality (96 kHz engine, 128-frame
blocks) and at 48 kHz (256 frames). Parity was also checked at 88.2 and
192 kHz. The engine and Performer were built as near the Rack's build as the
host allows: clang 18, `-O3 -std=c++17 -fno-builtin -ffp-contract=off`.
GCC 13 (`-O3 -std=c++20`) renders the same bytes as clang at every commit.

Instruction counts are callgrind totals for one render of
`01-dreadnought-steel` (setup included, about 4% of it). Wall time is the
best of eight interleaved runs over all eight demos on a shared 4-thread
Xeon VM: development-machine figures, not Reason or DAW CPU.

## Per commit

| Commit | Mechanism | Grade | 96 kHz instructions | 48 kHz instructions |
| --- | --- | --- | ---: | ---: |
| `bcf5c5d` | baseline (1.9.5) | | 18.880 G | 9.131 G |
| `af0b471` | room early arrivals read four at a time (vector lanes, mirrored ring) | exact | 18.622 G | 8.873 G |
| `1c97bda` | room converter's output filter: left and right in one vector | exact | 18.284 G | 8.871 G |
| `fbe1ccf` | string delay follower skipped at its target | exact | 18.188 G | 8.819 G |
| `2227a02` | room converter's rings written twice, read as one run | exact | 17.909 G | 8.819 G |
| `a8e28af` | room converter's long sums split (four and two running sums) | last bit | 17.845 G | 8.819 G |
| `372279d` | body radiation summed in four lanes | measured inaudible | 17.355 G | 8.574 G |

In all, -8.1% instructions at 96 kHz and -6.1% at 48 kHz. Wall time over the
eight demos: 0.185x -> 0.164x realtime at 96 kHz (-11.5%) and 0.089x ->
0.079x at 48 kHz (-10.9%). The sum splits save more time than instructions:
they shorten chains of additions that each waited on the one before.

## Output

- Exact commits: the same float bits by construction (same products and
  additions in the same order); the demos' FNV hashes are unchanged.
- `a8e28af`: only the order of double additions of exact float products
  changes, about 1e-16 of the sum. Every demo renders identical bytes at
  88.2, 96 and 192 kHz (48 kHz has no conversion).
- `372279d`: each mode's states and contributions are unchanged; the float
  sum is regrouped. Against `a8e28af`, at 96 kHz, the residual peaks at
  -119.4 dBFS (Dreadnought Steel) and lies 124-129 dB under each demo's RMS;
  Piezo Stage Steel renders the same bytes. Unit and audio tiers pass
  (86/86).

## Tried and dropped

- Bridge modes in a pair-of-lanes state layout, and branch-free: no fewer
  instructions, no faster. Its residue sum split in two: identical bytes but
  no measurable gain.
- The capture filter's two sides in one vector over the existing state
  layout: -0.3% instructions, not worth the code.
- Four running sums over the room converter's input filter with a run-time
  lane index: slower; the explicit unrolled form landed in `a8e28af`.

## Not done: a listening decision

A retained tail (the old wave after a re-pluck) stays on the bridge until it
falls under 2e-7 for 80 ms, and the bridge keeps driving it, so tails run on
21-49% of string-samples in the demos, mostly 40-50 dB under their own
string's new note. Retiring them earlier would save a large share of the
string work but changes the sound; it needs an A-Z listening test, not a
null test.

## Idle

Both front ends already suspend the engine after inaudible silence, and the
engine flushes its tails to exact zero, so a silent instance costs nothing
and no denormal tail lingers (checked with and without flush-to-zero).
