# Consolidation audit, 1.9.37 to 1.9.40 — 2026-11-09

A pass over the shipping model ahead of release: what was checked, what was
found, what changed and what was left. Sources 1.9.37 (`5ce09eb`) to 1.9.40;
48 kHz, Release, gcc 13.3 unless a line says otherwise, on one shared
development machine. The CPU side is in
[cpu-consolidation-2026-11-09.md](../benchmarks/cpu-consolidation-2026-11-09.md).
None of the three changes alters the sound: every factory demo renders bit
for bit as 1.9.37.

## Found and fixed

| Version | Found | Fix | Evidence |
| --- | --- | --- | --- |
| 1.9.38 | A chord kept under its keys, the pedal or the Sustain switch never reached the idle flush and ran down into subnormal numbers. Without the host's flush-to-zero a block cost 12% of its period for 30 s, 77% at 40 s and 100-170% from 90 s on, for as long as the strings were held. | The idle flush also runs with held strings, which are cleared where they are and stay held. | `testHeldChordComesToRest`; cost level at 11%. |
| 1.9.39 | After the flush each string's level observer decayed on, was subnormal half a second later and stayed at 1.4e-45 while the host kept the engine awake: 1.3% of a block, for good. | The flush zeroes every string's observer, as a suspension already did. | `testQuietStatesStayOutOfSubnormals`. |
| 1.9.40 | A note on a string still sounding copied and cleared 256 KB of an arrival queue that is nearly always empty. | Only the pending arrivals are moved. | First-time chord block 24.4 M to 22.8 M instructions; all hashes equal. |

Reason is not known to set flush-to-zero for a Rack Extension, the adapter
sets nothing, and its own sleep waits for every string to be let go, so the
first two are treated as reachable there. The plug-in sets
`juce::ScopedNoDenormals` and was not affected.

## Every note, three velocities, eight presets

`Tools/MeasureFretboardSweep.py`, the eight preset-derived configurations,
six strings, frets 0-20, velocities 32, 80 and 120 (3024 notes), exact
intonation, Room 0. The engine's output is unchanged since 1.9.37, so this
is one survey, not a before and an after.

| Preset | Settled pitch beyond 6 cents (of 126 notes) | Sustain 3 dB off its neighbours | of those, shared by the same pitch on another string | Attack, velocity 120 less 32: median (sd) | Fundamental T60, median (10th-90th) | Attack level per octave up a string |
| --- | ---: | ---: | ---: | --- | --- | --- |
| Dreadnought Steel | 4 | 26 | 22 | 18.1 dB (0.80) | 5.7 s (3.2-12.3) | −4.7 to +1.2 dB |
| Parlor Steel Fingerstyle | 4 | 27 | 24 | 18.0 (0.77) | 6.2 (2.5-8.7) | −3.4 to +1.5 |
| Mahogany Steel Thumb | 4 | 27 | 23 | 18.2 (0.82) | 6.2 (2.7-9.8) | −5.9 to +1.6 |
| Jumbo Steel Pick | 1 | 15 | 12 | 19.4 (0.77) | 6.7 (2.9-12.0) | +0.3 to +2.4 |
| Palm Muted Steel Pick | 3 | 16 | 15 | 19.4 (0.73) | 6.4 (3.4-11.7) | +0.3 to +2.6 |
| DADGAD Mono Mic | 3 | 33 | 28 | 18.3 (1.15) | 6.3 (3.1-9.5) | −3.8 to +1.3 |
| Open G Slide-ready | 4 | 21 | 19 | 18.1 (0.78) | 6.5 (3.3-10.1) | −4.8 to +1.6 |
| Piezo Stage Steel | 3 | 3 | 3 | 19.7 (0.64) | 5.2 (2.6-8.4) | +1.6 to +3.1 |

Level deviations are the mean over the three velocities against frets ±1
and ±2 on the same string.

**Tuning.** Settled pitch over the 3024 notes: 1.96 cents RMS, 82 beyond 5
cents and 27 beyond 10 (1.94, 1.90 and 2.03 cents RMS at the three
velocities). The 1.9.26 survey read 1.92 cents RMS, 50 and 17 of 2016. The
notes beyond 6 cents are the ones that survey names, all between E3 and A3
on the wound strings: E3 on the A string's 7th fret and the D string's 2nd
(−18 and −15 cents soft, +8 and +3 hard), F3 and F♯3 on the D string, G3 on
the low E's 15th fret (+12), the Jumbo's 11th fret (+8 to +11). Each is a
coupled pair 12-22 cents wide whose stronger member changes with velocity:
the sign flips between velocities on the same note, which a mistuned loop
cannot do. Not a model error
([coupled-tuning-2026-11-07.md](../research/coupled-tuning-2026-11-07.md)).

