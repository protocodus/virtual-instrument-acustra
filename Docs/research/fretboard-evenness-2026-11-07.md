# Fretboard evenness: the level up the neck, 1.9.25 — 2026-11-07

**Question.** The owner suspected the recent rounds had made the model worse
and asked for every playable note at two velocity layers to be checked for
outliers in tone, level and resonance. This note records what was measured,
what was found wrong, what was changed and what was left.

## Tools

- `Tools/MeasureFretboardSweep.py` (new): every string at frets 0-20, at
  velocities 48 and 112, for each of the eight factory presets through
  `AcustraPerformanceRenderer`; levels, centroids, fundamental and 2-6 kHz
  T60, H2/H3, between-partial floor, release and tail, each as a deviation
  from the frets around it, and the same pitch on two strings. Protocol at
  its head.
- `Tools/MeasureCorpusLevels.py` (new): level against pitch in the CC0
  Eastman E1D chromatic walks (`Tools/PrepareEastmanCorpus.py`, fetched, never
  committed), beside the same slope read from a sweep report.
- The under-saddle element's open-circuit voltage
  (`AcustraPerformanceRenderer --observe piezo_levels`) for the force the
  string puts on the saddle, open against fretted.

Baseline 1.9.24 (`ff7268f`); candidate the working tree of 1.9.25.
48 kHz, Release, GCC, default controls, Room 0.

## What the recent rounds did (1.9.9 to 1.9.24)

Single notes rendered through each round: level within 1 dB throughout, the
1.9.22 CPU round bit-identical on single notes and through the room, H3 moving
1-4 dB on the Finger from the 1.9.15/1.9.19 pluck-point easing, the Pick
unchanged. The 1.9.14 axial force sits 24-28 dB under the note. None of the
rounds degraded isolated notes. What was wrong is older.

## Found wrong

### 1. The same stroke got louder up the neck

The release amplitude took a hand at a fixed distance from the bridge:
`(1 - p) / (1 - a / L0)`. But the Finger's pluck-length easing (1.9.15,
1.9.19) and the 0.25 to 0.36 knee move the hand toward the bridge on stopped
strings, where the string is stiffer to it, and the formula did not know.
The equal-force law, deflection `F L p (1 - p) / T`, gives a compliance ratio
of 0.59 at the 12th fret where the code applied 0.91.

Saddle force (element peak, fret 12 against open, Finger, each string):

| String | 1.9.24 | 1.9.25 |
| --- | --- | --- |
| E2 | +2.8 dB | −0.5 dB |
| A2 | +2.9 | −0.3 |
| D3 | +3.0 | −0.2 |
| G3 | +2.8 | −0.4 |
| B3 | +3.5 | +0.7 |
| E4 | +0.3 | −2.0 |

Thumb, A and G strings, element peak at frets 6 / 12 / 18 against open:
1.9.24 +1.8 / +4.3 / +7.7 and +3.0 / +7.0 / +8.0 dB; 1.9.25 +0.2 / +0.7 /
+1.3 and +1.6 / +2.9 / +1.8 dB.

At the microphones (Dreadnought Steel, velocity 112, attack level of frets
13-20 against open, by string): 1.9.24 +4.1, +4.9, +4.0, −1.4, +4.8, +5.5 dB;
1.9.25 −0.6, −0.0, −1.0, −6.6, −0.1, +0.7 dB. The G string's open note sits
on the top's main resonance, so its own reference is high. The same pitch on
two strings now steps 1.6 dB on average where it stepped 2.2-2.6
(Finger and Thumb presets; the Pick presets are bit-identical, since a Pick's
hand sits below the knee and a fixed distance is what it keeps).

Against a real guitar: the Eastman E1D walks, one guitar, one microphone, one
take per technique, get quieter with pitch at −4.2 dB/octave finger-plucked
and −1.9 picked over MIDI 40-76 (sustain −4.1 and −3.3). The model's
Dreadnought Steel went from −1.6 to −3.1 dB/octave (sustain −1.4 to −2.9),
the Thumb preset from −2.2 to −4.0. The Pick presets stay at −0.4 to −1.0.
The player's effort is inside the recorded slope, and the recordings do not
say which string each note was on (the model takes the median over the
strings that reach the pitch), so this is a direction and a magnitude, not a
fit.

### 2. The treble-retention shelf was gated by string number

1.9.15 kept more of the upper partials per round trip on stopped notes, but
only on strings 4-6, only above MIDI 66, and with an exponent of 0.65: the
D string at MIDI 67 and the G string at MIDI 67 were damped by different laws.
A loss applied once per round trip and designed on the open string drains a
given partial faster in seconds by the ratio of round-trip rates, so keeping
a partial's lifetime independent of the fret means raising the per-trip
retention to `f_open / f` on every string (`fretHighLoss`). At the 20th fret
the per-trip loss is 0.32 of the open string's where 1.9.15 kept 0.51. The
wound strings' 2-6 kHz T60 is set by their dislocation and bending sections,
not this shelf, and its fret 13-20 to open ratio moves under 3 %; the sweep's 2-6 kHz T60 on the plain
strings is within its own scatter either way. This is a formulation choice
between two defensible laws and is recorded as such in the decision log.

## Checked and left

- Body holes: Dreadnought C5/C#5 (the E string's 8th-9th fret note sits
  5 dB down with a 20-70 s fundamental), Parlor B3, Auditorium G#3/G#4. They
  are antiresonances of the measured g21 body (`MeasuredBodyData.h`, Mores,
  Zenodo 4604577) between its fitted modes; a real guitar has them. Not a
  modelling defect; a listening test could still decide whether to want them.
- The B string's inharmonicity on frets 15-20 (its fitted stiffness).
- A unison on two strings grows after key-up on one of them (the coupled
  pair's antisymmetric mode) and the fundamental T60 reads as infinite there.
- The 1.9.24 shift level, chosen by the owner by ear.
- The Pick's level-against-pitch slope stays flatter than the picked
  recording (−0.4 to −1.0 against −1.9 dB/octave attack, −3.3 sustain). Its
  hand distance is fixed below the knee, so this change does not reach it;
  the difference is the body's and the player's and is left open.

## Verification

`Acustra.Engine` (its fret-20 retention floor rewritten to the new law, the
reason beside it), `Release`, `VelocityRealism`, `StringPitchRealism`, the
`unit`, `audio` and `perf` tiers and `AcustraDSPCxx17`: results in the
commit message.
