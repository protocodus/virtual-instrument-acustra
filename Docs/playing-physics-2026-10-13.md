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

## 2. A strum mutes the strings it crosses

A chord voiced with a gap, open E2, B3 and E4 on the sixth, second and first
strings say, or 70 of the 235 triads and sevenths a keyboard voices in close
and open position (a probe of the hand allocator), left the strings in the
gap out of its stroke: the pick went through them without touching them. A
pick cannot. A guitarist keeps those strings quiet with the fretting hand,
and the pick gives each one a dead stroke on its way across.

`Tools/MeasureMutedStrokes.py` finds them on GuitarSet's hex channels.
Report: [`muted-strokes-report.json`](validation-playing-physics-20261013/muted-strokes-report.json).
In the corpus's ordered strokes 683 strings are crossed but left out of the
chord. At the moment the pick reaches one, interpolated between the attacks
on either side of it, its channel rises a median 9.4 dB (quartiles 3.5 to
14.4) over its own last 30 ms, where the first string beyond the stroke's
far end rises 0.0 dB on the bass side and 2.4 dB on the treble side. The
strike sits a median 4.6 dB under the same string's own played attacks
(quartiles −10.1 to −0.9), and the string then falls 20 dB in a median 125,
100, 75 and 60 ms on the A, D, G and B strings: 13.8 to 14.8 periods of each
open string.

