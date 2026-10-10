# The strings end on the bridge: saddle termination, 2026-10-10

Work package B of the 2026-10-10 realism round. Baseline: main `e51f6a9`.
Three commits, in this order:

| Commit | What | Listening label |
|---|---|---|
| `32a0a68` | The saddle-to-pin segment is no longer a spring to ground (B1) | C (with the by-ear anchor kept) |
| `a3e1a86` | The Dreadnought anchor's A0 and T1 move to where steel-strung guitars ring (B3) | D (spring-back still 0.8) |
| `a6f1ad8` | The spring-back keeps the level the listener chose | B, the recommended candidate |

A is main. Nobody has listened to any of this; every statement below about
sound is a measurement, not a preference.

B2, the air gain as a filter on the summed pressure, was built, measured and
not committed (below; the draft is a patch in the work package's scratch).
The first commit alone fails two of the engine's tuning contracts and the
2026-09-02 low-E gate, which the second commit restores, so the anchor move is
part of the recommended default rather than an optional extra; it is still a
separate commit.

## 1. The false ground spring (`32a0a68`)

### What was wrong

Each string ended on a port made of the measured bridge in parallel with a
spring from the saddle to ground: DAFx-26's fixed-end tail, the string from
saddle crown to anchor, stiffness `T/L` with `L = bridgeTailLengthMetres`
(3.25 mm, about 220 kN/m summed over six strings). The port was
`(Y^-1 + K/s)^-1` taken as 2x2 heave/rock matrices.

On a pin bridge both ends of that segment are on the bridge. When the bridge
heaves or rocks the segment goes with it; its tension acts along a line
through two points of one rigid body and puts no net force or moment on it.
The archive measured g21's bridge strung (Docs/decisions.md, 2026-09-02:
"the string ends on the driving-point mobility, which Mores measured on a
strung bridge, so a spring to ground is double-counted geometry (Woodhouse
2004)"), so the strings' static load is already in the mobility. There is no
lateral crown-to-pin term either: the segment's sideways pull is internal to
the same rigid body.

Above each body resonance the bridge is mass-like, and a spring to ground in
parallel with a mass resonates. Read from each Shape's configured digital
sections at every string's lever arm, the strings' port conductance against
the bridge's own:

| Shape (A0 / T1 on main) | worst ratio | near A0 | near T1 |
|---|---|---|---|
| Parlor (111.3 / 183.7 Hz) | 17.9 | 5.8 at 123.5 Hz (+1.8 st) | 17.9 at 218.4 Hz (+3.0 st) |
| Auditorium (95.7 / 171.2) | 13.9 | 7.9 at 109.5 (+2.3 st) | 13.9 at 226.5 (+4.8 st) |
| Dreadnought (84.7 / 158.6) | 23.3 | 8.9 at 99.3 (+2.8 st) | 23.3 at 200.0 (+4.0 st) |
| Jumbo (76.1 / 145.6) | 33.8 | 8.6 at 91.1 (+3.1 st) | 33.8 at 191.2 (+4.7 st) |

(44.1, 48 and 96 kHz agree within 0.05.) After the change the ratio is 1.00 at
every frequency, string, Shape and rate. In the running junction a G2, 2.5
semitones above the Dreadnought's A0, drained 13.9 dB/s through the bridge
where the bridge's own conductance predicts 2.17 (`10 log10(e) 4 Z G f0`); it
now drains 2.04.

The spring also shunted about half the low-frequency force the body should
have received: the release step's own share of the 60-180 Hz band on E4/A4
rises 6.0-6.8 dB without it, and the body's low modes ring harder on every
note (section 3).

### What changed

- `bridgePortMobility`: `normal = heave + 2u cross + u^2 rock`, and the
  parallel plane's `(h/a)^2 rock` and transfer `-(h/a)(cross + u rock)` as
  before; no stiffness, no determinant reduction, no scalar fallback.
- `BridgeDrive` keeps the incident heave/rock waves and the three impedance
  moments; the stiffness moments are gone. `BridgeLoad::process` loses its
  sample period and tail state (`tailIntegrated*`, `previous*`); the main
  and body integrated forces are the body's. `bridgeAnchorMoments`, the
  per-voice anchor target/applied/step/samples, the anchor generation bump
  in `configureVoice`, its ramp in `process`, and the tail force/power
  observers (`getLastBridgeTailForce/Power`, their derivatives) are removed;
  the power ledger keeps six waves.
- `bridgeTailLengthMetres` stays in `PhysicalCalibration` with its bounds and
  vector slot (saved calibrations and the fitting tools keep their layout),
  inert, and `Tools/OptimizePhysicalModel.py` lists it in `RETIRED` so no
  stage searches it (`Tests/OptimizerFreezeTests.py` checks that).
- Tools that printed the tail (`TraceRinging`, `BridgeProbe`,
  `ModalPreloadProbe`, `CompareCpuOptimization`) drop or zero it.

No parameter, property or control was added. DSP/Performer stay C++17:
`AcustraDSPCxx17` builds, and both files pass GCC 13 with
`-std=c++17 -Wall -Wextra -Wshadow -Werror -fsyntax-only`.

### Tests (commit 1)

New, both failing on main:

- `testTheStringsDrainIntoTheBridgeAlone` (BodyShape): the 2026-09-02 gate,
  port conductance under 3x the bridge's own over 60 Hz-3 kHz and from A0
  and T1 to 1.6 times them, every Shape, 44.1/48/96 kHz. Main 33.1 (Jumbo,
  44.1 kHz, string 3, 190.6 Hz); now 1.00001.
- `testAPartialDrainsAsItsBridgePredicts` (BodyShape): the G2 drain above,
  normal plane alone (parallel loop silenced, idle strings off), coupled
  minus rigid H1 decay over 0.3-1.5 s within 0.5x-2x (+-1 dB/s) of the
  conductance prediction. Main 13.9 against 2.17; now 2.04.

Changed: `expectedPhase` in BodyShape drops the anchors (the test's own
independent solve now has nothing in parallel); the steel-blend comment;
`testLiveTuningPreservesAnchorForceAndReachesItsTarget` is removed with the
anchor; Engine, BodyRealism, PitchGeometryCache, GuitarModel and Capture
tests follow the removed state (six-wave ledger, two power branches,
reaction = body force).

At this commit alone six checks fail, all diagnosed against main (section 2
explains the first two): the loaded E2's late mode (+7.06 cents, bound 1.5),
MIDI 52's settled pitch (-3.91, bound 2.8), the Pick's attack lead over the
soft contacts (4.65 dB, bound 5), the +48 member glide's peak (0.913 against
the -1 dBFS bound, 0.891), the room tail outlasting the dry silence by 2.57 s
(bound 2.5), and the merged-string Age step (2.2e-3 of the peak, bound
2e-4). Commit 2 clears all but the Pick's lead (4.89 dB there); commit 3
clears that.

