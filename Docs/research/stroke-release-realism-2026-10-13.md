# Stroke direction and released notes — 2026-10-13

The user asked for a few novel ways to make the guitar more real, across
sound, resonance, playing, attack, release, sustain, artifacts and dynamics,
implemented and A/B tested, with no new parameter and no GUI change. This
pass adds three behaviours a real guitar has and Acustra did not:

| Mechanism | What changes | Evidence for it |
| --- | --- | --- |
| Stroke depth | An upstroke lands lighter on the bass strings and harder on the treble ones than a downstroke | GuitarSet's hex-pickup comping, measured here per string |
| Alternate picking | Single notes with a pick alternate down and up, one hand with the strums | The plectrum's own motion; the engine already reverses a return stroke's sideways release |
| Release fall | A released fretted note falls toward the previous fret while the hand damps it | Roland's guitar-to-MIDI patent, sized by the fret geometry |

Each is on by default and has an offline switch in `PerformanceRealism`
(`strokeDepth`, `alternatePicking`, `releasePitchFall`), the existing
ablation struct that is neither a saved parameter nor a panel control. All
three off renders bit for bit the previous engine (below). Nothing here was
chosen by ear; the user then listened to the blind A/B pack and preferred
the candidate (below).

## Candidates considered

The decision log and README Known gaps were read first so that nothing
rejected would be repeated. Fret buzz (rejected for want of a measured
displacement), a measured contact click (rejected by ear), audio-rate
tension modulation (inert at the fitted displacement), longitudinal axial
modes (the "water drop"), automatic legato (removed at the user's request)
and fretting sharpness or per-string detuning (the user asked for coherent
tuning on 2026-10-13) were left alone. A position-shift squeak was weighed and
dropped: it happens in the gap before the next note, which a real-time
instrument cannot know, and rendered at the next onset it is masked by the
attack. A back-plate oscillator and neck dead spots would need measurements
this project does not have and would move the calibrated single-note sound.
The three chosen act where a performance does, not on a single calibrated
note, so the recording benchmark and the construction loudness calibration
are untouched by construction.

## 1. Stroke depth: upstrokes lean treble

`Tools/MeasureStrumDirection.py` reads GuitarSet's 180 comping takes (Xi et
al., ISMIR 2018, Zenodo 3371780, CC BY 4.0; hashes of every input in
[`../validation/stroke-release-2026-10-13/guitarset-inputs.sha256`](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/stroke-release-2026-10-13/guitarset-inputs.sha256)).
Strokes are `Tools/MeasureStrums.py`'s onset clusters (three or more strings
inside 75 ms, the shortest sixteenth note of the takes): 5,501 downstrokes and
3,162 upstrokes. The hex pickup's per-coil gain is uncalibrated, so a string
is only ever compared with itself on its own channel in the same take: the
median attack over its upstrokes against the median over its downstrokes.
The coil gain cancels from that ratio.

Two things in the data had to be handled first:

- GuitarSet's annotated onsets trail the attacks. Averaged over every
  clustered onset, the hex channels' first-difference energy starts rising
  16 ms before the annotation and peaks 4 ms before it. The first version
  of the tool used the annotations as they are and found a ±5 dB profile,
  most of it the window catching the attack in its "before" half; every
  window now starts 16 ms early.
- A comping string rings on from the stroke before. The main measure is
  the energy the onset added (40 ms mean square less the 20 ms before);
  the first difference of the same (weighting the attack's broadband
  transient) and the plain 40 ms peak are reported beside it.

Up minus down, median over takes, dB:

| Measure | E2 | A2 | D3 | G3 | B3 | E4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Added energy | −1.28 | −2.45 | −1.98 | −1.25 | +0.70 | +1.11 |
| Transient | −2.05 | −3.74 | −2.41 | −2.50 | +0.57 | +0.74 |
| Peak | −1.54 | −2.09 | −1.60 | −0.96 | +0.53 | +0.69 |
| Takes | 63–64 | 82 | 123–125 | 130–131 | 119–121 | 66 |

A line through each take's own ratios against string number gives the
stroke's tilt with nothing common to the take in it. Over the 128 takes with
three or more strings in both directions, median and take-resampled 90%
interval:

