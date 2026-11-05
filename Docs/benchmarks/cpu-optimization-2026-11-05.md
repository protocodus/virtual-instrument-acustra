# Acustra CPU optimization, 2026-11-05

From `10bd1da` (1.9.21) to `b329e7c` (1.9.22): five engine commits, each one
mechanism, each exact. Every commit's float output and public state match the
previous engine bit for bit, and the eight demos' bytes are unchanged.

## Workload and build

The eight preset demos (`Docs/audio/midi`) through the Performer, as the audio
tier plays them, at 48 kHz in 256-frame blocks. GCC 13.3, Release (`-O3`),
x86-64 (SSE2), a shared 4-thread Xeon VM at 2.1 GHz: development-machine
figures, not Reason or DAW CPU. The Rack's 96 kHz (2x quality) rendering was
not timed this round.

Instruction counts are callgrind totals for one render of all eight demos
(`RenderAcustraPresetDemos --jobs 1`, setup and WAV writing included, about 1%
of the total). Wall time is `RenderAcustraPresetDemos --cpu --repeats 1`, the
1.9.21 and the 1.9.22 binaries run alternately for six rounds; a round's
figure is the sum over the eight demos of the CPU time as a fraction of real
time.

## Where the time went

At 1.9.21 the string's own stretch (`renderStringAxialForce`) was 22.8% of the
instructions, `process` itself 18.4%, the body's radiation bank 15.2%, the
bridge's modes 15.2%, `finishVoice` 6.0% and the room's early arrivals 6.0%.

## Per commit

| Commit | Mechanism | Instructions | Change |
| --- | --- | ---: | ---: |
| `10bd1da` | baseline (1.9.21) | 88.648 G | |
| `7a96aab` | the stretch's cell sums read the line unmasked when its window does not wrap | 85.019 G | −4.1% |
| `bd8f122` | the body bank's full four-mode groups skip the partial-group test | 84.366 G | −0.8% |
| `2d9fd17` | `finishVoice` inlined into the sample loop | 83.834 G | −0.6% |
| `f0c0822` | the per-sample stretch and delay read inlined; their redesigns out of line | 83.177 G | −0.8% |
| `b329e7c` | the room's early-arrival taps transposed in eight shuffles | 82.857 G | −0.4% |

In all, −6.5% instructions. The functions the commits touch:

| Function | 1.9.21 | 1.9.22 | Change |
| --- | ---: | ---: | ---: |
| `renderStringAxialForce` line sum (`sumAxialCells`, now in `evaluateStringAxialStretch`) | 20.23 G | 16.60 G before inlining | −18% |
| `BodyBank::render` | 13.51 G | 12.85 G | −4.8% |
| `RoomAmbience::step` | 5.33 G | 5.01 G | −6.0% |

Commits 3 and 4 move work between functions (inlining), so they are measured
on the whole render and on wall time: against their own parents, commit 3 was
faster in four of four interleaved rounds (mean −2.1%) and commit 4 in four of
five (mean −1.1%).

Wall time, sum over the eight demos of the realtime factor:

| Round | 1.9.21 | 1.9.22 | Change |
| --- | ---: | ---: | ---: |
| 1 | 0.6366 | 0.5979 | −6.1% |
| 2 | 0.6314 | 0.6187 | −2.0% |
| 3 | 0.6271 | 0.6365 | +1.5% |
| 4 | 0.6374 | 0.6170 | −3.2% |
| 5 | 0.6324 | 0.6075 | −3.9% |
| 6 | 0.6453 | 0.6116 | −5.2% |

The rounds scatter by the VM's neighbours; the instruction count is the
figure to trust, and the wall time agrees in sign and rough size (mean −3.2%).

## Output

Exact. After every commit `Tools/BuildCpuOptimizationComparison.sh` built the
1.9.21 and the candidate source side by side, and its full battery (123
configurations: 48 to 192 kHz, block partitions, techniques, captures,
connected articulations, tails, lifecycle) compared every Main and Piezo
sample with `memcmp` and every serialized public state word: all match. The
final source passes the same battery built with Clang 18 as well. The eight
demos render byte-identical WAVs. The unit, audio and perf tiers pass (89
tests), and the engine builds under Clang 18 with the Rack's
`-fno-builtin -ffp-contract=off -fno-exceptions` and `-Wall -Wextra -Wshadow
-Werror`.

## Tried and dropped

- The bridge modes' heave residue sum split into two running sums (the cross
  terms apart): no faster over four interleaved rounds, as on 2026-10-30.
- The bridge modes in a heave/rock pair layout: not built. The layout is read
  by six test suites and a tool, and the 2026-10-30 trial found no fewer
  instructions.

## Not done

- The stretch's four-cell loop is now bound by its gathers: twelve scalar
  reads and their lane moves per group, about 70 instructions for four cells.
  Without wider gathers than SSE2 the remaining saving is small.
- The body bank's denormal flush is about a fifth of its instructions; it is
  part of the engine's exact zero-tail contract, so thinning it would change
  bits (though not anything audible) and was left for a separate decision.
