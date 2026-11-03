# Acustra CPU optimization, 2026-11-03

From `9067514` (1.9.15) to `6a2bda6` (1.9.16): one engine commit, one
mechanism, last-bit exact. The later 1.9.17 and 1.9.18 commits change the
Performer's event order and a bounds guard; neither changes a demo's bytes
against 1.9.16.

## Workload and build

The eight preset demos (`Docs/audio/midi`) through the Performer, as the audio
tier plays them, at 48 kHz in 256-frame blocks. GCC 13.3, Release (`-O3`),
x86-64 (SSE2), a shared 4-thread Xeon VM at 2.1 GHz: development-machine
figures, not Reason or DAW CPU.

Instruction counts are callgrind totals for one render of
`04-jumbo-steel-pick` (setup included). Wall time is
`RenderAcustraPresetDemos --cpu --repeats 3`, the 1.9.15 and the 1.9.18 binary
run alternately for four rounds; a round's figure is the sum over the eight
demos of the CPU time as a fraction of real time.

## Where the time went

After 1.9.14, `renderStringAxialForce` (the string's own stretch at the
saddle) was 4.91 G of 27.99 G instructions, 17.5% of a Jumbo Steel Pick
render, the largest function after the engine's `process`. Its cell loop
evaluates a retarded slope at each cell of the string: two line reads (one
interpolated), a difference and a square.

## The change

`sumAxialCells` reads and squares four cells per step in 4-wide vectors where
the compiler has them (x86-64 GCC and Clang); the taps stay scalar gathers
and the rest of each cell's arithmetic is the vector form of the scalar code.
The leftover cells and every other target run the one-at-a-time loop. Only the
order the squares are added in changes: four partial sums joined last.

| | 1.9.15 | 1.9.16 | Change |
| --- | ---: | ---: | ---: |
| Render instructions (callgrind) | 27.986 G | 26.736 G | −4.5% |
| `renderStringAxialForce` | 4.909 G | 3.659 G | −25% |

Wall time, sum over the eight demos of the realtime factor:

| Round | 1.9.15 | 1.9.18 | Change |
| --- | ---: | ---: | ---: |
| 1 | 0.6875 | 0.6335 | −7.9% |
| 2 | 0.6787 | 0.6357 | −6.3% |
| 3 | 0.7004 | 0.6781 | −3.2% |
| 4 | 0.7513 | 0.6894 | −8.2% |

The rounds scatter by the VM's neighbours; the instruction count is the
figure to trust, and the wall time agrees in sign and rough size (mean
−6.4%).

## Output

Last-bit, the same grade as the room converter's long sums in 1.9.6. Against
1.9.15, at 48 kHz, every demo differs by at most 8.3e-7 (24-bit quantisation
noise), 115.7 to 121.1 dB under the demo's RMS. `Acustra.StringAxialForce`
passes. The 1.9.15 and 1.9.18 demos are in the review folder as A and B.

## Not done

- Copying the ring's recent samples into a linear scratch so the taps need no
  masking: estimated at about −3% more from the instruction profile, not
  built this round.