## 2. A0 and T1 (`a3e1a86`)

### Why the anchor had to move

The wide Dreadnought anchor (Docs/decisions.md, 2026-09-24, chosen by ear)
lowers the measured bank's air mode to 84.7 Hz (`airHz` 98: 90.82 x 98/107 x
1.018) and its T1 to 158.6 Hz (`modeScale` 0.900: 178.53 x 0.900 x (1 -
0.018/sqrt 2)). With the spring in place no string felt either, and the
spring moved what was heard: the air transient on notes whose partials
leave 60-150 Hz empty sounds at 99.1 Hz on main (MIDI 60-68, idle strings
off), not at 84.7. With the strings ending on the bridge (commit 1):

- the air transient drops to 84.8 Hz, 2.7 semitones under what the listener
  heard, and its T60 rises from 0.50 to 0.65 s;
- A0 sits 47 cents over the open low E. The loaded E2 splits (+7.06 cents
  late, `testLoadedE2IsCentredAndNotSplit`) and its fundamental decays at
  28.3 dB/s (idle strings off, 0.15-1.2 s), where the 2026-09-02 gate allows
  twice its recordings' 4.5-8.6;
- T1 sits 0.66 semitone under E3 and pulls its settled pitch -3.91 cents
  (`testPhysicalSustainSettlesNearRequestedPitch`, bound 2.8);
