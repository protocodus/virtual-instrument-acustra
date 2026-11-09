# CPU consolidation, 1.9.38 to 1.9.40 — 2026-11-09

Sources 1.9.37 (`5ce09eb`) to 1.9.40. Measurements are at 48 kHz in
256-frame blocks (5.33 ms) through `acustra::Performer` on one development
machine (gcc 13.3, 4-thread Xeon at 2.1 GHz) that four other builds shared,
so wall-clock figures carry several percent of noise and their worst blocks
more; decisions rest on instruction counts and on output hashes. They are not
Reason or DAW CPU figures.

## A held chord ran into subnormal numbers

**Scenario.** Dreadnought Steel, an open E chord struck once, the pedal
pressed, the keys released, and nothing more for 150 s. The same happens with
the keys held or the Sustain switch on: the six strings stay played.

**Before (1.9.37).** With the processor's flush-to-zero and
denormals-are-zero modes set, as `juce::ScopedNoDenormals` sets them in the
plug-in, a block cost 11% of its period throughout. Without them, as in the
Rack adapter and the tools unless the host has set them (not verified for
Reason), the mean block cost per second of the hold was:

| Time held | 0-30 s | 40 s | 50 s | 60-80 s | 90 s on |
| --- | --- | --- | --- | --- | --- |
| 1.9.37, no flush-to-zero | 12% | 77% | 66% | 36-62% | 100-170% |
| 1.9.38, no flush-to-zero | 11% | 11% | 11% | 9-10% | 9-11% |

From 90 s on a block took longer than it lasts, and stayed there until a
string was struck again.

**Where.** The denormal-operand exception was unmasked (MXCSR bit 8) and a
SIGFPE handler counted the faulting instruction addresses:

- 38-55 s: `sumAxialCells` and the last cell in `evaluateStringAxialStretch`.
  The lines hold a quasi-DC value near 1e-13 whose sample-to-sample
  differences are near 1e-20, and the stretch sum squares them.
- 60-80 s: the longitudinal ring's states (`renderStringAxialForce`), which
  a played string keeps running.
- 90 s on: `StringLoop::readDelay`. Once a line holds only zeros (its write
  floor is 1e-30) the Thiran read's double recursive states decay to
  4.9e-324 and stay there under round-to-nearest, on every loop, for good.

The output at that time: under 1e-11 of full scale from 26 s, 1e-16 at 38 s,
exact zero from 85 s.

**Fix.** The idle flush (`processIdleFlush`) already clears the whole
instrument once everything before the output gain is under 1e-11 for 80 ms,
but only with no string played. It now also runs with strings held by a key,
the pedal or the Sustain switch (a string let go still waits for its
hand-back, as before): those strings' lines, loop filter memories and level
observers are set to exact zero where they are (`StringLoop::settle`), and
the strings stay held, fretted and owned. Once they have been let go and
handed back, the instrument is flushed again as one with nothing held, so a
suspension still keeps exactly what an awake engine holds.

Three local floors were tried first (Aculele 0.4.57's 1e-30 floor on the
ring states, and candidates for the sum and the loops). The ring floor alone
cured only 60-80 s. One flush at the existing floor covers all three sites and
the level observers, and costs nothing while anything sounds.