| Measure | Tilt toward the high E (dB per string) | Value between D and G (dB) |
| --- | ---: | ---: |
| Added energy | +1.03 [+0.68, +1.72] | −1.11 [−2.12, −0.54] |
| Transient | +0.92 [+0.62, +1.41] | −1.59 [−2.59, −1.30] |
| Peak | +0.76 [+0.47, +1.07] | −0.89 [−1.32, −0.51] |

The engine uses the means: an upstroke string is released at
`−1.2 + 0.9 (k − 2.5)` dB of the force it would have on a downstroke, string
`k` 0 for the low E. The low E sits 3.45 dB under, the A 2.55, the D 1.65 and
the G 0.75; the B is 0.15 dB over and the high E 1.05. Downstrokes are the
reference and are bit-identical. The force is the hand's, so a lighter
string is also let go more slowly and darker, as the contact law already
does for a softer stroke. Single notes are aimed at their string and are not
tilted.

The offset common to the strings may be the return stroke's own lightness or
the players' accents on the beat: the corpus cannot separate them. A first
version left it out, attributing it all to accents that MIDI velocity
carries; at the microphones its upstrokes came out about 1.3 dB louder than
before, because the treble strings it raised radiate more than the bass
strings it lowered, and no take shows an upstroke louder than its downstroke.
With the measured offset, a six-string E major upstroke loses 0.69 dB at the
stereo microphones, 0.83 dB in its band below 250 Hz, and gains 0.44 dB in
its first difference (`Acustra.StrokeDepth`).

Measured and not modelled: upstrokes reach the bass strings half as often
as downstrokes (E2 in 14% of upstrokes against 29%, A2 30% against 61%) and
the high E twice as often (32% against 14%). A note a MIDI chord names is
always struck; the reach difference stays a player's choice of notes. The
upstrokes' annotated span is also 18% longer per string crossed (0.82 [0.77,
0.94] of the downstrokes' speed), which weak attacks' later onset detection
may explain in part; the strum timing is unchanged.

## 2. Alternate picking

A plectrum moving down then up releases the string sideways one way then
the other. Strums already reverse their sideways release on the return
stroke (2026-10-07), but every single picked note was a downstroke, so a
picked line or a tremolo repeated one stroke.

Under Pick the Performer now treats a single note, or a two-note group, as a
stroke of the same hand as the strums, with the strum's own rule: a stroke
within a dotted eighth of the hand's last (at the host tempo) is the return
and alternates; after a longer gap, or a rest over two seconds, it is a
downstroke. A stroke within the chord window (30 ms) of the last continues
it in the same direction, so a live chord's keys, or a guitar controller's
strings, arriving a few milliseconds apart are one stroke and not a flip per
note. Under Pick that continuation also applies to a strum completing such a
stroke. The direction keyswitches latch picked notes as they latch strums. A
connected transition (CC65 with CC84) is the fretting hand's and is not a
stroke. Finger and Thumb pluck toward the palm and are untouched: a Finger
performance renders bit for bit as before (`Acustra.AlternatePicking`).

`AcustraEngine::noteOn` gains a defaulted `upstroke` argument for a single
note's direction; its default, `false`, is the downstroke every single note
had. An upstroke reverses the fresh sideways displacement, release velocity
and contact sources exactly as a return strum does; old waves keep theirs.

Sharing one hand changes strums that follow picked notes under Pick. A bass
note on the beat with a strum on the off-beat used to make that strum a
downstroke, then the next on-beat strum an upstroke; the strum is now the
return of the bass note's downstroke and the on-beat strum a downstroke, as
the hand moves.

## 3. Release fall

A fretted note is stopped by the fingertip pressing the string onto the
fret. To end it the finger relaxes: the string lifts off the fret while the
fingertip still holds it, and the fingertip, behind the fret, becomes the
string's end. Roland's guitar-to-MIDI patent describes this in the pitch its
hexaphonic pickups track: "the finger that is in contact with the string
becomes the endpoint node"; when the finger is released, "a drop in the
pitch starts and following that, the envelope level begins to rapidly
attenuate", and when the envelope is nearly zero "the pitch drops about a
half tone"; the drop varies with how fast the finger lets go and from string
to string (US 7470851 B2, T. Haga, Roland Corporation, FIG. 4; its detector
accepts 70 to 130 cents as such a drop). Acustra damped a released note at
its fretted pitch.