- the corpus damping audit's 120-180 Hz band decays at 25.0 dB/s against
  the recordings' 9.6 (main 8.1).

Every T1 placement tried between 158.6 and 190 Hz moves pulls and beats
among E3-G3 rather than removing them; that is what a guitar with a
conducting bridge does near T1, and it is reported in section 5.

### What moved, exactly

`AnchorTransform` gains `t1Scale`, the factor for T1's group (the modes from
150 Hz up to T1; the plate modes above keep `modeScale`), through one helper
`anchorModeFactor` used by the anchor's own pair, the radiation poles and the
unpaired bridge poles. The anchor is `{ airHz 109, modeScale 0.900, bass
1.28, volume 0.93, asymmetry 0.018, t1Scale 1 }`:

- A0: 90.82 x 109/107 x 1.018 = 94.18 Hz, where both recorded dreadnoughts
  ring (93-95 Hz on the Eastman E1D and the Martin HD28, Docs/decisions.md,
  2026-10-01, which named the 84.7 Hz placement as an open problem). Its
  transient now sounds at 94.3 Hz (T60 0.55 s; the Eastman and Martin air
  transients' medians are 0.51-0.65 s, Docs/realism-consolidation-2026-10-02.md).
- T1: g21's measured 178.53 Hz under the same alternating detune, 176.26 Hz.
- Unchanged and still by ear: the plate modes x0.900, bass 1.28, volume 0.93,
  asymmetry 0.018, and the A0 decay extension's Q 23.5.

A placement at 99 Hz would reproduce the air pitch the listener heard
(`airHz` about 114.7); it was not chosen because the recordings ring at
93-95 Hz and the 99 Hz was the spring's artefact. The coupled
Christensen-Vistisen model carries the other Shapes with the anchor
(independently evaluated in double precision; the same evaluation from the
old anchor reproduces the old pins exactly):

| Shape | A0, main -> now | T1, main -> now |
|---|---|---|
| Parlor | 111.34 -> 121.49 Hz | 183.71 -> 208.06 Hz |
| Auditorium | 95.72 -> 106.12 | 171.18 -> 190.83 |
| Dreadnought | 84.68 -> 94.18 | 158.63 -> 176.26 |
| Jumbo | 76.09 -> 84.42 | 145.57 -> 162.13 |

### Tests (commit 2)

- Pins that moved with the anchor, each with its reason in the test: the
  Dreadnought anchor's A0/T1 and the coupled-model rows (Engine); the
  Original's A0 frequency (94.1-94.3 Hz) and free T60 bracket (0.54-0.56 s;
  Q 23.5 unchanged, `T60 = Q ln1000 / (pi f)`) (BodyShape).
- The vibrato test resolved the string loop alone. The loop is retuned for
  the bridge's reflection lag at each pitch of the excursion, which the
  bridge gives back; that change was +0.14 cents over the 20 at E3 with the
  spring and is +0.56 now (T1 is close). The loop alone read 20.57 cents
  where the string sounds 20.0. The snapshot now carries the bridge's lag
  (`bridgeDelay`, zero for every other user of `loopPhase`) and the depth
  reads 20.005 / 19.995 / 19.943 cents at 44.1 / 48 / 96 kHz; the 20.2 bound
  is unchanged.
- The 200-cent bend click test read the microphones. E3's 165-185 Hz sweep
  now crosses T1, the fundamental blooms, and in a 5 ms Hann frame its
  leakage is the 14 kHz band's floor: 1.64 / 1.69 at 44.1 / 48 kHz with the
  loudest frame where the bend crosses T1. The test now reads the saddle
  piezo with the bridge held (body-free): 1.048 / 1.048 / 1.067 at 44.1 / 48
  / 96 kHz on main and now alike. Bound 1.5 unchanged; the attack-glide half
  of the test still reads the microphones.