**After (1.9.38).** The flush lands 21.47 s into the hold. The same trace
over the 150 s finds 330 subnormal operations in all (the first samples of
the contact's travelling wave and of the stretch sum at the strike) against
one or more in every block from 38 s on before.

**Null test.** The eight factory demos hash identically before and after
(FNV-1a over the float samples of both channels):

| Demo | Hash, 1.9.37 and 1.9.38 |
| --- | --- |
| 01 Dreadnought Steel | `237634162b0c54f6` |
| 02 Parlor Steel Fingerstyle | `00eae61ce2bdaf61` |
| 03 Mahogany Steel Thumb | `06960e56362fc8bb` |
| 04 Jumbo Steel Pick | `a314fc3aaac4ab91` |
| 05 Palm Muted Steel Pick | `622f4d0ddff0cf9b` |
| 06 DADGAD Mono Mic | `dcefa47b353fb637` |
| 07 Open G Slide-ready | `66e6a28103564fde` |
| 08 Piezo Stage Steel | `92fdef005b0b08df` |

On the held chord, with flush-to-zero set for both, the two outputs are
identical up to the flush; after it 1.9.37 still holds its residue and
1.9.38 is exact zero, a largest difference of 3.2e-10, 188 dB under the
chord's peak.

**Test.** `testHeldChordComesToRest` (`Acustra.Engine`): a chord held by one
key and the pedal reaches the flush with six strings still played, every line
and loop filter memory exactly zero; a second of it rendered with
flush-to-zero and denormals-are-zero off raises neither the
denormal-operand nor the underflow flag; a string struck again sounds; let go,
the strings are handed back and the instrument flushed as one. On 1.9.37 it
fails at the first step: the held chord never comes to rest.

## At rest the level observers stayed in subnormal numbers (1.9.39)

**Scenarios.** Three more states a session sits in, each traced the same way
with the underflow exception unmasked as well (Dreadnought Steel, 48 kHz, no
flush-to-zero): one string of a held chord let go, a chord let go and left to
die away, and a held chord with another guitar selected under it.

**Found.** After the flush `voice.level` (`finishVoice`) went on decaying
over the cleared strings, was subnormal 0.5 s later and stayed at 1.4e-45:
one subnormal multiply and subtract a string every sample. A suspended
instrument's observers were already set to zero (`suspend`), and the Rack
adapter sleeps a quiet instrument, but not one with a key or the pedal down,
and the tools never do.

| Mean block cost at rest, no flush-to-zero | 1.9.38 | 1.9.39 | With flush-to-zero |
| --- | --- | --- | --- |
| A chord let go, engine kept awake | 4.9-5.0% | 3.7-3.9% | 3.3-3.7% |
| Five strings held, one let go | 9.0-9.6% | 7.9-8.6% | 7.7-7.8% |

**Fix.** The flush sets every string's level observer to zero, held or not.
Zero changes nothing the observer decides, which is why a suspension could
already do it.

**Null test.** The eight demos, the stress phrase, twelve first-time chords
and a walk through the presets hash as in 1.9.38; so does a chord let go,
left 13 s to come to rest, and two more chords struck after it, one let go
20 ms after its strike (`e9c9f2a7eb388a94` with and without flush-to-zero,
1.9.38 and 1.9.39).

**Test.** `testQuietStatesStayOutOfSubnormals` (`Acustra.Engine`) renders the
three scenarios with flush-to-zero and denormals-are-zero off and requires
the denormal-operand and underflow flags clear from one second after the last
key or preset event, and for two seconds from the moment of rest. On 1.9.38
all three fail at rest.

**Left as they are.** Two kinds of subnormal operation remain, neither a
state and neither with a measurable cost here (the 43 blocks after a
six-string release cost 9-11% with flush-to-zero on and off):

- An attack and a key-up start from and die through arbitrarily small
  numbers: the release noise's envelopes for 0.2 s after a key-up
  (`renderReleaseNoise`), and the last samples of the contact transport's
  fractional delays (`ContactTravel`), about 0.12 s of samples a six-string
  release in all.
- In a held chord's last 2.4 s above the idle floor the quietest string's
  stretch sum meets differences that cancel to one bit near 1e-20, whose
  squares are subnormal: 26 groups of four samples over that time.

## A note on a sounding string moved 256 KB it did not need to (1.9.40)

**Baseline (1.9.38, Dreadnought Steel, best of three runs a block).**

| Phrase | Mean | Median | 99th percentile | Worst block |
| --- | --- | --- | --- | --- |
| Stress: a full-velocity chord under the pedal, re-struck note by note, then eight more chords on top | 10.0% | 9.9% | 14.6% | 34.6% (six events), others 18-25% |
| Twelve first-time six-note chords, 0.4 s apart | 10.0% | 11.4% | 20-25% | 35-40% |
| A ringing chord while each preset is selected in turn | 11.3% | 11.1% | 15-17% | 27-37% (the preset blocks) |

**Profile.** Callgrind on one block of the first-time chords (six note-ons,
every tuning and dispersion design a miss): 24.36 M instructions against a
held block's 8.0 M. `noteOn` is 12.7 M: the tuning solve
(`coupledPolarisationDetune`) 7.8 M, `captureTail` 2.07 M, the dispersion
design 1.74 M, `StringLoop::reset` 0.53 M, `firePluck` 0.53 M. libc's
`memset` and `memcpy` were 1.34 M and 1.19 M bytes: 224 KB cleared and 198 KB
copied a note.

**Where the bytes went.** A note on a string that still sounds moves the old
wave into its retained branch (`captureTail`): two loops' rings (64 KB) and
`RepluckArrivals`, the queue of the old contact waves' remaining arrivals,
two planes of 16384 floats. The queue was copied whole (128 KB) and then
cleared whole (128 KB), and cleared whole again when the retained wave
retired. It holds nothing outside its pending span: `add()` writes only
inside it and `process()` zeroes each slot it reads. Nearly always the span
is empty.

**Change.** `RepluckArrivals::clear()` zeroes the pending span only, and
`moveTo()` moves it: both queues end as the copy and the clear left them,
every slot (`Acustra.ContactTravel` and the fret-motion checks compare the
whole planes).

| First-time chord block | 1.9.38 | 1.9.40 |
| --- | --- | --- |
| Instructions, gcc -O3 | 24,361,001 | 22,811,032 |
| Instructions, clang -O2 -fno-builtin -DJUKEBOX_SDK -msse3 | 24,004,436 | 23,161,210 |
| Worst block of the phrase, alternating pairs, gcc | 48.1%, 46.1% | 42.3%, 40.4% |
| Worst block of the stress phrase, alternating pairs, gcc | 42.6%, 38.0% | 40.7%, 35.6% |

The wall-clock pairs were taken with four other builds on the machine and
read 5-10 points above the quiet baseline table; the instruction counts do
not depend on that. The clang line is the closest stand-in here for the Rack
device's build (the Jukebox SDK compiles with clang at -O2 with no builtins,
to SSE3 or NEON, never AVX2); its lanes are the engine's `JUKEBOX_SDK` path.