The fingertip lies between the fret and the one below it, so the speaking
length grows toward the previous fret's: the fall approaches one semitone.
It approaches with a time constant of a quarter of the hand's T60 (the
nominal fretted release is 0.32 s), so it leads the damping as the patent
describes: a third of a semitone when the note has lost 6 dB and 0.86 when
it has lost 30 dB, half the T60, where it stops. A firmer key-up damps
faster and falls faster with it. Open strings have no finger on a fret and
do not fall; nor does a note under the pedal or the Sustain switch until it
is let go. A new stroke on the string presses it back onto the fret.

The fall lengthens each loop's configured delay by `P (2^(−fall/12) − 1)`
samples, `P` the string's period, and the loop's delay follows on its usual
6 ms retune. A first version moved the string as a slide, re-solving its
dispersion and bridge phase at every control period: a six-string release
cost half again a callback, and at 96 kHz with 32-frame blocks its 99th
percentile overran the 333 µs budget. The dispersion, bridge compensation
and hand loss now stay as the fretted note had them, a period at most 6%
shorter than the one sounding, on a note being damped; the speaking length
stays the fretted one, so the fall is not a finger sliding on the winding.

The isolated string's rendered fundamental, against a twin that does not
fall, drops 26.5 cents over a 50 ms window centred 30 ms after the damping
begins and 59.5 cents centred at 80 ms, where the curve, weighted by the
window and the decaying note, predicts 27.8 and 61.3 (`Acustra.ReleaseFall`,
48 and 96 kHz).

## Tests

Three new suites, all exact where the property is exact:

- `Acustra.StrokeDepth`: at 44.1, 48 and 96 kHz and for every technique,
  each upstroke string's released force is the measured line to 2.7e-7
  relative, and a downstroke's is bit-identical; a single upstroke is not
  tilted; quarter-note downstroke strums render bit for bit with the depth on
  or off; an E major down-up pattern keeps its first downstroke exactly and
  its upstrokes move energy from the bass band toward the treble without
  getting louder.
- `Acustra.AlternatePicking`: eighths and sixteenths alternate; quarter
  notes and a line after a rest start down; Finger and Thumb never take an
  upstroke and a Finger performance with strums is bit-identical; a bass note
  makes the off-beat strum an upstroke and the next on-beat strum a
  downstroke; a rolled chord and a picked note completed by a strum within
  30 ms keep one direction; both keyswitch latches hold for picked notes; a
  connected transition is not a stroke; the switch off picks every note down;
  the upstroke is audible against a downstroke and identical at blocks of 1,
  61, 64 and 512.
- `Acustra.ReleaseFall`: the fall follows `1 − exp(−t / (T60/4))` within one
  control period until it stops at half the T60, at 44.1, 48 and 96 kHz, and
  both loops carry it as added delay to 1.7e-7 of a period; the rendered
  pitch falls as above; open strings are bit-identical; a re-fret clears the
  fall and makes no sliding scrape on a wound string with Release Noise at
  70%; the pedal holds the fall until it lifts; hand-back and panic clear it;
  a G chord's falling release never peaks above the same chord held; the
  fall at 50 ms agrees across rates within 0.015 semitone and renders
  identically at blocks of 1, 37, 64 and 512; a firm key-up falls faster
  than a nominal one and a gentle one slower; an MPE member's tension bend
  keeps its tension.

`Acustra.StrumDirection` checks that a return stroke reverses only its
sideways field, with every other part of the release equal. The stroke
depth now changes an upstroke string's force on purpose, so that check runs
with the depth switched off; its exact equalities are unchanged and the
depth has its own exact test.

## The A/B pack

`Tools/BuildStrokeReleaseReview.py` freezes the previous engine (`99b15e7`)
and this one, compiles the same renderer
(`Tools/RenderStrokeReleaseReview.cpp`) against each, and renders five
8-second scores at the plug-in's defaults (Output 0 dB, Room 50 %, Piezo Mix
20 %, Release Noise 10 %, stereo microphones, Dreadnought Spruce), 48 kHz,
64-frame blocks. Before writing anything it checks that every score renders
bit for bit the previous engine with all three switches off, that the
open-string control is bit-identical with all three on, and that each
mechanism changes its own score. Each pair is blind: which of A and B is the
candidate is in `key.json`, kept off the page, and every file shares one
gain.

Which mechanism reaches which score (a score changes when that switch alone
is turned off):

