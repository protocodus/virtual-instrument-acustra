# Playing physics, second pass — 2026-10-13

The user asked for new techniques, models and phenomena that make the
instrument more real, with no new parameter, one change per commit and a
blind A/B test once all of them are in. Each change below is on by default
and has an offline switch in `PerformanceRealism`, the ablation struct that
is neither a saved parameter nor a panel control; with every switch off the
engine renders the previous build bit for bit. Each section says what the
change does, what sizes it, how it is tested and what it leaves open.
Nothing here was chosen by ear.

The decision log, README Known gaps and the 2026-10-11 DSP completion
checklist were read first so that nothing already rejected would be
repeated: fret buzz, a measured contact click, audio-rate tension
modulation, automatic legato, per-string detuning, a position-shift squeak,
MIDI-triggered body knocks and a hiss floor stay out. Two measured
candidates were dropped on their evidence (end of this report).

## 1. Fingers pluck a chord together

Every chord of three or more notes was strummed, whatever the Picking: a
Finger chord crossed its strings at a pick's speed, 22 to 106 ms over six
strings, and alternated down and up as a pick does. A fingerstyle hand does
not do that with three or four strings. The thumb and fingers each take a
string and pluck them together.

`Tools/MeasureFingerChords.py` measures both hands on GuitarSet's comping
(Zenodo 3371780, CC BY 4.0, 180 takes). Its bossa nova comping is played
with the thumb and fingers; its rock, singer-songwriter and funk comping is
strummed with a pick. Every chord member's attack is detected again on its
own hex channel by one rule, so a detector lag common to the strings cannot
move the comparison. Report: [`finger-chords-report.json`](validation-playing-physics-20261013/finger-chords-report.json).

| | Finger (bossa nova) | Pick (rock, singer-songwriter, funk) |
| --- | ---: | ---: |
| Chords | 1,219 | 4,637 |
| In string order, low to high / high to low / neither | 16% / 26% / 59% | 46% / 15% / 40% |
| Three-string spread, median (quartiles) | 13.2 ms (7.6–20.4) | 20.1 ms (13.7–28.1) |
| Four-string spread | 16.9 ms (11.8–26.2) | 25.9 ms (18.7–34.7) |
| Lag behind the highest string, three strings, by rank from the top | 0, +2.27, +4.85 ms | 0, −6.67, −12.72 ms |
| Lag behind the highest string, four strings | 0, +2.36, +6.98, +3.06 ms | 0, −6.85, −13.11, −21.00 ms |
| Residual about that pattern, robust SD (3 / 4 strings) | 4.47 / 7.83 ms | 7.03 / 7.55 ms |

A pick sweeps: its strings run in order, top string last. Fingers do not:
most finger chords have no string order at all, the top string leads by a
few milliseconds on average, and the spread is two thirds of a pick's. Five
and six strings are rare in the finger takes (30 and 4 chords) and spread
like a brush.

**Model.** Under Finger, with no stroke direction latched, a chord the hand
plans on three or four strings is plucked (`Performer::flushNoteGroup`).
Each string takes the measured lag behind the highest string for its rank
plus a draw of its own, uniform with the three-string residual's 4.47 ms
standard deviation (half-width 7.74 ms); the earliest string sounds at the
note-on (`AcustraEngine::fingerChordOffsetSamples`). The four-string
residual's wider 7.83 ms is not used: rolled chords and missed attacks fill
its tail, and the detector's own scatter is in both figures. The strings
take a single note's pluck, so a plucked chord has no strum level jitter, no
upstroke tilt and no direction; it is not one of the strumming hand's
strokes and does not move their alternation. Five or six strings are
brushed as before, and Pick, Thumb and any chord after a direction keyswitch
strum exactly as before, so C0 or C#0 still asks for a finger strum.

**Tests** (`Acustra.FingerChords`): at 44.1, 48 and 96 kHz each rank's draws
have the measured mean lag within 0.12 ms, a 4.47 ms standard deviation
within 0.12 ms and stay inside the bounds; through the Performer, 60
three-string and 60 four-string chords never spread past what the lags
allow, the earliest string sounds at the note-on, both the highest and the
lowest string come first in some chords, and each rank's mean lag is the
measured one within 2.5 ms; a plucked chord takes the single-note direction
and leaves a following strum's down-up alternation as it was; Pick and Thumb
chords, five- and six-string Finger strums and a Finger chord after C0 are
bit-identical with the switch on and off, while the switch changes a
four-string Finger chord audibly; and the result is the same at blocks of
1, 61, 64 and 512 and in milliseconds at every rate. `Acustra.Performer` and
`Acustra.StrummingRealism` measure the sweep itself with three-string
Finger chords, so those fixtures turn the plucked chords off.

**Limits.** The finger takes are one style. The pattern is a median over
players and chords, and the draw is independent per string, not a hand's
correlated timing. Which finger takes which string, and the thumb's own
contact on the bass, are not modelled: every string keeps the Finger pluck.

## Dropped on their evidence

- **Strum speed following the tempo.** Pooled over every picked stroke, a
  stroke's string-to-string time falls with tempo (a log-log slope of
  −0.39), but most of that is between styles. Within one player playing
  one progression at its two tempi the exponent is −0.21 (take-resampled 90%
  interval −0.34 to −0.09), and rock and funk strum more slowly at their
  faster tempo. Too weak and too inconsistent to model.
- **The first and last string of a stroke.** In picked strokes of four or
  more strings a string is struck 2.3 dB lighter when the pick reaches it
  first and 5.1 dB lighter when it reaches it last than inside the stroke
  (90% intervals −3.1 to −1.5 and −5.6 to −3.9 dB). The 1.4.0 upstroke tilt
  was fitted on the same strokes without separating position from
  direction, so adding a position profile would count part of it twice; it
  needs a joint refit of both.
