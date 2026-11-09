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