- RepeatedPluckRealism's merged-string Age step is read against the steady
  G2's peak. G2 now sits 0.7 semitone over A0 and drains into it, so that
  peak fell from 0.101-0.109 to 0.080, while the step itself stays at
  1.8-2.0e-5 of full scale (1.3-2.0e-5 on main; at commit 1 alone it reads
  2.2e-3 of the peak). The bound is 2.7e-4 of the peak, the same 2.0-2.2e-5
  of full scale that 2e-4 allowed.

## 3. The spring-back (`a6f1ad8`)

The release step (the held string's static force, given back at the pluck)
was chosen by ear at 0.8 of the force on 2026-10-01, through a junction whose
anchors took their share of it. Without them the same 0.8 reaches the body
stronger. The step's own part of the 60-180 Hz band over the first 300 ms of
E4 and A4, re the note's first second (Dreadnought, velocity 0.6, idle
strings on), commit 2 against main: Finger +5.00 / +4.89 dB, Pick +5.10 /
+5.10, Thumb +4.89 / +4.80. The share becomes 0.45 = 0.8 x 10^(-4.95/20), a
named engine constant (`releaseStepShare`) that the four tests recovering the
held displacement from the step now read. The whole band with the step,
main -> commit 2 (0.8) -> commit 3 (0.45), dB re the note:

| Picking, v0.6 | E4 | A4 |
|---|---|---|
| Finger | -35.1 -> -30.1 -> -34.1 | -30.9 -> -26.0 -> -30.2 |
| Pick | -31.2 -> -26.1 -> -30.6 | -27.9 -> -22.8 -> -27.4 |
| Thumb | -36.2 -> -31.3 -> -34.8 | -31.9 -> -27.1 -> -31.1 |

What remains above main (0.5-1.4 dB) is the body's own low ring, which the
spring no longer shunts (without the step the band is 5-7 dB over main). The
band stays flat with velocity re the note, as on main (0.3/0.6/0.9 within
0.7 dB of each other per Picking and note; main 0.5). The Pick keeps its
attack lead (2-12 kHz share of the first 15 ms: Pick -6.83, Thumb -12.73,
Finger -17.96 dB; at 0.8 the lead was 4.9 dB, under the test's 5).
Variant D keeps 0.8.

## 4. B2, not committed

The audit's finding 12: `lowBodyModeGain` (x4 on the left, bridge-side
microphone) scales the measured 85-145 Hz modes' residues, so on a moved
Shape the force path's left/right ratio grows without bound under A0 and
the moment path shows notches. Confirmed. The proposed fix, one RBJ peak on
the left channel's summed pressure at the configured A0, matched at the
default Dreadnought's E2 (Q from the mode's Q x 2.85/23.5, rms error 0.52 dB
from E2 to T1), was built on commit 1's engine. It reopens a 25 dB
left-channel notch on the Parlor's E2, where the residue gain had put the
bridge microphone on the upper-bout microphone's level under 150 Hz. It is
not recommended; the draft is `b2-filter-draft.patch` in the work package's
scratch, against commit 1, and was not re-measured on commits 2-3.

## 5. Before and after

Main against the recommended candidate (commit 3, label B); intermediate
columns where they matter. Release build, GCC 13.3.0, `-O3 -DNDEBUG`, CMake
3.28.3, Python 3.13; shipping calibration (the 24 `SHIPPING` values of
`Tools/OptimizePhysicalModel.py`), default controls unless stated.

### Corpus benchmark (lower is better)

Rendered with `AcustraPhysicalFitRenderer OUT <SHIPPING>` and scored with
`Tools/FitPhysicalModel.py OUT/{train,validation,flattop}.json`:

| | training | development validation | bank flat-top |
|---|---|---|---|
| main | 6.8630 | 6.5775 | 6.5853 |
| commit 1 | 7.1083 (+3.6%) | 6.9480 (+5.6%) | 6.6434 (+0.9%) |
| commit 2 | 6.9459 (+1.2%) | 6.8614 (+4.3%) | 6.3514 (-3.6%) |
| commit 3 (B) | 6.9190 (+0.8%) | 6.8121 (+3.6%) | 6.5366 (-0.7%) |

