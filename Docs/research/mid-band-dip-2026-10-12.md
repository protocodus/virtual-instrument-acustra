# The mid-band dip, measured at Room 0.5 — 2026-10-12

Scope: the summed microphone pressure of every construction takes a dip
at its own mid-band peak, the strong body mode between 404 and 614 Hz
that put one note of the E2-E5 sweep 8-11 dB over its neighbours (5.4 on
the Parlor). This
follows the air-mode dip of 2026-10-11 ([the report](studio-smoothness-2026-10-11.md))
and the 2026-10-12 decision entry that measured the peak and hole and
left them. The user directed on 2026-10-12 that this candidate be built
and shipped; nobody has listened to it (see [Hearing it](#hearing-it)).

## What the sweep showed

`Tools/MeasureNoteEvenness.py` (velocity 96, Finger, stereo mic, at the
plug-in's Room 0.5, one engine per note, 1.2 s held, the sustained level
0.08-1.15 s after the attack against the mean of up to three neighbours
each side) put the largest deviation left on every Shape in the body's
mid modes: a peak with a hole one to three semitones above it. Before
this change: Dreadnought A#4 +11.4 dB and C#5 -12.4 (RMS deviation 4.20
dB over E2-E5); Auditorium C5 +8.4 and D#5 -10.3 (3.51); Jumbo G#4 +10.5
and B4 -10.6 (3.81); Parlor D#5 +5.4 and E5 -10.4 (3.35). The real
flat-top figures in README Known gaps (the Eastman E1D takes, 1.8 dB RMS
fingered and 2.2 picked) count each note in its own neighbourhood's mean,
over its first second, as `Tests/BodyShapeTests.cpp` does; this tool
leaves the note out. Counting the note from the logged levels (rounded to
0.1 dB; the window and Room stay this sweep's), these sweeps read 3.55,
2.89, 3.26 and 2.75 dB before the dip and 2.95, 2.63, 2.75 and 2.26 with
it, against the tool's 4.20, 3.51, 3.81 and 3.35, and 3.52, 3.20, 3.22 and
2.71: compare those with the recordings.

## Where the pair is in the engine's own response

The summed microphone response to a unit bridge force (both microphones'
force residues, `2 Re[R / (1 - p z^-1)]` per mode, before the capture
filter) carries on every construction, between 450 and 800 Hz, an
antiresonance 36-40 dB under its mid-band peaks, where the measured g21
bank's same-sign residues cancel (the C#5 hole of
`Tests/BodyShapeTests`), and about three and a half semitones below it
(3.50-3.62) the upper of the body's two strong mid modes. The
Dreadnought, Auditorium and Jumbo also have a lower mid mode (320-416 Hz)
as tall in the pressure response; on the sweep that note plays only 2-3
dB over its neighbours (Dreadnought F#4 +2.3, Jumbo E4 +3.0, Auditorium
G#4 +3.0), while the upper mode's note is the +8 to +11 dB peak. The
Parlor's lower mode (461-486 Hz) is as tall and its nearest note, A#4,
plays +5.2 dB over, as far as the upper mode's D#5 (+5.4); the dip takes
the upper one, and A#4 stays. So the hole finds the peak, not the
response's tallest point:
`AcustraEngine::locateMidBand` walks the response at 1/96 octave at
prepare, takes the band's deepest point as the hole and the highest point
within a fourth below it as the peak, for all twelve Shape and Wood cells,
and the capture filter dips the peak. Shape, Wood and the calibration
move both together. The walk takes no band-edge point for a hole, and
under 44.1 kHz the pair is the one a 48 kHz build of the same
construction places: there the bank drops modes above 0.46 fs whose
cancellation forms the hole, and at 8-10 kHz the hole had moved to
another mode and taken the dip with it (Dreadnought Spruce 660 Hz rather
than 460). Every rate from 8 to 192 kHz now dips the same peak.

| Shape | Spruce peak / hole (Hz) | Mahogany | Maple |
| --- | --- | --- | --- |
| Parlor | 605 / 741 | 584 / 714 | 614 / 751 |
| Auditorium | 516 / 632 | 498 / 609 | 524 / 646 |
| Dreadnought | 460 / 563 | 444 / 543 | 467 / 575 |
| Jumbo | 419 / 516 | 404 / 494 | 428 / 524 |

The dip is -9 dB at Q 8 on the summed pressure, after the balanced
observation blend and the air-mode dip, the eighth section of the capture
filter (`MicrophoneBalanceData.h`); the piezo never passes through it. It
carries a level restore of +0.5263 dB sized on the default cell's
calibration phrase, which the dip took from -27.7532 to -28.2795 LUFS
(below); every construction is re-levelled to the restored default by
`Tools/CalibrateConstructionLoudness.py` afterwards.

## Measured

The same sweep, before and with the dip, and with the boost at the hole
that was measured and not shipped (next section). `dev` in dB over the
mean of the neighbours; the last column is the fundamental's share of the
sustained power (+/-1.5 % around f0, dB re the 20 Hz-10 kHz total), which
says how much of a note a dip or boost at its fundamental can reach. The E2-E5 RMS deviation
falls from 4.20 to 3.52 dB on the Dreadnought, 3.51 to 3.20 on the
Auditorium, 3.81 to 3.22 on the Jumbo and 3.35 to 2.71 on the Parlor; the
holes move 1-3 dB nearer their neighbours because the neighbours fell.
The logs are in [`mid-band-dip-2026-10-12/`](mid-band-dip-2026-10-12)
(`before-*.log`, `dip-*.log`, `dip-and-boost-*.log`); `dip-*.log` is the
shipped engine with the level restore below, `dip-and-boost-*.log` the
boost candidate before it, so their `rms` columns are offset by about
0.15-0.4 dB (the restore as each construction's re-levelling passed it
on) and only the `dev` columns compare directly.

### Dreadnought

- before: rms deviation 4.20 dB; worst C#5 -12.4 dB; deepest hole C#5 -12.4 dB; highest peak A#4 +11.4 dB
- dip: rms deviation 3.52 dB; worst C#5 -11.0 dB; deepest hole C#5 -11.0 dB; highest peak E5 +6.1 dB
- dip and boost (not shipped): rms deviation 3.80 dB; worst C#5 -11.5 dB; deepest hole C#5 -11.5 dB; highest peak D5 +9.4 dB

| Note | Hz | Before | Dip | Dip and boost | Fundamental share before / dip (dB) |
| --- | ---: | ---: | ---: | ---: | --- |
| E4 | 329.6 | +0.2 | +0.3 | +0.4 | -4.5 / -4.6 |
| F4 | 349.2 | +1.3 | +1.4 | +0.9 | -0.3 / -0.3 |
| F#4 | 370.0 | +2.3 | +2.7 | +2.6 | -0.4 / -0.4 |
| G4 | 392.0 | -2.1 | -0.0 | -0.2 | -6.5 / -7.5 |
| G#4 | 415.3 | +0.5 | +1.7 | +1.6 | -3.0 / -4.5 |
| A4 | 440.0 | -1.2 | -3.6 | -3.6 | -1.0 / -2.9 |
| A#4 | 466.2 | +11.4 | +5.0 | +5.3 | -0.2 / -1.3 |
| B4 | 493.9 | -1.0 | +0.8 | -0.3 | -7.7 / -11.1 |
| C5 | 523.3 | -7.8 | -5.9 | -6.8 | -10.4 / -12.1 |
| C#5 | 554.4 | -12.4 | -11.0 | -11.5 | -13.7 / -14.6 |
| D5 | 587.3 | +5.3 | +4.9 | +9.4 | -0.4 / -0.4 |
| D#5 | 622.3 | +5.7 | +5.9 | +4.6 | -6.3 / -6.6 |
| E5 | 659.3 | +6.2 | +6.1 | +5.2 | -0.1 / -0.1 |

### Auditorium

- before: rms deviation 3.51 dB; worst D#5 -10.3 dB; deepest hole D#5 -10.3 dB; highest peak E5 +8.4 dB
- dip: rms deviation 3.20 dB; worst E5 +8.4 dB; deepest hole D#5 -8.4 dB; highest peak E5 +8.4 dB
- dip and boost (not shipped): rms deviation 3.66 dB; worst E5 +12.8 dB; deepest hole D#5 -9.0 dB; highest peak E5 +12.8 dB

| Note | Hz | Before | Dip | Dip and boost | Fundamental share before / dip (dB) |
| --- | ---: | ---: | ---: | ---: | --- |
| E4 | 329.6 | +0.5 | +0.5 | +3.9 | -4.4 / -4.2 |
| F4 | 349.2 | -1.7 | -1.6 | -2.0 | -1.1 / -1.1 |
| F#4 | 370.0 | +1.0 | +1.0 | +0.8 | -2.6 / -2.7 |
| G4 | 392.0 | +2.1 | +2.3 | +1.5 | -0.3 / -0.3 |
| G#4 | 415.3 | +3.0 | +3.5 | +3.4 | -0.8 / -0.9 |
| A4 | 440.0 | -2.5 | -0.8 | -0.8 | -6.2 / -7.1 |
| A#4 | 466.2 | +0.8 | +1.1 | +1.1 | -1.2 / -2.0 |
| B4 | 493.9 | -2.5 | -4.5 | -4.7 | -1.1 / -3.4 |
| C5 | 523.3 | +8.4 | +3.8 | +3.7 | -0.6 / -3.2 |
| C#5 | 554.4 | +1.4 | +2.3 | +1.3 | -3.9 / -6.6 |
| D5 | 587.3 | -6.7 | -5.0 | -5.4 | -5.0 / -6.3 |
| D#5 | 622.3 | -10.3 | -8.4 | -9.0 | -11.1 / -12.0 |
| E5 | 659.3 | +8.4 | +8.4 | +12.8 | -0.1 / -0.1 |

### Jumbo

- before: rms deviation 3.81 dB; worst B4 -10.6 dB; deepest hole B4 -10.6 dB; highest peak G#4 +10.5 dB
- dip: rms deviation 3.22 dB; worst B4 -9.3 dB; deepest hole B4 -9.3 dB; highest peak D5 +8.6 dB
- dip and boost (not shipped): rms deviation 3.19 dB; worst B4 -9.5 dB; deepest hole B4 -9.5 dB; highest peak D5 +7.4 dB

| Note | Hz | Before | Dip | Dip and boost | Fundamental share before / dip (dB) |
| --- | ---: | ---: | ---: | ---: | --- |
| E4 | 329.6 | +3.0 | +3.2 | +3.1 | -0.2 / -0.2 |
| F4 | 349.2 | -7.2 | -5.9 | -5.9 | -3.4 / -3.9 |
| F#4 | 370.0 | -2.1 | -0.9 | -0.9 | -3.1 / -4.1 |
| G4 | 392.0 | +3.3 | +2.1 | +2.2 | -1.2 / -2.7 |
| G#4 | 415.3 | +10.5 | +5.7 | +5.6 | -0.7 / -3.6 |
| A4 | 440.0 | +2.1 | +1.7 | +0.7 | -2.6 / -5.8 |
| A#4 | 466.2 | -5.2 | -3.4 | -4.9 | -8.7 / -10.8 |
| B4 | 493.9 | -10.6 | -9.3 | -9.5 | -6.9 / -7.9 |
| C5 | 523.3 | -2.8 | -2.8 | +2.2 | -1.7 / -2.0 |
| C#5 | 554.4 | +1.1 | +1.0 | +1.9 | -2.1 / -2.2 |
| D5 | 587.3 | +8.6 | +8.6 | +7.4 | -3.7 / -3.9 |
| D#5 | 622.3 | +0.1 | +0.3 | -1.8 | -8.9 / -9.1 |
| E5 | 659.3 | -0.9 | -0.8 | -1.7 | -6.2 / -6.3 |

### Parlor

- before: rms deviation 3.35 dB; worst E5 -10.4 dB; deepest hole E5 -10.4 dB; highest peak A#3 +5.6 dB
- dip: rms deviation 2.71 dB; worst E5 -7.7 dB; deepest hole E5 -7.7 dB; highest peak A#3 +5.8 dB
- dip and boost (not shipped): rms deviation 2.67 dB; worst E5 -7.2 dB; deepest hole E5 -7.2 dB; highest peak A#3 +5.7 dB

| Note | Hz | Before | Dip | Dip and boost | Fundamental share before / dip (dB) |
| --- | ---: | ---: | ---: | ---: | --- |
| E4 | 329.6 | -3.7 | -2.7 | -3.2 | -2.8 / -2.6 |
| F4 | 349.2 | -0.8 | +0.1 | -0.2 | -1.7 / -1.7 |
| F#4 | 370.0 | -2.0 | -1.5 | -1.1 | -0.8 / -0.7 |
| G4 | 392.0 | +0.9 | +0.9 | +2.6 | -2.5 / -2.3 |
| G#4 | 415.3 | -0.1 | -0.0 | -0.1 | -1.6 / -1.5 |
| A4 | 440.0 | +0.3 | +0.3 | -0.2 | -0.7 / -0.7 |
| A#4 | 466.2 | +5.2 | +5.3 | +4.8 | -0.0 / -0.0 |
| B4 | 493.9 | -4.2 | -3.0 | -3.0 | -5.3 / -5.8 |
| C5 | 523.3 | -3.0 | -1.0 | -1.1 | -5.9 / -6.9 |
| C#5 | 554.4 | +1.8 | +1.6 | +1.5 | -0.7 / -1.3 |
| D5 | 587.3 | +4.5 | +1.4 | +1.3 | -0.8 / -3.1 |
| D#5 | 622.3 | +5.4 | +2.2 | +2.3 | -0.4 / -2.0 |
| E5 | 659.3 | -10.4 | -7.7 | -7.2 | -1.9 / -3.2 |

## Over the full range, and at the default piezo mix

The sweeps above stop at E5, so E5's neighbourhood is one-sided and the
notes above the dipped mode went unmeasured. The same sweep over the
instrument's whole range, E2-C6 (`--last 84`), on the four Spruce Shapes,
in four variants: main before the dip (c4596b4) at Room 0.5, this engine
at Room 0.5, this engine dry (Room 0), and the plug-in's default sound,
Room 0.5 with Piezo Mix 0.2 (`AcustraPerformanceRenderer --piezo-mix`,
added for this). The logs are in
[`mid-band-dip-2026-10-12/e2-c6/`](mid-band-dip-2026-10-12/e2-c6).

| Shape | Before, Room 0.5 | Dip, Room 0.5 | Dip, dry | Default (Room 0.5, Piezo Mix 0.2) |
| --- | ---: | ---: | ---: | ---: |
| Dreadnought | 3.93 | 3.32 | 2.83 | 3.25 |
| Auditorium | 3.44 | 3.20 | 3.11 | 3.19 |
| Jumbo | 3.74 | 3.25 | 2.98 | 2.98 |
| Parlor | 3.91 | 3.42 | 2.75 | 3.90 |

RMS deviation in dB, E2-C6, the tool's formula (the note left out of its
neighbourhood mean). With both neighbourhoods the E2-E5 sweeps' E5 figures
fall away: the Auditorium's E5 reads +4.1 (+8.4 at the sweep's edge) and
the Dreadnought's +4.0 (+6.1). The Auditorium's D#5 hole reads -10.3, as
before. Above E5 the dip changes nothing: the largest notes left are the
Jumbo's D5 (+8.1, the strong mode just above its hole), the Auditorium's
A#3 (+7.0), the Parlor's C6 (+6.8), G5 (+6.6) and A#5 (-7.8), all present
before the dip too.