**Model.** For a swept stroke whose strings the hand has planned, the
Performer gives each string strictly inside the span with no chord note a
dead stroke at its place in the sweep (`curvedStrumDelay`, with the
stroke's own speed draw). `AcustraEngine::muteStroke` plucks it open as a
member of the stroke, at 0.589 (−4.6 dB) of a played string's force, under
the finger's touch: 43 periods of T60, the 20 dB time taken as exponential,
1.4 dB a period. The touch holds from the moment the chord is fretted, so it
also stops whatever the string carried, a sympathetic ring or the last
chord's note. Once the pick has let the string go it is handed back to the
open string after that T60 and the hand's 80 ms. A string a key or the pedal
holds, or whose own pluck is still on its way, is left alone. Where the
finger lies on the string is not measured, so no local contact is added; an
authored one also made the engine fall faster than the corpus.

**Tests** (`Acustra.MutedStrings`): at 44.1, 48 and 96 kHz, down and up,
the three crossed strings of E2-B3-E4 are muted with no key owning them and
take their places in the sweep in order; the A string takes exactly the
43-period touch from the finger's landing, falls 60 dB within it and is
handed back after the T60 and 80 ms (within 2 ms); a key or the pedal holding
a crossed string keeps it out; chords with no gap under Pick, Finger and
Thumb, a finger-plucked chord, and the switch off are bit-identical, while
the dead strokes are audible against the switch off; a key on a muted string
takes it back and rings as on a fresh engine within 1 dB; panic clears a
muted stroke; and blocks of 1, 61, 64 and 512 render identically.

**Limits.** The hand mutes the string only for that T60 and 80 ms; a real
hand keeps touching it while the chord is held, and the engine lets it ring
sympathetically again afterwards. The strike and touch are medians over
strings and players. On the saddle force the engine's dead strokes fall 20
dB in 35 to 60 ms rather than the hex channel's 60 to 125, because that
force carries the pluck's broadband first pulse and the two polarisations'
beat; the touch is matched as a T60, not as that envelope. The strings just
outside a stroke are grazed in the corpus too (the string the pick passes
first rises a median 3.6 dB on the bass side and 9.0 dB on the treble, struck
18 to 19 dB under a played attack) and are not modelled.

## 3. The release fall, measured

1.4.0 let a released fretted note's pitch fall as the lifting fingertip
becomes the string's end, with an authored curve: one semitone approached
with a quarter of the hand's T60, stopping at 86% when the note is 30 dB
down. GuitarSet measures it. `Tools/MeasureReleaseFall.py` reads the
corpus's per-string pitch tracks against each note's own middle and the hex
channel's level against its sustain, over the last 200 ms of every note
whose string is not played again within 300 ms
([`release-fall-report.json`](validation-playing-physics-20261013/release-fall-report.json)):

| Level under the sustain | 0–3 dB | 3–6 | 6–10 | 10–15 | 15–20 | 20–25 | 25–30 | 30–40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fretted, 4,329 note ends, median cents | −0.4 | −1.9 | −15.7 | −28.9 | −39.2 | −49.8 | −61.6 | −80.5 |
| Open, 394 note ends, median cents | −0.1 | +1.8 | +5.8 | +10.0 | +16.9 | +6.0 | +7.3 | −4.8 |
| Authored 1.4.0 curve at the bin's centre | −10 | −26 | −41 | −56 | −69 | −78 | −84 | −86 |

The fretted notes hold their pitch for the first few decibels and then fall
about 2.3 cents a dB, to 80 cents at 35 dB down; the open strings, with no
fingertip to lift, do not fall. The authored curve fell far too early and a
little too far. The fall now follows the measured medians against the level
the hand's own damping has taken (60 dB per T60), interpolated linearly
between the bins' centres, and stops at 35 dB down; at the nominal fretted
0.32 s release that is 29 cents after 67 ms and 80.5 after 187 ms. A firmer
key-up still falls faster, a string fretted only in part still falls in
proportion, and the delay offset that carries the fall is unchanged.

**Tests** (`Acustra.ReleaseFall`, updated): the fall follows the measured
curve within one control period at 44.1, 48 and 96 kHz and stops at 80.5
cents; the isolated string's rendered fundamental falls 4.3 cents in a 50 ms
window centred 30 ms after the damping begins and 32.6 cents centred at 80 ms,
where the curve, weighted by the window and the decaying note, predicts 5.3
and 32.6; the re-fret, pedal, panic, rate, block and firm-key-up checks keep
their 1.4.0 meaning with thresholds moved to the new curve.

**Limits.** The curve is a median: real note ends spread widely about it
(at 30–40 dB down the quartiles are 6 and 288 cents) with how fast a finger
lets go, which a key-up's velocity only partly carries. The comping corpus
is one guitar and six players.

## 4. A strum's strings scatter about its sweep

A swept stroke put each string exactly where the wrist's arc and the
stroke's speed draw placed it: every strum sounded its strings in string
order, and a repeated chord kept one shape, only stretched. A pick does not
meet the strings on a line. `Tools/MeasureStrumScatter.py` fits each
GuitarSet stroke of four or more strings, its attacks re-detected by the
finger-chords rule, by least squares against string number, which is the
stroke's own sweep whatever its speed and direction, and reads each
string's moment about it. Report: [`strum-scatter-report.json`](validation-playing-physics-20261013/strum-scatter-report.json).

| | Pick (rock, singer-songwriter, funk) | Jazz |
| --- | ---: | ---: |
| Strokes of four to six strings | 2,430 | 719 |
| Each string about its stroke's sweep, robust SD (SD) | 4.53 ms (6.77) | 4.45 ms (6.71) |
| Four, five, six strings, robust SD | 4.19, 4.94, 5.40 ms | 4.18, 5.55, 5.52 ms |
| Downstrokes, upstrokes, robust SD | 4.35, 5.09 ms | 3.49, 6.07 ms |
| In string order at 4–12 ms a string, four / five / six strings | 65% / 49% / 34% | 70% / 43% / 27% |

The scatter is a time, not a place on the sweep: within each of 52 picked
takes, the slower half of its strokes, taking 1.43 times as long a string,
scatters 0.86 times as much as the faster half (quartiles 0.68 to 1.19),
where a place would scatter 1.43 times as much. Part of the 4.53 ms is the
detector's. An off-tree probe of this snapshot reads every string's bridge
reaction force, a stand-in for its hex channel, through 320 picked strokes
of eight chords ([`probes`](validation-playing-physics-20261013/probes)):
the same rule finds the unscattered strokes' attacks a robust 1.27 ms off
their own sweeps, where their scheduled plucks sit 0.36 ms off (the arc's
bend), which leaves 4.35 ms for the hand.

**Model.** Each string a swept stroke reaches, when the hand has planned
the chord, takes a uniform draw of 4.35 ms standard deviation (half-width
7.53 ms), a time added after the stroke's speed draw
(`AcustraEngine::strumScatterSamples`, `Performer::flushNoteGroup`). The
chord's own strings and the dead strokes of section 2 draw alike, six draws
a stroke from a generator of their own, so the speed draws are unchanged.
The earliest string, which can be any the stroke reaches, sounds at the
note-on, and at fast repeats none is taken past the next stroke's predicted
start, which the speed draw already keeps the sweep clear of. The size is
chosen from the robust spread; a check it was not fitted to agrees. Moved
by such a draw and the detector's 1.27 ms, each corpus stroke's own size and
time a string sound 63%, 47% and 32% of four-, five- and six-string strokes
in string order, against the corpus's 65%, 49% and 34%. Read back by the
probe, the engine's strokes now scatter a robust 4.79 ms (SD 4.51) and
sound 60%, 47% and 38% in string order, where the unscattered engine
measured 1.27 ms and 100%, 100% and 89%. Sixteen alternating E major
strokes 0.3 s apart correlate 0.015 between downstrokes and 0.034 between
upstrokes over their first 250 ms, against 0.093 and 0.127 unscattered. A
stroke costs six draws and a few divisions; nothing runs per sample.

**Tests** (`Acustra.StrumScatter`): at 44.1, 48 and 96 kHz the draws centre
within 0.06 ms of zero, spread 4.35 ms within 0.06 ms and fill ±7.53 ms;
through the Performer, at each rate, 160 six-string E major strokes keep
their speed draws, sound their earliest string at the note-on and move each
string from the unscattered stroke by 3.99 ms about the stroke's own mean
(4.35 sqrt(5/6) = 3.97 expected, within 0.3 ms), as much in the fastest
third of the speed draws as in the slowest (3.92 and 4.00 ms; a place would
differ by half); 43 of them sound in string order where the unscattered
sweep keeps all 160, and some downstrokes are led by a string other than
the low E. The dead strokes of E2-B3-E4 scatter with the chord, its earliest
contact at the note-on. A soft six-string stroke every 60 ms never
schedules a string past the next stroke or leaves one waiting into it.
Single notes under Pick and Finger, two-note groups, a finger-plucked
four-string chord and a seven-note group the hand cannot plan are
bit-identical with the switch on and off, while a strum changes audibly;
blocks of 1, 61, 64 and 512 render identically, and preparing again
restarts the draws. `Acustra.Performer`, `Acustra.StrummingRealism` and
`Acustra.MutedStrings` measure the sweep's own geometry, so those fixtures
turn the scatter off; one `Acustra.StrummingRealism` check now reads a
stroke's direction from the stroke instead of from which string sounds
first. With the switch off the probe's forces and plucks are the previous
build's bit for bit.

**Limits.** One draw for every string and stroke: the corpus's upstrokes
scatter more than its downstrokes (5.09 against 4.35 ms robust) and its
wider strokes more than its narrower (4.19 to 5.40 ms), and neither is
modelled. The draw has no tail, where 10% of the corpus's residuals exceed
10.6 ms, partly the detector's misses. The jazz takes scatter as much but
keep string order differently (70%, 43% and 27%), so the order check is the
picked styles'. Where a stroke starts against the beat is still the
sequencer's.

## 5. A pick's two-note group is a stroke

Only three or more notes on one sample were swept. Under Pick two notes, a
power chord or a double stop, sounded both strings on the same sample, and
one plectrum cannot do that: it reaches one string before the other.
`Tools/MeasureTwoStringStrokes.py` finds GuitarSet's two-string groups (two
strings' onsets inside the chord window with no other onset within 120 ms
either side) and detects both attacks again. Report: [`two-string-strokes-report.json`](validation-playing-physics-20261013/two-string-strokes-report.json).

