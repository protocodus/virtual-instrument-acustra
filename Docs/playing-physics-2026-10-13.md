# Playing physics, second pass — 2026-10-13

The user asked for new techniques, models and phenomena that make the
instrument more real, with no new parameter, one change per commit and a
blind A/B test once all of them are in. Each of the seven changes below is
on by default and has an offline switch in `PerformanceRealism`, the
ablation struct that is neither a saved parameter nor a panel control. The
measured release fall replaced the authored one under the switch 1.4.0
already had, so with every switch off and the fall let go the engine renders
the previous build's own output with its fall let go, bit for bit. Each
section says what the change does, what sizes it, how it is tested and what
it leaves open. Nothing here was chosen by ear. The user then listened to
the blind A/B pack, picked the candidate in four of nine pairs and chose to
ship all seven; the pack, that listening and the CPU check close the report.

The decision log, README Known gaps and the 2026-10-11 DSP completion
checklist were read first so that nothing already rejected would be
repeated: fret buzz, a measured contact click, audio-rate tension
modulation, automatic legato, per-string detuning, a position-shift squeak,
MIDI-triggered body knocks and a hiss floor stay out. The candidates
dropped on their evidence are listed at the end of this report.

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

**Limits.** As committed, the engine's picked pairs sounded 5.4 ms apart on
average at this velocity where the corpus's are a median 13.0 ms apart, and
20% of them within 2 ms against the corpus's 5%: they took a strum's speed
for two strings, while the corpus's smaller strokes cross each string more
slowly. Section 6 measures that and corrects most of it. Finger-played pairs are not
simultaneous in the corpus either (8% within 2 ms, in no preferred order),
and stay so here.

## 6. A small stroke crosses each string more slowly

Every stroke crossed each string in the same time for its velocity: a
two-string stroke spanned a fifth of a six-string one, a three-string
stroke two fifths. GuitarSet's strummers do not sweep a small stroke at a
big one's speed. `Tools/MeasureStrokeSpan.py` takes every picked stroke of
two to six strings (runs of onsets on distinct strings inside the chord
window), detects each attack again, and fits its time a string by least
squares against string number. Report: [`stroke-span-report.json`](validation-playing-physics-20261013/stroke-span-report.json).

| Strings in the stroke | 2 | 3 | 4 | 5 | 6 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Strokes | 2,282 | 2,205 | 1,569 | 641 | 218 |
| Time a string, pooled median | 12.2 ms | 8.9 | 7.6 | 6.1 | 5.3 |
| Within a take, over its four-string median (takes) | 1.48 (64) | 1.15 (71) | 1 | 0.96 (37) | 0.85 (17) |
| Simulated at the spacings' −0.36 power, measured alike | 1.49 | 1.12 | 1 | 0.90 | 0.84 |
| Engine at MIDI 96, unscattered | 9.7 ms | 7.6 | 6.5 | 5.9 | 5.4 |
| … over its four-string time | 1.48 | 1.16 | 1 | 0.90 | 0.83 |

The pooled medians mix the takes: a take that plays many small strokes is
not the take that plays many big ones, at its own speed. One player's own
profile is the within-take ratio, a take's median time a string for a size
over its median for four strings (where it has five of each). To read an
exponent from it, every take's strokes are simulated at the take's
four-string median scaled by the spacings to a power, each string moved by
its own scatter (section 4) and the detector's error, and measured the same
way: the −0.36 power is the closest profile (90% interval over resampled
takes −0.41 to −0.31). A log-log regression of time a string on spacings
within takes reads −0.41, which that scatter should bias toward zero; it
weighs the occasional near-zero two-string time the scatter's tail makes,
which the medians do not, so the profile is the one taken.

**Model.** The pick's speed for a velocity is the six-string stroke's, the
map kept as it was; a stroke over fewer spacings crosses each one
(5 / spacings)^0.36 times more slowly, so its span grows as the spacings'
0.64 power (`AcustraEngine::strumDelaySamples`, which the Performer now
tells how many spacings the stroke crosses). Its own strings keep the
wrist's arc between its ends; a six-string stroke, a finger-plucked chord
and every single note are as before. At MIDI 96 a picked pair now sounds
9.6 ms apart on average, 8% within 2 ms, against the corpus's 13.0 ms and 5%.