Flat-top terms, main -> B: attack 6.064 -> 6.119, body 13.401 -> 13.104,
decay 4.060 -> 4.105, harmonics 7.512 -> 7.417, pitch trajectory 0.632 ->
0.620, tuning 2.393 -> 2.364. Training: attack 8.233 -> 8.433, body 13.721
-> 13.973, decay 3.504 -> 3.442, tuning 2.093 -> 2.159. Validation: attack
9.800 -> 9.983, body 13.232 -> 13.866, tuning 1.792 -> 2.345.

The never-fitted flat-top rows prefer the louder spring-back (commit 2,
-3.6%); the archtop and classical monitors prefer the heard level. The
calibration was fitted around the spring and is not refit here.

### The 2026-09-02 termination gates (steel rows)

Idle strings off, default controls, velocity 90, early decay of the named
partial over 0.15-1.2 s:

| Row (recording, gate) | main | commit 1 | B |
|---|---|---|---|
| flat-top A#2 H2 (8.0, 16.0 dB/s) | 7.7 | 7.5 | 7.5 |
| flat-top A#3 H1 (17.8, 35.6) | 10.1 | 8.3 | 8.6 |
| steel A3 H1 (8.1, 16.2) | 14.6 | 8.9 | 9.6 |
| low E H1 (4.5-8.6, 17.2) | 8.6 | **28.3** | 11.4 |