**Null test.** The eight demos (hashes above), the stress phrase
(`a2ce53d367ec24b9`), the first-time chords (`55a3a685898ba91c`) and the
preset walk (`e8df7a9981226198`) hash as in 1.9.38.

**What a note-on still moves.** The two rings into the retained branch
(64 KB), the two fresh rings cleared (64 KB) and the contact transport's
history cleared (32 KB): 0.4 M of the block's 22.8 M instructions. Each ring
is dirty all round 170 ms after any string sounds, so a shorter clear would
need the loops to carry how far back they may read; not done.

**Library calls on the audio path, compiled as the Rack device is.** Under
`-fno-builtin` a libm call is a real call. A held block of the clang build
above is 9.08 M instructions, the stretch sum 57.8% of it, and makes one
kind of library call: `atan2`, once a control period, in
`bridgePortMobility` (6 K instructions). Nothing per sample: the engine's
`exact::` wrappers and inlined complex division already keep `fabs`, `floor`,
`sqrt` and the complex helpers inline. A note-on block calls `atan2`, `cos`,
`sin`, `cexp`, `exp`, `log1p`, `sqrt` and `pow` from the tuning solve and the
designs, 1.8 M of its 23.2 M instructions.

**What the 10% target would still take.** Unchanged from
[worst-block-profile-2026-11-09.md](worst-block-profile-2026-11-09.md): a
held chord is 8-12% of the period, half of it the stretch sum the owner kept
at four cells by ear, and each first-time note adds its tuning solve (1.3 M
instructions on a six-string shape) and dispersion design. No exact change
found here reaches 10% on a chord's attack block.

## The CpuBudget gate, 1.9.37 against 1.9.40

`Acustra.CpuBudget` times the eight demos and their stress variants in
256-frame blocks (median of three renders) and fails a row whose worst block
is over 10% of its period. It was run as 1.9.37 (`23e9b2c`), 1.9.40, 1.9.40,
1.9.37, with the machine's other builds still going (load average 3-6 on four
cores):

| 16 rows | 1.9.37 | 1.9.40 |
| --- | --- | --- |
| Real-time factor, mean | 0.098 | 0.098 |
| Demos, worst block (the better of two runs) | 17.8-28.9% | 17.9-28.9% |
| Stress variants, worst block (the better of two runs) | 33.0-43.3% | 32.7-43.1% |
| Rows over the 10% ceiling | 16 | 16 |

The gate fails on every row before and after, and the two versions do not
separate at this noise: single rows moved by up to 10 points between two
runs of the same binary. The 6% fewer instructions in a first-time chord
block are real and below what this clock resolves. The ceiling value belongs
to the shared harness and was not changed.