**Tests** (`Acustra.StrokeSpan`): at 44.1, 48 and 96 kHz and four
velocities a stroke's span is the six-string span times (spacings / 5)^0.64
within a sample (in proportion with the switch off), the six-string span
and the default unchanged, and a stroke's own strings evenly spaced; through
the Performer, unscattered strokes of two to six strings under Pick each
span their speed draw times the law's span within a sample and cross each
string (5 / spacings)^0.36 times more slowly than six strings do (within
0.02 of the ratio); six-string strums, G major, a finger-plucked chord,
finger pairs and picked single notes are bit-identical with the switch on
and off, a three-string strum changes audibly, and blocks of 1, 61, 64 and
512 render identically. `Acustra.TwoStringStrokes` takes a pair's place from
the two-string law (200 alternating power chords: 9.6 ms apart on average
against 9.7, 16 of 200 within 2 ms, the lower string first in 87 of 100
downstrokes and the higher in 89 of 100 upstrokes).

**Limits.** The six-string anchor is a choice: the velocity map was pinned
to the pooled speeds of strokes of three to six strings, mostly three and
four, and keeping six strings where they were changes the least of what is
already heard. Against the pooled medians the engine's pairs at MIDI 96 are
quicker (9.7 against 12.2 ms a string): the pooled figure carries the takes'
mix, which the profile leaves out. The jazz and finger takes' small groups are not strokes in the same
sense and are not used. How a stroke's speed follows its force within a
player is a separate measurement.

## 7. A stroke's edges are lighter, and the upstroke's tilt refit

1.4.0 tilts an upstroke's strings toward the treble by 0.9 dB a string with
a 1.2 dB offset at mid-neck, the line GuitarSet gives when each string's
upstroke attacks are compared with its own downstroke attacks over a whole
take (`Tools/MeasureStrumDirection.py`). That comparison carries whatever
else the strokes held: the corpus's upstrokes reach the bass strings half as
often as its downstrokes, and the first and last strings of a stroke were
found lighter (Dropped, below). `Tools/MeasureStrokeEdges.py` fits both at
once inside single strokes: each string's attack on its own channel, less
that string's median in the take, so the coil's gain cancels; then against
the rest of its own stroke, so the stroke's force and any accent cancel; then
by least squares as the entry and exit strings' differences from the
strings inside, plus a tilt across string number for each direction. A
stroke enters on its lowest string going down and its highest going up.
Report: [`stroke-edges-report.json`](validation-playing-physics-20261013/stroke-edges-report.json).

| 2,344 picked strokes of four to six strings, 85 takes | Energy added | 40 ms peak | Transient |
| --- | ---: | ---: | ---: |
| Entry string under those inside (90% over takes) | 1.47 dB (1.15–1.80) | 1.76 (1.45–2.11) | 2.15 (1.75–2.53) |
| Exit string under those inside | 1.86 dB (1.54–2.20) | 1.81 (1.55–2.07) | 2.20 (1.86–2.54) |
| Upstroke's tilt over the downstroke's, inside a stroke | +0.09 dB a string | +0.13 | +0.68 |
| The same without the edges in the fit | +0.25 | +0.15 | +0.69 |

Inside one stroke the pick strikes the strings it enters and leaves on
about 2 dB more lightly than those between, going down as much as going up,
and an upstroke tilts its strings by a fraction of the whole takes' line.
The transient measure keeps more of a tilt; the energy and the peak hardly
any. The 1.2 dB offset is a difference between strokes and cannot be seen
inside one; it stays as measured.

**Model.** For a stroke of three or more strings the Performer names the
strings it enters and leaves the chord on (`AcustraEngine::beginStrum`), and
each string captures its part at note-on, as it captures the stroke's
direction, carried with it if the forming hand moves it
(`AcustraEngine::strumEdgeGain`). The entry string is released at 0.813 of
its force (−1.8 dB) and the exit string at 0.794 (−2.0 dB), the means of the
three measures; a pair has no strings inside to be lighter than and takes
none. An upstroke's tilt is the measures' mean, 0.30 dB a string, on the
same −1.2 dB offset: `−1.2 + 0.30 (k − 2.5)` dB, k 0 for the low E. Dead
strokes are inside the stroke by definition.

