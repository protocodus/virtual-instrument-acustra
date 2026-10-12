# The default Piezo Mix against the microphones — 2026-10-12

The default Piezo Mix (20 %) deepens two notes: the Parlor Spruce's F#5
(-7.5 to -14.3 dB against its neighbours) and the Jumbo Spruce's C5 (-2.8 to
-8.5) on the E2-C6 sweep at Room 0.5. This study asked whether the blend
itself should change: invert the piezo, or shift it in time against the
microphones. It should not. No variant fixes those notes without moving the
cancellation elsewhere, and the shipped blend stays as it is. Nothing in the
sound changes. Nobody has listened to any variant; every figure below is a
signal measurement.

## Frozen inputs

Source `8ea51f9` (clean). `AcustraPerformanceRenderer` built from it, Release
`-O3`, GCC 13.3, SHA-256 `d31f7bb1…` (full hashes of the renderer, engine and
scorers in [`provenance.txt`](piezo-blend-2026-10-12/provenance.txt)). The
scorer is `Tools/MeasureNoteEvenness.py`'s own functions (per-note sustained
level, deviation against neighbours, RMS deviation). Strums come from
`Tools/MeasureMaterialLoudness.py` (`strums()`, BS.1770 loudness). Unless
stated: stereo microphones, Finger, velocity 96, Room 0.5, 48 kHz, all 12
constructions (four Shapes by three Woods), E2-C6. Scripts and text results
are in [`piezo-blend-2026-10-12/`](piezo-blend-2026-10-12/); the raw renders
are not kept.

## Method: superposition

The engine adds the Piezo Mix term after the microphone path has been
delayed into alignment with the piezo pipeline, and Capture Piezo outputs
that same term alone. So Main at Piezo Mix *m* equals Main at 0 plus *m*
times Capture Piezo. Checked sample by sample on 540 single notes, 540 Pick
notes, the per-string set, 44.1 and 96 kHz sweeps and strums up to velocity
127: the largest difference is 3.0e-8, under one float32 step, and no render
reached the safety limiter. Every variant was then evaluated offline from
the same renders. The variants: V0 microphones alone; V1 the shipped blend;
V2 the piezo inverted; V3 the piezo shifted -1 to +1 ms in 0.125 ms steps in
either polarity.

## Why the two notes cancel

At each note's fundamental, the microphones' level and phase against the
piezo (transfer files in `transfer/`, [`string_phase.txt`](piezo-blend-2026-10-12/string_phase.txt)):

- **Parlor Spruce F#5, top string.** The microphones are only 0.8 dB above
  0.2 times the piezo and 179° from it, so the blend takes 21 dB off the
  fundamental and 5.3 dB off the note. Its neighbours on the same string sit
  at 15-100°.
- **Jumbo Spruce.** A#4 and B4 sit in the microphones' hole, 6-7 dB under
  the piezo and nearly in phase, so the blend fills them (+10 dB on the
  fundamental). C5 is at 158° and loses 7.8 dB.
- **The phase depends on the string.** The same F#5 on the fifth string is
  -18°, and the blend adds 4.6 dB. The microphones are driven by the bridge's
  force and its rocking moment, the piezo by the saddle force alone, and the
  strings' lever arms have opposite signs on the bass and treble sides. Where
  the force part of the microphone response cancels, the moment part sets
  the phase, string by string.

## Results

Mean RMS deviation over the 12 constructions, in dB, and how many
constructions each variant makes better or worse than the shipped blend by
more than 0.05 dB ([`summary_table.txt`](piezo-blend-2026-10-12/summary_table.txt),
[`strings.txt`](piezo-blend-2026-10-12/strings.txt)):