**Level.** Of the 168 notes whose sustain is more than 3 dB off their
neighbours, 146 are off the same way at the same pitch on another string: the
measured bodies' resonances and antiresonances, which a guitar has. Fifteen
more are pitches only one string plays. The other seven, and nine attack
levels flagged the same way (14 notes in all), were each compared with the
same pitch elsewhere:

- The open D string (four presets, −3 to −5 dB): D3 is as quiet on the A
  string's 5th fret and the low E's 10th (−28.6, −29.6 and −27.7 dBFS attack
  on the Dreadnought). The level climbs 9 dB from D3 to F3 on every string;
  an open string has neighbours on one side only, so the climb reads as a
  step.
- The Mahogany's open A string (+4 dB): A2 is within 1.1 dB on the low E's
  5th fret, and the level falls 4.7 dB from A2 to B2. The same one-sided
  edge.
- F5 and D♯5 on the top string's 11th-13th frets, A4 and A♯4 on the D
  string's 19th-20th, E5 on the B string's 17th, D♯5 on the G string's 20th:
  within 1-3 dB of the same pitch on the other strings.
- F3 on the Jumbo's D string, 3rd fret (sustain −3.5 dB): one of the coupled
  pairs under Tuning, read in a beat. Its attack is within 0.6 dB of its
  neighbours.
- D5 on the top string's 10th fret stands 8-10 dB over its 8th and 9th
  frets: those are C5 and C♯5, the Dreadnought body's known hole
  ([fretboard-evenness-2026-11-07.md](../research/fretboard-evenness-2026-11-07.md)).
  D5 is 6 dB quieter on the G string's 19th fret and the B string's 15th,
  which is the Finger's level falling up the neck (1.9.25), not the pitch.

None follows a string or a fret where the pitch does not. No per-note trim
exists and none was added.

**Velocity.** The attack rises 18.0-19.7 dB from velocity 32 to 120 with a
spread of 0.6-1.2 dB over the fretboard. Eight notes exceed the median by
3-5 dB, all on the two plain strings' 8th-17th frets under the Finger or the
Thumb, which fits a pad rounding a short string's soft attack more than its
hard one; the cause was not isolated. The Pick presets have none.

**Decay.** The fundamental's T60 has a median of 5.2-6.7 s. One to eight
notes a preset read over 20 s (a coupled pair's slow member, or a beat
through the fit's window) and at most two under 1.5 s (the Parlor's B2 and
G3, on its air and top modes). The 2-6 kHz T60 is 0.6, 0.9, 1.1 and 1.4 s on
the four wound strings and 2.7-3.0 s on the two plain ones.

**Open.** A Pick's attack level rises 0.3-2.6 dB an octave up each string
where the picked recordings fall 1.9 dB an octave (README, Known gaps). It
needs the player's effort or a second corpus to settle, not a quick win.

## The engine at 96 kHz against 48 kHz

The Rack device's 2x setting runs the engine at twice the host rate, so a
decay that depended on the rate would make its settings sound different.
Acukoto had that (its loop gain was not exact at the fundamental). Checked
on the Dreadnought (Finger) and the Jumbo (Pick), six strings at frets 0, 5,
12, 17 and 20, velocity 80, the 96 kHz render brought to 48 kHz and measured
with the sweep's own metrics:

| 60 notes | 96 kHz against 48 kHz |
| --- | --- |
| Settled pitch | within 0.06 cent on every note |
| Fundamental T60 | within 1% on 54 of 59, within 5% on 56; extremes −11% (B string, 20th fret) and +6% |
| Sustain level | median 0.06 dB apart; beyond 0.5 dB on 6 notes, all at the 12th fret or above on the three highest strings |
| Largest difference | C6, the top string's 20th fret, Dreadnought: 1.5 dB quieter at 96 kHz, its second partial 4 dB lower |