**Tests** (`Acustra.StrokeEdges`): at 44.1, 48 and 96 kHz under Finger,
Pick and Thumb, each string's released force against the switch off is its
edge's gain times the change from the 0.9 tilt to 0.30, within 1e-5 of the
ratio, a downstroke's inner strings exactly as before and a stroke naming no
edges unchanged; through the Performer, D major's D and high E strings are
its entry and exit in each direction and its G and B strings take no edge
gain, and E2-B3-E4's dead A, D and G strings take none while the low E is
its end (within 0.2%: open and handed-back strings keep a trace of the
bridge's motion); picked single notes, downstroke pairs, a finger-plucked
chord and a seven-note group the hand cannot plan are bit-identical with the
switch on and off, while six-string strums change audibly; blocks of 1, 61,
64 and 512 render identically. `Acustra.StrokeDepth` keeps testing the 1.4.0
line with the switch off.

**Limits.** The edges are measured on strokes of four to six strings and
applied to every stroke of three or more; three-string strokes cannot part
an edge from a tilt (both lie along the same three strings), though their
exit string is lighter too. The edge gains are means over the three
measures, which disagree most on the tilt. This replaces part of what the
user heard and preferred in 1.4.0, as a measured correction, and the user
picked the 1.4.0 tilt over it in the blind pack (Listening, below).

## The A/B pack

`Tools/BuildPlayingPhysicsReview.py` freezes two DSP trees, 1.4.0 (4baeb82,
the engine the user heard and preferred) and the candidate with all seven
changes, compiles `Tools/RenderPlayingPhysicsReview.cpp` against each with
clang++ -O3, and renders ten 8 s scores at the plug-in's default controls,
48 kHz and 64-frame blocks. The candidate's build can switch each mechanism
off on its own, or all of them. Each pair isolates one change where it can:

| Pair | Score | Candidate | Against |
| --- | --- | --- | --- |
| 1 | Fingerstyle comping (Finger) | candidate | finger chords off |
| 2 | Open-voiced strums (Pick) | candidate | dead strokes off |
| 3 | Staccato fingerstyle (Finger) | every change off but the fall, measured | 1.4.0, authored fall |
| 4 | One chord, strummed over and over (Pick) | candidate | scatter off |
| 5 | Power chords (Pick) | candidate | two-string strokes off |
| 6 | Small strokes (Pick) | candidate | span law off |
| 7 | Down-up strumming (Pick) | candidate | stroke edges off (the 1.4.0 tilt) |
| 8 | A picked song | candidate | 1.4.0 |
| 9 | A fingerstyle song | candidate | 1.4.0 |
| 10 | Open strings, one at a time | candidate | 1.4.0, bit-identical by design |

The build stops unless every score renders, with every mechanism off and the
fall let go, the 1.4.0 tree's own fall-off render bit for bit; the control
renders 1.4.0 bit for bit; each mechanism's own score changes when only it is
off; and the measured fall alone changes the staccato score. All four hold.
The files share one gain (the loudest render 1 dB under full scale) and are
written at 16 bits with no EQ, separate normalization, compression or timing
alignment. The candidate sits on A in five of the ten pairs, drawn by a
generator seeded with the candidate renderer's hash; 1.4.0's pack, seeded
the same way but unbalanced, put it on A in all four test pairs. The key
and each pair's level and difference measurements stayed outside the
repository until the listening verdict, since a pair's level change would
tell its takes apart. They are now committed, the key as
[`review-key.json`](validation-playing-physics-20261013/review-key.json)
and the measurements in
[`review-manifest.json`](validation-playing-physics-20261013/review-manifest.json)
beside the frozen sources' and renders' hashes and the checks. A copy of
the key went into the listening page's store, which the page never reads,
at 01:33 UTC on 2026-10-14, nearly three hours before the first answer;
it is unchanged and matches.

### Listening

The user listened blind on a page that played each pair's takes as A and B
and kept the key away, choosing one take a pair; "can't tell" was offered
and not used, and the control had no choice.

| Pair | What differed | Candidate | Picked | Preferred |
| --- | --- | --- | --- | --- |
| 1 | Finger chords | A | B | off, the chord swept |
| 2 | Dead strokes | A | A | **on** |
| 3 | The measured fall, alone | A | B | 1.4.0's authored fall |
| 4 | Strum scatter | A | A | **on** |
| 5 | Picked pairs | A | A | **on** |
| 6 | Stroke span law | B | A | off |
| 7 | Stroke edges and the 0.30 dB tilt | B | A | off, 1.4.0's 0.9 dB tilt |
| 8 | A picked song | B | B | **1.5.0** |
| 9 | A fingerstyle song | B | A | 1.4.0 |

That is four of nine for the candidate. The fingerstyle song went to
1.4.0, and of the six new switches only the finger chords' changes it. The
picked song went to 1.5.0; the scatter, picked pairs, span law and edges
all change it, though the span law and edges lost on their own. Each row is
one forced choice by one listener. The draw put the candidate on A in pairs
1 to 5 and on B in 6 to 10, an order a balanced shuffle gives one time in
252, and A was picked six times in nine, so a preference for A cannot be
excluded; the playback chain was not recorded. After seeing the key the
user chose to ship all seven as they are. The finger chords, the measured
fall, the span law and the stroke edges therefore ship against the user's
own blind pick, as measured corrections, and each keeps its
`PerformanceRealism` switch.

## CPU

The changes add work at a stroke's or a release's events, not per sample:
six scatter draws and a few divisions a stroke, a finger chord's draws, a
dead stroke's pluck, a captured edge gain, and the measured fall's curve
read once a control period. Paired probes, compiled with g++ 13.3 -O3
against the frozen source on a 4-CPU Xeon at 2.1 GHz (a cloud VM, nothing
else running; not a quiet-machine benchmark,
[`probes/results.txt`](validation-playing-physics-20261013/probes/results.txt)):

- One build, the seven switches on against off, 14 s played passages: a
  picked song with full, open-voiced and three-string strums, power chords
  and single notes, −2.4% (48 kHz, 64 frames) and −1.8% (96 kHz, 32) on the
  median callback; a fingerstyle one with plucked chords, +1.4% and −0.2%.
  Run-to-run noise is of that size. Pooled p95 and p99 move by as much
  either way, except the fingerstyle p95 at 48 kHz (178 against 158 µs),
  where a plucked chord's strings start within a few milliseconds of one
  another, within one callback's 1.3 ms.
- Both trees built alike, the 1.4.0 release probe (an F barre let go, the
  0.45 s after its damping begins): with the fall let go the two engines
  cost the same (88–91 µs at 48 kHz, 42–43 at 96 kHz). The measured fall
  adds 5.7–6.4% to the mean callback at 48 kHz and 6.0–7.0% at 96 kHz
  against 1.4.0's 2.7–4.9% and 4.6–8.4%; it now runs to 35 dB down instead
  of stopping at 30. The median callback shows more (11.7–12.4% against
  7.5–7.7% at 48 kHz) because the fall is active in nearly half the
  window's callbacks.
- 1.4.0's own song probe on both trees: within run-to-run noise (90–94 µs
  at 48 kHz, 43.6–44.0 at 96 kHz on the median callback).