| Variant | E2-C6 sweep | Per string | Pick | Parlor Spruce F#5 | Jumbo Spruce C5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| V0, microphones alone | 3.42 | 3.30 | 2.42 | -7.5 | -2.8 |
| V1, shipped (+0.2) | 3.30 | 3.19 | 2.39 | -14.3 | -8.5 |
| V2, inverted | 3.29 (7/5) | 3.27 (4/6) | 2.38 (5/6) | -1.8 | +1.6 |
| V3c, piezo 0.75 ms earlier | 3.01 (7/3) | 2.95 (11/1) | 2.29 (8/3) | -2.9 | -0.1 |
| V3d, inverted, 0.625 ms later | 3.11 (12/0) | 2.90 (12/0) | 2.30 (8/1) | -12.6 | -4.0 |

- **V2, inverted, moves the holes.** It digs new ones: Parlor Spruce E5
  -10.2, Dreadnought Mahogany C6 -10.0, Jumbo Spruce B4 -10.0, Auditorium
  Maple A3 on the third string -11.5. Its sign of benefit flips with Room,
  Capture Mono and Piezo Mix: with Capture Mono it is worse on 8 of 12.
- **No single shift aligns the sensors.** The best shift per construction
  scatters over five settings in both polarities, and the best lag disagrees
  between note sets. The good common shifts help about as much as an
  uncorrelated piezo of the same power would: they act as a decorrelating
  comb, not as alignment. V3c also needs the microphones delayed by another
  0.75 ms, which adds latency to Main.
- **V3d is the most consistent on single notes but fails elsewhere.** It
  leaves Parlor F#5 at -12.6 and Jumbo C5 at -4.0. On barre chords the
  shipped blend is already the most even (V3d worse on 6 of 12 at Room 0.5,
  9 at Room 1), and at Room 1 or with Capture Mono its single-note advantage
  shrinks to 6/3 and 8/2. It works only within 0.5-0.625 ms of its chosen
  shift ([`verification/`](piezo-blend-2026-10-12/verification/)).
- **Any change reshapes the tone of every Piezo Mix setting.** Against the
  shipped blend, V2 and V3d lift the Parlor's 80 Hz third-octave by about
  11 dB and take 3-5 dB off 100-125 Hz on the larger bodies. V3d raises the
  default's loudness by up to 1.2 LU on the Jumbo, spreading the
  constructions, which are levelled at Piezo Mix 0, from 0.41 to 0.91 LU
  ([`phrases.txt`](piezo-blend-2026-10-12/phrases.txt)). Every saved session with
  a piezo would change, and so would the Rack's default patch.
- **Neither a lower default nor a filter fixes it.** Piezo Mix 0.1 halves
  both the cancellation and the fill (mean 3.38; Parlor F#5 -11.1). A
  1-2 kHz high-pass on the piezo removes the fill too and is less even
  than the microphones alone (3.50). A 90° rotation is a coin flip by note
  set ([`rotation.txt`](piezo-blend-2026-10-12/rotation.txt)).
- **The mono-sum metric exaggerates the holes.** Scored per stereo channel,
  Parlor F#5 with the shipped blend is -10.4 dB on the left and -8.5 on the
  right (-9.5 as their power mean), against -14.3 for the mono sum the sweep
  tool scores. The shipped blend is more even than the microphones alone on
  average (3.30 against 3.42), and its real cost is concentrated in the
  Parlor (3.90 against 3.42).

## Verification

Two independent agents rebuilt every analysis from their own scripts. One
reproduced every figure above exactly, at the printed precision; the only
error it found was a count, now corrected (the per-construction optima use
five settings, not eight). The other tested superposition at Room 1, on
Capture Mono and at velocity 127, scored chords, per-channel levels, attack
evenness and holdout splits, and refuted any shift as a fix. Both agreed
that no blend change meets the bar of helping every construction without
moving the cancellation.

## What would fix the notes

The holes start in the microphones: with the piezo off, Parlor Spruce F#5
is already -7.5 and Parlor Maple F#5 -15.3. If a listener hears the Parlor's
top-string F#5 as a fault, the place to work is the microphones'
top-string response at the mid-band hole (the rocking-moment term), not the
Piezo Mix. Until then the open question for the owner stands, by ear:
whether these notes matter at the default Piezo Mix.