Acustra's loop gain already divides the loss sections' magnitude at the
playing frequency out, so the fundamental does not see the rate. What is
left is the upper partials on a loop 46 samples long at 48 kHz, which the
README already lists (the H8 decay spread of 4.3% over 44.1-96 kHz and the
dispersion fit's order by rate). No change.

## Library calls where the Rack device cannot inline them

Reason compiles a Rack Extension with clang at `-O2 -fno-builtin` to SSE3 or
NEON: no AVX2, no FMA, and a libm call on the audio path is a real call. A
build with those flags and `JUKEBOX_SDK` defined makes one kind of library
call in a held block, `atan2` once a control period
(`bridgePortMobility`); nothing per sample. The engine's `exact::` wrappers
and inlined complex division were written for this.

## Against Aculele's copy of the engine

Aculele forked this engine at 1.9.0. Its changes to shared code since, with
the Aculele version of each:

| Aculele change | Taken? |
| --- | --- |
| Arrival queues clear and copy only their pending slots (0.4.60) | Found independently the same hour; 1.9.40 is the same change. Acustra's axial arrival queue is cleared only at reset, so it needs none. |
| Level observers zeroed at the flush (0.4.61) | The same finding as 1.9.39. |
| 1e-30 floor on the longitudinal ring (0.4.57) | No. Tried first for 1.9.38: it cured only one of three sites. The flush covers all three. |
| Quiet-string stretch skip (0.4.56) | No. It leaves a residual 82 dB under the note, so here it would be a sound change for the owner's ear, and the four-cell sum was kept by ear on 2026-11-09. |
| Dispersion designs made ahead per string and fret, interim design until ready (0.4.59) | No. Acustra's design frequency carries each note's fretting detune, so a table misses; the interim design is audible. Future work, with the tuning solve. |

Aculele took the held-chord flush (1.9.38) and the denormal tracer from here.

## Found and not fixed

- **The 10% ceiling.** A held chord takes 8-12% of a 256-frame block and a
  first-time six-note chord's block 35-40%, preset-change blocks 27-37%.
  Half of a held block is the stretch sum at four cells, kept by ear; a
  note's tuning solve is 1.3 M instructions on a six-string shape. No exact
  change reaches 10%. What would: fewer cells (rejected by ear), or
  spreading a chord's solves over its first blocks, which changes the first
  milliseconds of those notes and needs a listening set.
- **The Rack device starts at 2x.** New instances and its factory patches
  run the engine at twice the host rate; the portfolio target is the lowest
  setting by default. The engine alone on the development machine (the
  device's decimation filter not included), Dreadnought Steel, as a share of
  a 256-frame host block at 48 kHz: a held chord 9.4-9.7% at 1x and 19-22% at
  2x; twelve first-time chords a mean of 13% against 21-22%, their worst
  block 41-44% against 55-60%. The 96 kHz check above shows what 1x would
  leave alone (pitch, the fundamental's decay) and where it differs (the top
  frets' upper partials); aliasing was not compared. The default lives in
  the adapter and is a choice between the two settings by ear.
- **A held chord at rest** still runs the whole per-sample loop over zeros,
  7.7% of a block. Suspending with strings held needs the suspension to
  carry them; not a quick win.
- **Subnormal transients.** A key-up's release noise envelopes (0.2 s) and
  the contact transport's last samples compute on subnormal numbers, about
  0.12 s of samples a six-string release; a held chord's last 2.4 s above
  the idle floor meets a few in the stretch sum. No state keeps them and no
  block cost was measurable.
- **A note-on still copies and clears 160 KB** of rings and transport
  history (0.4 M of a chord block's 22.8 M instructions).
- **96 kHz against 48 kHz at the top string's last frets**, above.

## Tests on the final tree

Release, gcc 13.3, `build-dsp`, rebuilt after the last source change
(`998c5df`):

| Tier | Result |
| --- | --- |
| `unit` (122) | 121 passed in the full run. `Acustra.StrummingRealism` hit its 60 s limit while the shared machine's load average stood at 11.8 on four cores; run alone straight after, it passed in 21.5 s, as it had in the full run before the rebuild (29.9 s, same engine source). |
| `audio` (3) | All passed. |
| `perf` (2) | `Acustra.EnginePerformance` passed. `Acustra.CpuBudget` fails on the 10% worst-block ceiling on all 16 rows, as 1.9.37 does ([CPU document](../benchmarks/cpu-consolidation-2026-11-09.md), the last section). |
| `AcustraDSPCxx17` | Builds. |

## Not run here

The plug-in build could not be made in this session (the pinned JUCE archive
download is refused by the session's network policy), so
`Acustra.ParameterReach`, `Acustra.ParameterMatrix`, `Acustra.PluginProcessor`
and the editor render were not run on these changes. The changes touch no
parameter and no control path; CI runs those suites.