At 48 kHz and 64 frames a callback has 1,333 µs and these take 7–8% of it;
at 96 kHz and 32 frames, 333 µs and 13–15%. A local timing is no guarantee
on another processor or host.

## Dropped on their evidence

- **Strum speed following the tempo.** Pooled over every picked stroke, a
  stroke's string-to-string time falls with tempo (a log-log slope of
  −0.39), but most of that is between styles. Within one player playing
  one progression at its two tempi the exponent is −0.21 (take-resampled 90%
  interval −0.34 to −0.09), and rock and funk strum more slowly at their
  faster tempo. Too weak and too inconsistent to model.
- **The first and last string of a stroke, on their own.** In picked
  strokes of four or more strings a string is struck 2.3 dB lighter when the
  pick reaches it first and 5.1 dB lighter when it reaches it last than
  inside the stroke (90% intervals −3.1 to −1.5 and −5.6 to −3.9 dB), by
  attack time on one measure. The 1.4.0 upstroke tilt was fitted on the same
  strokes without separating position from direction, so adding this
  profile would have counted part of it twice. Section 7 refits both
  jointly, by the stroke's spatial ends, on all three measures.
- **Strum direction from the beat grid.** A rule reading the beat (down on
  the eighth-note grid, up between) names GuitarSet's stroke directions
  right more often overall than the engine's interval rule, 67.6% of 3,910
  strokes against 62.7%, but only through the bossa nova and jazz takes
  (70.4% against 39.0%, 80.5% against 62.8%). On the picked styles it is
  worse: rock 67.4% against 75.4%, singer-songwriter 60.4% against 63.4%,
  funk 63.4% against 61.2%. The interval rule stays (the README's Known
  gaps keep the listening question).
- **A stroke's speed following its force.** Within takes, controlling a
  stroke's size and direction, a stroke 10 dB louder on its own channels
  takes 0.836 times as long a string (take-resampled 90% interval 0.78 to
  0.90), or 0.70 if the level measure's split-half reliability (0.50) is
  corrected for. The velocity map is a player's map that also
  carries how fast a player strums a song at all, which a within-take slope
  does not measure, so it stays.
- **The thumb on the bass of a finger chord.** Fingerstyle puts the thumb on
  the bass strings, but the engine's Thumb contact is authored, not fitted
  to thumb-plucked recordings (the real-note archive merges finger and thumb
  labels), so giving it the bass would trade one unmeasured timbre for
  another.
- **Finger-played pairs.** GuitarSet's isolated finger-played two-string
  groups are no more simultaneous than picked ones (8% within 2 ms, a
  median 11.2 ms apart) but take no order (42% lower string first, 50%
  higher): two fingers a hundredth of a second apart, not a pinch. Without a
  measure of when a player means that, Finger pairs still sound together.