No peak within +-150 cents of any row stands above -28 dB (the analysis
window's own sidelobes). The conductance gate is the BodyShape test above;
the suite runs at 44.1/48/96 kHz (and the extended-rate suite at its
rates); the corpus is re-scored above. The held-chord T60 ratio named in
that entry is not defined there precisely enough to reproduce and was not
measured.

### Damping audit (`Tools/AuditDampingCurve.py`, steel median early decay, dB/s)

| band Hz | recording | main | commit 1 | B |
|---|---|---|---|---|
| 80-120 | 5.1 | 8.7 | 11.4 | 11.4 |
| 120-180 | 9.6 | 8.1 | 25.0 | 10.8 |
| 180-270 | 12.0 | 9.2 | 8.9 | 8.7 |
| 270-400 | 15.0 | 13.4 | 11.6 | 13.9 |
| 400-600 | 20.5 | 12.5 | 12.5 | 13.4 |

Above 600 Hz the bands move by under 1.5 dB/s. Doublet-aware (`--doublets`),
the 180-270 Hz band's two-component decay goes 16.6 -> 11.8 dB/s against
the recordings' 12.1; under 180 Hz its "doublet" partner is a body mode, not
a string member (main 104.6 and 96.5 cents, B 224.5 and 95.5), so that
column says little there.

### Per note, Dreadnought, E2-G#3

Finger, velocity 90, first position, stereo microphones, idle strings on;
H1 at 40-60 ms (dB), bloom to its peak within 0.8 s, decay over 0.3-1.5 s,
pitch over 0.3-1.5 s:

| Note | attack A | attack B | bloom A | bloom B | decay A | decay B | cents A | cents B |
|---|---|---|---|---|---|---|---|---|
| E2 | -35.2 | -29.0 | 0.9 | 0.3 | 8.5 | 11.3 | +0.3 | +0.1 |
| F2 | -30.8 | -23.9 | 0.2 | 0.8 | 7.6 | 11.3 | +0.5 | +0.1 |
| F#2 | -26.6 | -20.8 | 0.6 | 3.0 | 7.8 | 25.3 | +0.4 | -0.5 |
| G2 | -23.9 | -20.4 | 3.8 | 1.7 | 18.8 | 18.4 | -0.5 | +1.1 |
| G#2 | -23.9 | -22.9 | 1.4 | 0.2 | 9.8 | 10.3 | +0.3 | +0.4 |
| A2 | -28.4 | -28.6 | 0.3 | 1.2 | 8.7 | 9.6 | +0.2 | +0.2 |
| A#2 | -33.5 | -30.3 | 1.3 | 0.3 | 7.7 | 8.6 | +0.2 | +0.2 |
| B2 | -34.7 | -30.5 | 0.2 | 0.2 | 7.1 | 7.9 | +0.2 | +0.2 |
| C3 | -35.2 | -30.2 | 0.2 | 0.1 | 6.4 | 7.2 | +0.3 | +0.2 |
| C#3 | -35.1 | -29.1 | 0.0 | 0.0 | 5.8 | 6.8 | +0.3 | +0.3 |
| D3 | -36.1 | -29.2 | 0.2 | 0.2 | 7.9 | 9.3 | +0.1 | +0.2 |
| D#3 | -34.0 | -26.3 | 0.1 | 0.1 | 7.9 | 10.7 | -0.0 | +0.3 |
| E3 | -31.7 | -23.1 | 0.1 | 0.6 | 13.0 | 1.7 | -2.5 | +0.6 |
| F3 | -28.3 | -18.5 | 0.3 | 0.2 | 8.7 | 11.9 | +0.4 | +3.3 |
| F#3 | -24.7 | -20.0 | 1.0 | 0.1 | 18.3 | 0.4 | +1.1 | -4.5 |
| G3 | -19.0 | -26.7 | 0.1 | 0.9 | 11.6 | 17.6 | -3.3 | -0.5 |
| G#3 | -15.7 | -24.9 | 0.5 | 0.3 | 15.4 | 12.1 | -4.4 | +0.1 |

The fundamentals E2-F3 radiate up to 10 dB more at the attack (+3 to +10
dB, except G#2 +1.0 and A2 -0.2): the force the spring shunted now reaches
the body. G3 and G#3 lose 7.7 and 9.2 dB, the spring's 200 Hz false
resonance having lifted them. F#2 now sits on A0 (fast decay,
+3 dB bloom). E3 and F#3 decay in two stages: the normal member drains into
T1 (H1 -19 and -21 dB at 0.4 s) and the parallel member takes over and
sustains; the 0.3-1.5 s fit reads that sustain (1.7 and 0.4 dB/s).

Parlor and Jumbo (same controls), the notes that moved most: Parlor G2 7.7
-> 27.7 dB/s and B2 25.4 -> 30.7 (A0 121.5 Hz), A#2 9.4 -> 17.3, G#3 22.4
-> 14.8; fundamentals E2-F#2 +7-9 dB at the attack. Jumbo E2 8.1 -> 23.5
dB/s and F2 9.5 -> 20.7 (A0 84.4 Hz: the Jumbo's low E now fails the
2026-09-02 low-E gate, which was written for the default), D#3 7.6 -> 20.2,
G3 23.4 -> 11.0; fundamentals A2-E3 +4 to +12 dB, F#3-G#3 -6 to -11 dB. Full
tables: `results/notes-A.json` and `results/notes-C3.json` in the scratch.

### Pitch: chords, doublets and late drift

Chord pull (E, G, Am, D, C held, every fundamental tracked over 0.1-0.6 s
and 0.8-1.3 s, coincident pairs reported apart), worst separable note early
/ late in cents:

| | Dreadnought | Parlor | Jumbo |
|---|---|---|---|
| main | 1.9 / 3.4 | 6.4 / 1.0 | 3.4 / 6.0 |
| commit 1 | 3.1 / 0.9 | | |
| B | 10.3 / 2.1 | 8.2 / 1.5 | 3.2 / 7.0 |

The Dreadnought's 10.3 is E3 in Am and C (-10.3 and -9.8 early): the open
low E string, idle in those chords, has its second partial on E3 and the
pair couples harder through a bridge that now conducts near T1. With the
idle strings off a single E3 reads +0.3 cents early (0.1-0.6 s), +2.2 at
0.8-1.6 s and +8.9 at 1.5-2.5 s (main +0.8 / +0.0 / +0.3); F#3 +1.1 / -5.9
/ -6.6 (main -0.1 / +2.2 / +4.6). On the A0 notes, with the idle strings
on: Dreadnought F#2 -3.1 early / +7.9 late, Jumbo E2 -4.5 / +9.8, Parlor B2
+3.1 / -7.5 (main within 1 cent). The late member is the parallel plane,
which shares the normal loop's compensated length by design ("the doublet's
split and its beat are left as the coupling makes them",
`coupledPolarisationDetune`), so near a strong body mode it is left off by
the normal member's compensation. The engine's own gates pass: E2 late, the
settled pitch of MIDI 40/45/52/59/64/71 (within 2.8 cents alone and 6.5
with the idle strings), the vibrato depth and the chord tests.