| Score | Stroke depth | Alternate picking | Release fall |
| --- | :---: | :---: | :---: |
| 1. Down-up strumming (Pick) | yes | no | yes |
| 2. Picked line and tremolo (Pick) | no | yes | yes |
| 3. Staccato fingerstyle (Finger) | no | no | yes |
| 4. Song (Pick) | yes | yes | yes |
| 5. Open-string control (Finger) | no | no | no |

The pairs, candidate against the version named (native levels, before the
shared gain of 0.979):

| Pair | Against | Level change | Difference re. RMS | First difference |
| --- | --- | ---: | ---: | ---: |
| 1. Down-up strumming | depth off | −0.07 dB | −11.7 dB | 0.389 s, the first upstroke |
| 2. Picked line and tremolo | alternation off | +1.88 dB | −0.6 dB | 0.368 s, the second note |
| 3. Staccato fingerstyle | fall off | +0.03 dB | −23.0 dB | 0.475 s, the first release |
| 4. All three together | previous engine | +0.38 dB | −10.9 dB | 0.772 s |
| 5. Unchanged control | previous engine | 0 | identical | — |

In pair 1 the upstrokes' first 60 ms lose 0.56 dB below 250 Hz and gain 0.17
dB in their first difference (medians over 13 upstrokes).

Pair 2 is louder because of the bridge, not the pick's force. The upstroke
reverses only the sideways release (the return-stroke model strums have had
since 2026-10-07): the plectrum's tip still presses the string toward the
top. At the saddle the sideways force's moment and the normal force's
moment at the string's lever arm then add on the treble strings and cancel
on the bass ones, so an isolated upstroke radiates 0.6-1.0 dB less than a
downstroke on the low E and A, the same on the D, and 2.1, 3.6 and 3.8 dB
more on the G, B and high E (first 300 ms, frets 0-12, up to +5.7 dB on one
note; `probes/UpDown.cpp`). Whether a real plectrum's upstroke differs this
much is the question this pair puts to the ear: nothing measured here
settles it, and switching `alternatePicking` off restores the downstrokes.

### Listening

The user listened to the pack blind, on a page that kept the key away, and
then judged the candidate better: an overall verdict, not one per pair.
The builder's draw, seeded with the candidate executable's hash, put the
candidate on A in pairs 1 to 4 (replaying it gives A, A, A, A, B; a fair
draw does that one time in sixteen), so the verdict cannot exclude a
preference for the first file or for A. The playback chain was not
recorded. All three changes ship on, pair 2's upstroke level included.

## CPU

Paired callback timing on one build, the switches on against off (the
probes and raw numbers are in
[`../validation/stroke-release-2026-10-13/probes`](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/stroke-release-2026-10-13/probes/results.txt);
4-CPU Intel Xeon at 2.1 GHz, cloud VM, nothing else running, not a quiet
benchmark machine):

| Case | 48 kHz / 64 frames | 96 kHz / 32 frames |
| --- | ---: | ---: |
| A played song with a pick (bass notes, strums, picked fills), median callback | +0.2% | +0.4% |
| Six fretted strings released at once, median over the 0.45 s after | +4.5% | +10.5% |
| The same release, 99th percentile | 262 µs against 280 µs | 120 µs against 121 µs |

The worst case is a whole barre chord released on one sample: six strings'
delays retuning sample by sample until the falls stop. The stroke depth and
alternate picking add a few operations per stroke. Of the release fall's
rejected forms, a slide configured every control period cost +49% and +51%
on the same release, its 99th percentile at 96 kHz/32 frames 389 µs against
a 333 µs budget; configured every 2 ms, +31% and +26%; as a delay offset
that never stopped, +16% and +15%.

## Limits

- The stroke depth is a mean line through the strings, fitted per take; the
  corpus's per-string jitter (already in the engine at its measured 4.47 dB)
  is far larger than the tilt. The offset may be partly the players' accents.
  The reach difference and the upstrokes' longer span are measured and not
  modelled.
- Alternate picking follows the strum's interval rule, not the host's beat
  grid, so two upstrokes a beat apart in a syncopated pattern come out down
  and up unless the C#0 keyswitch asks for upstrokes (README Known gaps).
- The release fall is the patent's qualitative account sized by the fret
  geometry, not a measured trajectory; its one-semitone asymptote and the
  quarter-T60 time constant are authored inside the patent's description.
  During the fall the dispersion and bridge compensation are the fretted
  note's.
- One listener's overall preference, with the candidate always on A, is
  the only listening evidence; the measurements describe what changed,
  not whether it sounds better.