The default piezo deepens two holes. Piezo Mix adds the under-saddle
piezo coherently beneath the microphones, and at the microphones'
antiresonance the two can meet in opposite phase. On the Parlor, F#5 (740
Hz, the Parlor's located hole) falls from -7.5 dB to -14.3 dB against its
neighbours, its level 5.4 dB lower with the piezo added than without it;
on the Jumbo, C5 (523 Hz, beside its 516 Hz hole) from -2.8 to -8.5, 3.6
dB lower. On the Dreadnought and Auditorium the piezo fills the hole
instead (C#5 -11.0 to -8.9, D#5 -10.3 to -9.1), and every other note it
reaches gains level. The Parlor's default sound is therefore the least
even of the four (3.90 dB RMS against 3.42 with the microphones alone).
This is the 2026-10-12 default ("a bit of piezo"), chosen without a
listening verdict; whether the deeper Parlor F#5 is heard as a fault, and
whether the piezo's polarity or level should follow the Shape, is the
owner's to judge by listening. Nothing is changed for it here.

## The boost at the hole, measured and not shipped

The candidate as designed also lifted the hole, +8 dB at Q 8 at the
located antiresonance. Measured (the `Dip and boost` column) against the
dip alone at the same levelling, it raised each hole's note by about 1 dB
(0.8-1.4 dB in `rms`) but lifted the note just above the hole by 4.4-5.0
dB against its neighbours (Dreadnought D5 +4.9 to +9.4, Auditorium E5
+8.4 to +12.8, Jumbo C5 -2.8 to +2.2) and the second partial of the note
an octave under it (Auditorium E4 +0.5 to +3.9), so each hole came out
slightly deeper against its neighbours (C#5 -11.0 to -11.5 on the
Dreadnought, D#5 -8.4 to -9.0 on the Auditorium, B4 -9.3 to -9.5 on the
Jumbo; only the Parlor's E5 rose, -7.7 to -7.2). Those holes' notes are
radiation nulls: at the sustained level their fundamental carries only
-7 to -15 dB of the note's power (the fundamental-share column: C#5 -13.7
before and -14.6 with the dip, D#5 -11.1/-12.0, B4 -6.9/-7.9), so a boost
at the fundamental reaches little of the note, and a boost strong and
narrow enough to fill a 10 dB hole would ring. The RMS deviation with the boost was 3.80, 3.66,
3.19 and 2.67 dB against the dip's 3.52, 3.20, 3.22 and 2.71. The boost
was removed; the capture filter has eight sections.

## Levelled

The dip took the default cell's calibration phrase (Dreadnought Spruce
Finger, stereo microphones) from -27.7532 to -28.2795 LUFS. Levelled to
that without a restore, the whole instrument went half a dB quieter, and
the Rack Extension's piezo patch (`Piezo Stage Steel`) rendered its open
E strum at -28.22 dBFS RMS, under its bank's -28 dBFS floor
(`Examples/Acustra/Tests/AllPatchRenderTest.cpp`). So the dip carries a
level restore of +0.5263 dB on the capture filter's gain, sized on that
phrase as the fitted contour's 1.2172 dB was, and every construction is
re-levelled to the restored default by
`Tools/CalibrateConstructionLoudness.py --write-header` (all Shapes and
Pickings, the Parlor's Finger and Thumb cells keeping their 0.5 LU lift);
`--check` holds: stereo mic -0.00..+0.50 LU, mono mic -0.00..+0.50 LU, piezo -0.92..+0.50 LU over the 36 cells, every Pick cell short of
the headroom having given up the level the tolerance allows. The mono
microphone, the right channel at its own reference, had risen 0.15 LU
against the pair on the default construction under the dip (the dipped
mode radiates more to the left microphone), from 0.85 to 1.0002 LU over
the default's stereo loudness, past the 1 LU tolerance; its default cell
is pinned at 1 in the generated tables, so the mono microphone takes
-1.0 dB (`originalMonoLevelGain`, `MicrophoneBalanceData.h`), which puts
the default's mono capture on the target the other cells are levelled to.
The construction loudness fixture (`Tests/ConstructionLoudnessTests.cpp`,
the chord-and-strum phrase, 3 LU) reads -2.85..+1.74 LU over its 51
renders, the quietest Parlor Spruce Finger (was -2.57: the dip costs the
Parlor's fixture phrase about 0.3 LU more than its calibration phrase),
the loudest Dreadnought Maple Pick, mono mic.

## Tests

Capture (with the new `testMidBandPairFollowsTheConstruction`: the
located hole is the band's deepest point and the peak the highest within
a fourth below it on an independent walk of the summed response, local
extrema, at least 6 dB apart, and the live filter centred on the peak on
all twelve cells, the same peak at every rate from 8 to 192 kHz, and
bounds at the float rounding they absorb; with the low-rate rule
disabled, it fails at 8 and 10 kHz), CaptureVoicing (the complex
prototype carries the dip and its restore, probed at the default
construction's air-mode and mid-band centres at seven rates),
MicrophoneBalance (eight slots),
BodyConfigurationCache (the cached pair matches a direct build),
BodyShape, Engine, ConstructionLoudness, ConstructionLoudnessCalibration
and the rest of the DSP suite: 88 of 88 at 44.1, 48 and 96 kHz where a
suite spans rates, plus the C++17 library build for the Rack toolchain;
the plug-in's PluginProcessor and PluginTempoRelease suites, with every
fixture as on main. (On the candidate before its level restore the
Parlor Mahogany's beating A3 tail read 1.016e-7 in
`Tests/ConstructionHistoryTests.cpp`'s 0.4 s natural-decay window, over
its 1e-7 floor; the restored engine reads 7.4-9.1e-8 in every cell's
first window, that cell 8.6e-8, as on main, so the fixture keeps its
original check.) On `Tests/BodyShapeTests`' note-to-note measure (E2-C6, 1 s RMS,
dry, steel Dreadnought) the roughness reads 2.23 dB and the deepest
one-note hole 7.41 dB against the mean of its two neighbours (bounds 3.2
and 8); see the README's Known gaps for the comparison with main.

## Hearing it

Nobody has listened. A blind pack (A/B, the key apart) was rendered for
the user with main's engine (`c4596b4`) and this engine by each build's
`AcustraPerformanceRenderer`: the strummed phrase, seven single notes
(E2, G2, A2, A3, E4, A#4, C#5) and an A4-D5 melody that crosses the mid
peak and hole, on the Dreadnought and the Auditorium, stereo mic, Finger,
Room 0.5, 24-bit WAV at one shared gain. The user directed on 2026-10-12
that the candidate ship on the measurements; this report records them,
not a listening verdict.