### Level and headroom

`Tools/CalibrateConstructionLoudness.py --renderer ... --jobs 2 --check`:
the default construction reads -25.33 LUFS where main reads -24.05
(-1.28 LU), and the stereo-microphone cells now span -0.44..+2.36 LU around
it (main -0.90..0.00; 22 of 36 within +-1). `--check` fails; the
ConstructionLoudness and ConstructionMatrix CTest suites pass, so the header
was not regenerated here (the integrator regenerates it after merging). The
hardest Pick case peaks -0.74 dBFS before the limiter on the stereo
microphones (main -0.24), +0.16 on the piezo (main +0.18). The piezo's least
reference headroom rises from 1.90 to 2.76 dB. The engine test's +48 member
glide peaks at 0.888 against its 0.891 (-1 dBFS) bound (main 0.849).
Silence after a released chord: 15.89 s dry and 18.13 s at Room 0.6 (main
18.46 and 19.91).

## 6. Exact commands

From the worktree root, Release build reused:

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF \
  -DACUSTRA_BUILD_TOOLS=ON -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE=<pyenv>/bin/python -DACUSTRA_REQUIRE_PYTHON_TESTS=ON
cmake --build build-dsp --config Release --parallel 2
ctest --test-dir build-dsp -C Release --output-on-failure
python Tools/CalibrateConstructionLoudness.py \
  --renderer build-dsp/AcustraPerformanceRenderer --jobs 2 --json out.json --check
build-dsp/AcustraPhysicalFitRenderer OUT $(python -c "import sys; sys.path.insert(0,'Tools'); \
  import OptimizePhysicalModel as o; print(' '.join(repr(float(v)) for v in o.SHIPPING))")
python Tools/FitPhysicalModel.py OUT/train.json   # and validation, flattop
python Tools/AuditDampingCurve.py OUT [--doublets]
python Tools/BuildRealismListeningTest.py --baseline-source <main DSP tree> \
  --candidate-source . --baseline-commit e51f6a9bd5c027d00e7e02cf9e3c58a35b8caa38 \
  --candidate-commit a6f1ad8e8ad383d6b8d5f613e3243fb14189fede \
  --track 01-evening-fingerpicking --track 02-open-road-strumming \
  --track 03-connected-melody --track 04-repeated-note-groove --out <new dir>