| Two strings, adjacent | Pick (rock, singer-songwriter, funk) | Finger (bossa nova) | Jazz |
| --- | ---: | ---: | ---: |
| Groups | 1,526 | 293 | 268 |
| Apart, median (quartiles) | 13.0 ms (7.8–19.3) | 11.2 ms (6.0–16.1) | 9.8 ms (4.5–15.9) |
| Within 2 ms of each other | 5% | 8% | 13% |
| Lower string first / higher first, by more than 2 ms | 61% / 34% | 42% / 50% | 46% / 41% |

The picked pairs run mostly low string first, as down- and upstrokes in the
proportion strummers play them would; strings two and three or more apart
behave alike (135 and 99 groups, 10% and 6% within 2 ms). The attack
detector's own error, 1.27 ms on each attack (section 4), cannot part two
simultaneous attacks by 13 ms.

**Model.** Under Pick a two-note group the tuning can sound is a stroke
across its two strings (`Performer::flushNoteGroup`): it takes the strum's
sweep at the pick's speed for its velocity, the stroke's speed draw, each
string's own moment (section 4), the strum's force variation and upstroke
tilt, and a dead stroke on any string it crosses between them (section 2).
Its direction is the one the picked-note rule already gave it, so the
hand's alternation is unchanged. Fingers and the thumb still sound two notes
together.

**Tests** (`Acustra.TwoStringStrokes`): at 44.1, 48 and 96 kHz, 200
alternating E2-B2 power chords under Pick sound their first string at the
note-on, only 41 within 2 ms of each other (all 200 with the switch off),
the lower string first in 69 of 100 downstrokes and the higher first in 68
of 100 upstrokes, 5.36 ms apart on average along the stroke against the
sweep's 5.44 ms for that velocity, and keep the stroke direction they had
before; an E2-E3 octave shape gives the A string a dead stroke; a pair 10 ms
after a picked note continues that note's stroke; Finger and Thumb pairs,
picked single notes and picked strums are bit-identical with the switch on
and off, while picked pairs change audibly; blocks of 1, 61, 64 and 512
render identically. `Acustra.StrumScatter`'s unswept pairs are now Finger's.

**Limits.** The engine's picked pairs sound 5.4 ms apart on average at this
velocity where the corpus's are a median 13.0 ms apart, and 20% of them
within 2 ms against the corpus's 5%: they take a strum's speed for two
strings, while the corpus's smaller strokes cross each string more slowly
(isolated groups of two, three and four strings take 12.0, 8.6 and 7.7 ms a
string). Finger-played pairs are not simultaneous in the corpus either (8%
within 2 ms, in no preferred order), and stay so here.

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
