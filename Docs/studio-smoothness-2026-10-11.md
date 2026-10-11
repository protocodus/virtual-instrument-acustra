# Note-to-note smoothness and boominess, measured — 2026-10-11

The user asked for a smooth, natural studio sound without unwanted resonances
or boominess. Nobody listened during this pass, so it records what the
instrument measures, which of its levers move those measurements, and what
is left for a listener to choose. No sound change ships from it. The engine
is `acdb856` (the Sustain switch and the panel knobs change no sample), the
renderer `AcustraPerformanceRenderer` from a Release GCC 13.3 build, 48 kHz.

## The sweep

`Tools/MeasureNoteEvenness.py` renders E2-E5 one note at a time on the string
that reaches each at the lowest fret, velocity 96, Finger, held 1.2 s, and
reads each note's sustained level (0.08-1.15 s, mono sum) against the mean
of up to three neighbours each side. Room 0. A real flat-top's own sweep
deviates by about 2 dB RMS (Eastman E1D, README Known gaps), so the
question is where the deviation is larger than that and why.

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

Two bounded, authored changes, each in a direction the listener had already
chosen and each sized by the sweep (Docs/decisions.md, 2026-10-11):

- **A dip at the construction's air mode** on the summed microphone pressure:
  a peak section of -4 dB, Q 2.5 (MicrophoneBalanceData.h), centred on the
  lowest configured g21 pole, so it follows Shape where the fixed 125 Hz
  refinement of 2026-10-09 (which the user confirmed as less boom) could
  not. The piezo never passes through it.
- **Damped mid plate modes**: the measured modes between 350 and 900 Hz
  keep 0.7 of the flamenca's Q, shared with their bridge twins
  (`radiationPole`), the direction of the 2026-09-27 blind verdict.

The sweep, same tool and settings, before and after (neighbourhood
deviation in dB):

| Render | RMS deviation | Deepest hole | Loudest peak | F#2 | G2 | G#2 | A2 | E2 | A#4 | C#5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Dreadnought, stereo mic | 3.64 → 3.10 | -10.4 → -9.4 (C#5) | +9.8 → +8.0 (A#4) | +4.2 → +3.2 | +4.0 → +2.4 | +1.3 → +1.1 | -2.5 → -2.2 | -5.4 → -3.5 | +9.8 → +8.0 | -10.4 → -9.4 |
| Auditorium, stereo mic | 3.64 → 3.12 | -9.8 → -8.8 (D#5) | +9.8 → +8.2 (C5) | -1.8 → -1.0 | +1.8 → +0.9 | +5.7 → +4.3 | +3.3 → +2.1 | -4.5 → -3.4 | -1.5 → -1.0 | +2.4 → +2.7 |
| Jumbo, stereo mic | 3.54 → 3.16 | -10.4 → -10.0 (B4) | +9.5 → +7.9 (G#4) | +0.8 → +0.8 | -0.5 → -0.2 | -1.2 → -1.2 | -1.0 → -0.9 | +2.2 → +1.6 | -3.5 → -3.0 | -0.4 → -0.3 |
| Parlor, stereo mic | 2.49 → 2.16 | -6.1 → -5.5 (E5) | +5.7 → +5.0 (D#5) | -1.1 → -0.6 | -2.4 → -1.6 | -1.7 → -1.4 | -0.5 → -0.5 | -1.0 → -1.1 | +3.8 → +3.4 | +0.2 → +0.3 |
| Dreadnought, mono mic | 3.50 → 3.08 | -8.3 → -8.1 (C#5) | +7.3 → +6.4 (A#4) | +3.4 → +2.1 | +4.1 → +2.5 | +1.9 → +1.7 | -2.3 → -2.0 | -5.4 → -3.0 | +7.3 → +6.4 | -8.3 → -8.1 |

The Parlor's air-mode notes, A#2 and B2, fall from +4.2/+5.5 to +3.1/+4.0.
In absolute sustained level the air-mode notes lose 2.5-3.7 dB (the
Dreadnought's F#2 -3.3, G2 -3.7; the Auditorium's G#2 -3.7, A2 -3.6) and
E2 0.8-1.2 dB, except on the Jumbo, whose air mode sits at E2/F2 (-3.0/-3.4),
where E2 was 2.2 dB over its neighbours and is now 1.6. The mean sustained
level over the sweep falls 0.7-1.0 dB per Shape before re-levelling.

Levelling: the default cell's stereo phrase (the calibration phrase set)
measured -27.3515 LUFS before and -28.1285 after, so the capture level
scalar takes 0.7770 dB back (-27.3515 again); its mono microphone moved
-0.60 LU and its piezo +0.01 LU before the construction re-levelling, which
`Tools/CalibrateConstructionLoudness.py --write-header` then applied to
every cell and `--check` verified; its measurements, before and after the
gains, are in [construction-loudness-2026-10-11.json](construction-loudness-2026-10-11.json).
Before the re-levelling the cells had moved by up to 1.7 LU on the
microphones (28 of 36 stereo cells still within the ±1 LU tolerance) and
by at most 0.9 LU on the piezo; the hardest case's peak stays under the
limiter's knee on every stereo and mono cell.

What remains: the Shape-dependent mid peak and hole are smaller, not gone
(A#4 +8.0, C#5 -9.4 on the Dreadnought); they are the single-point
near-field microphone response documented in Known gaps, and a bridge
microphone fitted as a blend of the archive's measured positions remains
the way to close them. The air-mode notes still sit 2-4 dB over their
neighbours, as the real flat-top's A#2 does.

## Hearing it

Nobody has listened. To hear the change blind, build the previous engine
(`d0da5a3`) and this one, render the sweep and the strummed phrase above
with each `AcustraPerformanceRenderer` (the tool for the sweep; the phrase
is `Tools/MeasureNoteEvenness.py`'s events format with the chords listed
in the room section), label them A and B without telling the listener
which is which, and record the verdict in Docs/decisions.md. The tool's
`summary.json` records every number in these tables.