```

The other measurements are short scripts in the work package's scratch
(`wp-b/tools`, `wp-b/probes`), run on renderers and libraries frozen per
commit (`baseline-main-bin`, `bin-b1`, `bin-c2`, `bin-c3`), each built from
the named commit's sources and checked bit-identical where reused: per-note
metrics `notes.py`, chord pull `chords.py`, spring-back `springback.py` and
`step_report.py` (step on/off), air and T1 transients `body_transient.py`
(idle strings off), termination gates `termination_gates.py`, port ratios
`port_probe.cpp`, vibrato compensation `vib_probe.cpp`, bend leakage
`bend_leak.py`, Age step `age_probe.cpp`, coupled Shapes
`coupled_shapes.py`.

## 7. Listening sets

In `scratchpad/listening/wp-b/`, 48 kHz stereo, one scalar gain per file:

- `songs-A-main-vs-B-candidate/`: `Tools/BuildRealismListeningTest.py`, the
  four required songs dry and at Room 0.5, blind (`index.html`; in
  `audio/`, `x` is A and `y` is B, as `manifest.json` records with every
  trim). Track 05 (body transitions) was left out: the tool's gain match
  did not converge within 0.05 LU on its room render.
- `notes-chords-shapes/`: A, B, C and D through `AcustraPerformanceRenderer`
  (Finger unless named, velocity 90, stereo microphones unless named,
  spruce, Room 0): E2-G#3 single notes on the Dreadnought, Parlor and Jumbo;
  held E, Am, C and G chords on the Dreadnought; E4/A4 with Finger and with
  Pick (the spring-back); Auditorium G2-B2 on the mono microphone (the A2
  boom of 2026-10-09). `matched/` holds the gain-matched files, `raw-float/`
  the native renders, `manifest.json` the commits, controls and trims, and
  the folder's `README.txt` lists every trim.

Questions the sets can answer, none answered yet: B against A overall; D
against B (the spring-back at the level heard, or 0.8 now that the body
takes all of it); C against D (the by-ear anchor, which commit 1 alone
leaves with the low E split and drained, against A0/T1 where the recordings
ring); whether the Auditorium's low boom moved from A2 to G#2 or grew; and
whether the late pitch of the A0/T1 notes (F#2 on the
Dreadnought, E2 on the Jumbo, B2 on the Parlor, E3/F#3) sounds like a guitar
or like a tuning fault. Set 4 (2026-09-24) preferred stable high partials
over a +7 to -4 cent drift as the parallel member took over.

## 8. Validation

At `a6f1ad8`, configured with `-DACUSTRA_REQUIRE_PYTHON_TESTS=ON` and the
pinned Python 3.13 (NumPy/SciPy), every target built and the full CTest
suite passed: 70 of 70, including the Python tool tests. `AcustraDSPCxx17`
builds and both DSP/Performer files pass GCC 13's
`-std=c++17 -Wall -Wextra -Wshadow -Werror`. At `32a0a68` and `a3e1a86`
the focused suites were run and their failures are listed in sections 1
and 2.

## 9. Limits, risks and open questions

- The late drift above is the existing tuning policy (the parallel loop
  shares the normal loop's length) meeting a bridge that now conducts. A
  policy change - the parallel loop tuned on its own port, or the
  compensation aimed at the late window - is a design question for the
  ear, not part of this change.
- Every Shape now has a note on its A0 that drains fast and splits:
  Dreadnought F#2, Jumbo E2 (23.5 dB/s, past the low-E gate written for the
  default), Parlor G2/B2. Main had fast notes too (Dreadnought G2, Jumbo G3,
  Parlor B2), placed by the spring.
- The by-ear 2026-10-09 capture change reduced the Original Auditorium's A2
  boom on the close/mono microphones. On the mono microphone (Finger,
  velocity 90) A2's fundamental now leads H2/H3 by +20.6 dB over 80-250 ms
  (main +19.0) and +14.0 over 400-900 ms (main +21.3); G#2, now 0.4
  semitone under the Auditorium's A0 (106.1 Hz), leads by +26.2 early (main
  +20.3) and decays at 24.2 dB/s (main 8.5). The boom may have moved a
  semitone down rather than gone; it needs the same listener's ear.
- The construction loudness table needs regenerating (`--check` fails as
  above) and the default construction is 1.28 LU quieter at the tool's
  reference performance; whether to restore the absolute level is left to
  the integrator.
- The 24 calibration values were fitted with the spring; nothing was refit.
  The archtop/classical monitors read +0.8% and +3.6%.
- Two test fixtures changed what they read (the vibrato test now includes
  the bridge's lag; the bend click test reads the saddle piezo); the
  reasons and both readings are in the tests.
- README.md still describes the saddle-to-anchor stub in several places
  (Signal flow, Known gaps, the anchor and junction notes); this work
  package does not edit it.
- B2 was measured on commit 1's engine only.
- No CPU measurement was made. The change removes per-sample work (the
  anchor ramp, two derivative trackers, the tail power), but no timing
  claim is made.
