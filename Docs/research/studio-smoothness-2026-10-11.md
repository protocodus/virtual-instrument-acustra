# Note-to-note smoothness and boominess, measured — 2026-10-11

The user asked for a smooth, natural studio sound without unwanted resonances
or boominess. Nobody listened during this pass, so it records what the
instrument measures, which of its levers move those measurements, what was
applied from that (one bounded capture refinement, under "What was
applied") and what was left for a listener to choose; the listener's
verdict came on 2026-10-12 ("Hearing it"). The survey below is
on engine `acdb856` (the Sustain switch and the panel knobs change no
sample); the applied change is measured against the merged main
(`dbd939b`, whose balanced observation blend moved the same numbers
first). The renderer is `AcustraPerformanceRenderer` from a Release GCC
13.3 build, 48 kHz.

## The sweep

`Tools/MeasureNoteEvenness.py` renders E2-E5 one note at a time on the string
that reaches each at the lowest fret, velocity 96, Finger, held 1.2 s, and
reads each note's sustained level (0.08-1.15 s, mono sum) against the mean
of up to three neighbours each side. Room 0. The real flat-top's figures
in README Known gaps (the Eastman E1D, 1.8-2.2 dB RMS) count each note in
its own neighbourhood's mean, which this tool does not, so the tool reads
the same levels about a fifth rougher (17-22 % on the sweeps of
[the mid-band dip report](mid-band-dip-2026-10-12.md)). The question is
where the deviation is larger than the recordings' and why.

| Render | RMS deviation (dB) | Deepest hole | Loudest peak | F#2 | G2 | E2 | A#4 | C#5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Dreadnought, stereo mic | 3.64 | -10.4 (C#5) | +9.8 (A#4) | +4.2 | +4.0 | -5.4 | +9.8 | -10.4 |
| Auditorium, stereo mic | 3.64 | -9.8 (D#5) | +9.8 (C5) | -1.8 | +1.8 | -4.5 | -1.5 | +2.4 |
| Jumbo, stereo mic | 3.54 | -10.4 (B4) | +9.5 (G#4) | +0.8 | -0.5 | +2.2 | -3.5 | -0.4 |
| Parlor, stereo mic | 2.49 | -6.1 (E5) | +5.7 (D#5) | -1.1 | -2.4 | -1.0 | +3.8 | +0.2 |
| Dreadnought, mono mic | 3.50 | -8.3 (C#5) | +7.3 (A#4) | +3.4 | +4.1 | -5.4 | +7.3 | -8.3 |

Three features repeat across the Spruce constructions:

- **A low peak at the air mode.** On the Dreadnought F#2 and G2 sit 4 dB over
  their neighbours with 75-90% of their sustained power in the fundamental
  (`fund` -1.2 and -0.4 dB) and decay fast (-21 and -17 dB over the window
  against -8 to -12 elsewhere): the string's fundamental drives the air mode
  and radiates through it. The Auditorium's air mode is higher, so its peak
  is G#2 (+5.7) and A2 (+3.3), the A2 emphasis the user already named as
  boom on 2026-10-09. E2, under the air mode, sits 5 dB under its neighbours
  on both.
- **A mid peak and a hole from the body's 400-600 Hz modes.** A#4 on the
  Dreadnought is 9.8 dB over its neighbours (fundamental 466 Hz, 90% of the
  power, a 482 Hz Q 29 mode beside it); C5 and C#5 fall 6.6 and 10.4 dB into
  the same-sign modal pair's antiresonance under the bridge microphone
  (README Known gaps). Each Shape moves the pair: C5 +9.8 with D5/D#5 -9 on
  the Auditorium, G#4 +9.5 with B4 -10.4 on the Jumbo.
- **The Parlor is the smoothest** at 2.5 dB RMS, within reach of the real
  flat-top's 2 dB.

## What the levers do

Each variant is the same source and renderer with one calibration line in a
`--calibration` file (Tools/CalibrationFile.h), on the Dreadnought's stereo
microphones.

| Variant | RMS deviation (dB) | F#2 | G2 | E2 | A#4 | C#5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| shipping | 3.64 | +4.2 | +4.0 | -5.4 | +9.8 | -10.4 |
| `bodyQScale 0.7` | 3.22 | +3.4 | +3.4 | -4.9 | +8.4 | -9.9 |
| `bodyQScale 0.5` | 2.70 | +2.6 | +2.8 | -4.3 | +6.7 | -8.6 |
| `lowBodyModeGain 2.83` (+9 dB in place of +12) | 3.60 | +3.9 | +3.6 | -4.9 | +9.8 | -10.4 |

- **The air-mode gain is not the low peak's lever.** Taking 3 dB off it moves
  F#2 and G2 by 0.3-0.4 dB and costs E2 0.5 dB of level and 1.8 dB of its
  fundamental against its second partial (H2/H1 +1.0 to +2.8 dB), the ratio
  the gain was chosen by ear to match on the Eastman's E2 (2026-09-25). The
  peak is the measured body's own A0 radiation at the upper-bout
  microphone, which the gain does not reach.
- **Halving every body Q evens the sweep from 3.6 to 2.7 dB** but is a
  tonal change over the whole body (every mode's ring shortens by half), and
  the antiresonance hole it cannot fill stays at -8.6 dB. The mode Qs were
  set against the anechoic flamencas' and kept by a blind verdict
  (2026-09-27), so this is a listener's choice, not a measured correction.

## The room adds no bass

A strummed phrase (E A D G E, open chords, 100 BPM, Finger, velocity 96,
released before each strum, then a 1.6 s tail) was rendered at Room 0, 0.5
and 1, and its third-octave long-term spectrum compared. The room adds
+1.7 and +5.0 dB at 200 Hz and +1.9/+5.3 dB at 504 Hz (0.5/1) and takes
0.3-1.4 dB off every band below 160 Hz. After the last release the 60-150 Hz
band falls 6 dB per 100 ms at every Room setting. At the 50% default the
room is not where boominess would come from.

## Against the real flat-top

The embedded bank's eight finger-plucked flat-top notes (E2, A#2, E3, A#3,
E4, B4, F5, B5, velocity 91), rendered through `AcustraPhysicalFitRenderer`
with the shipping calibration and compared per note (sustain 0.08-1.0 s
after each detected onset, mono sum):

- the real notes' own level profile deviates more than the model's (4.5
  against 3.0 dB RMS over the eight), with the real A#2 5.4 dB louder than
  the real E2 where the model's A#2 sits 0.8 dB under its E2: a loud note
  near the air mode is what the real guitar does too;
- on the low notes the bass share agrees within 0.5 dB (E2 -0.5 real against
  -0.2, A#2 -1.5 against -1.6, E3 -3.3 against -2.8), so the model's low
  notes are not bass-heavier than the recording's;
- the model's attacks stand 3-7 dB less over their sustain than the real
  ones (the snap-then-mellow gap in README Known gaps), and above A#3 its
  second partial sits 3-13 dB higher against the fundamental.

The archtop training and validation rows were not usable for this: their
targets' spectral peaks sit hundreds of cents from the nominal pitches (the
legacy sampler zones cover key ranges), so a per-partial comparison reads
the wrong partials.

## What was applied

One bounded, authored change, in the direction the user confirmed as less
boom on 2026-10-09 and sized by the sweep (Docs/decisions.md, 2026-10-11):
a dip at the construction's air mode on the summed microphone pressure, a
peak section of -2.5 dB, Q 2.5 (MicrophoneBalanceData.h) centred on g21's
lowest mode as each Shape and Wood places it, so it follows the
construction where the fixed 125 Hz refinement could not. The piezo never
passes through it. It sits after the balanced microphone observation blend
the user approved the same day (the parallel work merged with this
branch), which had not removed the sweep's air-mode excess: over the blend
the Dreadnought's G2 still stood +3.1 dB and the Auditorium's G#2 +5.5.

Every construction whose air mode sits under 117 Hz takes it: the Jumbo's
at 84-91 Hz, the Dreadnought's at 94-101 and the Auditorium's at 106-113
as Spruce, Mahogany and Maple place it. The Parlor's, at 121-128 Hz, lifts
A#2/B2 as much (+3.5/+5.3 dB) but keeps it: its margin on the cross-phrase
construction loudness fixture (Parlor Spruce Finger at 2.87 of the 3 LU
allowed) has no room for the 0.2 LU more the dip costs that fixture's
phrase than the calibration phrase, and a tolerance is not loosened to fit
a refinement. A -4 dB dip with a level restore sized on the calibration
phrase was tried first and put the Parlor past that tolerance; the -2.5 dB
dip takes no restore.

A second candidate, 0.7 of the flamenca's Q on the plate modes between 350
and 900 Hz, measured well in isolation (A#4 +9.8 to +8.0 on the previous
engine) but was dropped at the merge: the user's model and parameter audit
of the same day pins the radiation to each mode's measured damping (432
GuitarModels expectations), and the approved blend states that no physical
damping change is part of it. The mid peak and hole therefore stay as
measured.

The sweep, same tool and settings, the merged main before and after the dip
(neighbourhood deviation in dB; the deepest hole and loudest peak do not
move):

| Render | RMS deviation | Deepest hole | Loudest peak | F#2 | G2 | G#2 | A2 | A#2 | B2 | E2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Dreadnought, stereo mic | 3.71 → 3.63 | -11.5 (C#5) | +9.9 (A#4) | +4.3 → +3.7 | +3.1 → +2.0 | +1.2 → +1.0 | -2.2 → -2.0 | -1.9 → -1.6 | -1.5 → -1.3 | -4.3 → -3.1 |
| Auditorium, stereo mic | 3.65 → 3.57 | -11.1 (D#5) | +10.0 (C5) | -1.8 → -1.2 | +0.3 → -0.2 | +5.5 → +4.6 | +2.7 → +1.9 | +0.2 → +0.3 | -1.9 → -1.9 | -3.2 → -2.6 |
| Auditorium Maple, stereo mic | 4.19 → 4.13 | -10.8 (D#5) | +11.7 (C5) | -2.7 → -1.9 | -3.1 → -2.8 | +0.7 → +0.6 | +3.8 → +2.8 | +5.1 → +4.3 | +0.2 → -0.2 | -0.5 → -0.4 |
| Jumbo, stereo mic | 3.48 → 3.45 | -9.3 (B4) | +9.9 (G#4) | +0.9 → +0.9 | -0.6 → -0.4 | -1.2 → -1.2 | -1.0 → -0.7 | -2.3 → -2.2 | -1.7 → -1.6 | +2.6 → +2.3 |
| Parlor, stereo mic | 2.64 | -7.8 (E5) | +5.3 (B2) | -0.3 | -2.0 | -2.7 | -1.1 | +3.5 | +5.3 | -1.0 |
| Dreadnought, mono mic | 3.48 → 3.38 | -8.4 (C#5) | +9.0 (A#4) | +3.4 → +2.6 | +3.3 → +2.2 | +1.7 → +1.5 | -2.1 → -1.9 | -1.8 → -1.5 | -0.9 → -0.8 | -4.1 → -2.7 |

In absolute sustained level the air-mode notes lose 1.7-2.4 dB (the
Dreadnought's F#2 -1.8, G2 -2.3; the Auditorium's G#2 -2.1, A2 -2.2, its
Maple box's A2/A#2 -2.4; the Jumbo's E2/F2 -1.7/-2.0, its air mode sitting
there) and E2 0.4-0.6 dB on the Dreadnought and Auditorium. The air-mode
notes still sit 2-4.6 dB over their neighbours, as the real flat-top's A#2
does ("Against the real flat-top").

Levelling: the default cell's stereo phrase (the calibration phrase set)
measured -27.5164 LUFS on main and -27.7532 with the dip, 0.24 LU inside
the construction loudness contract, and no scalar restores it. The dipped
constructions were re-levelled to the default by
`Tools/CalibrateConstructionLoudness.py --write-header --shapes auditorium
dreadnought jumbo` (the new `--shapes` keeps the other Shapes' gains), the
Parlor's Finger and Thumb cells keep main's gains (0.2-0.3 LU over the
lowered target) and its Pick cells follow the headroom rule
(`--shapes parlor --pickings pick`); `--check` verifies every cell and
capture: the stereo microphone within -0.00/+0.28 LU of the target on all
36 cells, the mono microphone within -0.00/+0.85 and the piezo within
-0.90/+0.31 (the loudest the Parlor's un-dipped Finger and Thumb cells),
the hardest Pick case 2.3 dB under the limiter's knee on the stereo
microphones and 1.3 dB on the piezo. Its measurements, as rendered with
the final gains and without them, are in
[construction-loudness-2026-10-11.json](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/research/construction-loudness-2026-10-11.json).

## The other techniques, and the third microphone

With the change, the Dreadnought's sweep on the stereo microphones reads
2.19 dB RMS with the Pick (worst D#5 +4.8, deepest hole C5 -4.2) and
3.85 dB with the Thumb (A#4 +10.7, C#5 -11.9): the Pick's brighter, less
fundamental-heavy notes sit closer to a real guitar's 2 dB already.

The measured bank holds a third position, the bass-bridge microphone,
which the stereo pair never uses. Its force-radiation transfer, computed
from the modal data alone at the Dreadnought anchor, shares the same nulls
(C#5 -5.9 dB and D#5 -8.5 against the treble-bridge microphone's -7.6 and
-12.5, relative to each position's mean over G4-E5), so blending it would
not fill the holes; the mid peak and hole are the single-point near-field
response at every measured position, and only a microphone response
measured further from the top would remove them.

## Audio quality probes

The strummed phrase at Room 0.5, velocity 96 and 127, rendered with the
shipped engine at 44.1, 48 and 96 kHz (mono sum, 65536-point Welch
spectrum): no DC (the mean under -129 dBFS), the share of the power under
20 Hz at -47.5 to -48.3 dB, the tail two seconds after the last release at
-107 to -112 dBFS, no sample within 1 dB of full scale (peaks -8.7 to
-14.1 dBFS, so the safety limiter is idle), and across the three rates the
10-16 kHz share within 2.6 dB and the 16-20 kHz share within 4.2 dB (the
higher rates' lower shares; 96 kHz adds nothing above 20 kHz that the
lower rates fold back).

## Against the recordings

The project's descriptor loss (`Tools/FitPhysicalModel.py --compare`, the
matched observation) on `AcustraPhysicalFitRenderer`'s embedded-bank sets,
the merged main (`dbd939b`) against this engine, same calibration and
targets:

| Set | Rows | Before | After | Change | Terms that moved |
| --- | ---: | ---: | ---: | ---: | --- |
| Flat-top, Finger | 8 | 7.1444 | 7.1844 | +0.56% | attack +0.10, harmonics +0.05, body +0.04 |
| Archtop training, Pick | 54 | 8.8686 | 8.8099 | -0.66% | attack -0.16, body -0.16, harmonics -0.04, dynamics +0.05 |
| Archtop validation, Pick | 14 | 7.4836 | 7.4145 | -0.92% | attack -0.17, body -0.17, harmonics -0.06 |

Two of the three sets read closer to their recordings, the finger-plucked
flat-top slightly further on its attack term. All three moves are within
about one percent, the size the project has treated as a tradeoff rather
than a verdict before (Docs/research/recording-realism-2026-10-07.md); a descriptor
loss establishes closer measured features, not a listener's preference.

## Hearing it

Heard on 2026-10-12. The blind pack was rendered with main's engine
(`dbd939b`) as A and this engine (`8cfcec6`) as B by each build's
`AcustraPerformanceRenderer`: the strummed phrase from the room section,
seven single notes (E2, G2, A2, A3, E4, A#4, C#5) and an A4-D5 melody that
crosses the mid peak and hole, each on the Dreadnought and the Auditorium,
stereo mic, Finger, Room 0.5, 24-bit WAV at one shared gain, the key in a
separate file. The user's verdict, given before reading the key, was "B is
better"; no piece or Shape was singled out. Docs/decisions.md (2026-10-12)
records it. The tool's `summary.json` records every number in the tables
above.
