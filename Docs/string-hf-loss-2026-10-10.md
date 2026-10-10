# String high-frequency loss: loss angles, equal planes and the plate floor at steel level, 2026-10-10

Work package A of the 2026-10-10 realism round (evaluation findings 6, 7 and
8 and missed phenomenon 7 of
[the physical-model evaluation](physical-model-evaluation-2026-10-10.md)).
It is built on the saddle termination (work package B, `a6f1ad8`, report
[saddle-termination-2026-10-10.md](saddle-termination-2026-10-10.md)), merged
under it before the final fit. Commits on `worktree-agent-a668808f06d07141c`:

| Commit | What |
|---|---|
| `504b673` | Work in progress, set aside to merge WP-B under it (not for review on its own) |
| `1ec9388` | Merge of WP-B (`32a0a68`, `a3e1a86`, `a6f1ad8`, `cccf61f`) |
| `296924c` | The string loss itself: plate floor at steel level with a rocking residue, equal planes, the dislocation and winding-friction loss angles, the refitted friction |
| `a5cfa55` | Test fixtures the full suite showed (section 7) and the construction loudness tables regenerated with their tool |
| this report | |

Labels used everywhere below, in the listening sets and their manifests:

| Label | Revision | What |
|---|---|---|
| M | `e51f6a9` (main) | the shipping engine the round started from |
| W | `a6f1ad8` (WP-B head) | the strings end on the bridge; the string loss as on main |
| B | `a5cfa55` | this work, the candidate: winding friction `1.3e-4` |
| C | `ec25a13` (a scratch clone's branch on `a5cfa55`, not for merge) | B with winding friction `4.5e-4`, which keeps the old by-ear wound bending's early decay |

Nobody had listened to any of this when sections 1-10 were written. Every
statement about sound in them is a measurement, not a preference. B is the
candidate because it is closer to the recordings on every decay measurement
made; C is the listening alternative that keeps a listener's earlier choice
(section 4). The listening since, and the blend it asked for, are in
section 11.

## Summary

- The bridge's plate conductance floor was added in the measured flamenca's
  units while the measured modes it continues are scaled to steel level, so
  at 5-7 kHz the bridge's conductance stood at 7.2 times the Fylde's measured
  1.59e-3 s/kg (the piezo design's `G`); and it was heave only, so the
  parallel polarisation met no bridge loss above the rocking modes' 2.2 kHz.
  The floor now takes the modes' scale (0.207 as shipped) and a rocking
  residue equal to its heave one: 2.0 times the Fylde's at the bridge's
  centre, 3.4 times at the Fylde's own measurement point, and 0.24-0.94 dB/s
  of bridge loss for the parallel plane above 2 kHz (open strings and the
  5th and 12th frets), where it had at most 0.02.
- Both polarisations now lose the same intrinsic loss (Paté et al. measured
  the isolated string "similar for both polarizations"): one round-trip gain,
  0.99915, the geometric mean of the authored 0.9995/0.9988, and equal shelves.
- The strings' frequency-proportional loss is now two constant loss angles
  (Paté et al., after Valette): dislocation in the steel on every string,
  `Q_disl` 5500 scaled by the by-ear `steel.frequencyLossScale` (Q about
  10600), in place of an authored broad shelf; and winding friction on the
  four wound strings, a complex tension, in place of the by-ear cube-law
  bending factor 0.035 they used to carry. Every string keeps its plain-steel
  bending loss, a wound string's on its core.
- Measured, the tone is less static as it fades: at the default controls a
  plain-string note's 2-6 kHz band darkens against its 0.1-1 kHz band at 11.5
  dB/s over 0.3-1.5 s, where W darkened at 4.0 and the flat-top and archtop
  training recordings at 12.6 and 12.8 (the archtop validation notes at 4.7).
  The wound strings' upper partials, which W lost at 98 dB/s in their first
  0.3 s and then held at 26 dB/s, now decay at 45 and 36 dB/s (3.5-6 kHz).
- Against the recordings' own decay rates B is closer than W and M in most
  upper bands: the archtop damping audit's 4.5-9 kHz bands from 9-10 dB/s too
  slow to within 1.5 dB/s (mean absolute error over its twelve bands 3.1
  dB/s, W 4.0), the never-fitted flat-top rows' wound register at 1.5-5 kHz
  from 1.6-2.1 times the recordings' rate to 1.7-7.6 dB/s over it, and their
  plain register above 3.3 kHz from 5-21 dB/s too slow to within 2.8-9.1. It
  is further from them than W, by 1.6-3.6 dB/s, at the archtop's 0.9-2 and
  3-4.5 kHz bands and the flat-top plain register's 1.5-3.3 kHz.
- FitPhysicalModel scores B worse than W: +3.8% training, +3.2% validation
  and +10.1% flat top at the default (matched) observation, almost all of it
  the body term (the long-term spectrum is brighter once the floor no longer
  over-damps the top) and the decay term's low partials and noise-floor
  bands. Section 5 takes this apart; it is the main risk of this work.

## 1. The plate floor at steel level, with a rocking residue

### What was wrong

`plateConductanceMode` adds one over-damped positive-real section to the
bridge whose conductance is flat between a fitted corner (2187.76 Hz) and
16 kHz: the conductance a 50-mode fit of g21's driving-point mobility loses
between modes, where a real top has dense modal overlap (Docs/decisions.md,
2026-08-30). Its plateau `bridgeConductanceFloor` (0.011, chosen by ear on
2026-08-31 between the corpus's 0.0063 and the flat-top rows' 0.017) is in
g21's units, like the modal residues it continues, but the residues reach the
strings scaled to a steel-string guitar's level by `bridgeMobilityScale`
(0.755, fitted) times `steelTopMobilityRatio` (0.274, the Fylde Falstaff's
|Y| against g21's), and the floor was not. The bridge's conductance (Re Y
over 5-7 kHz, heave plus `u^2` rocking at lever arm `u`):

| | Re Y 5-7 kHz, centre (u = 0) | at the Fylde's point (u = -1) |
|---|---:|---:|
| measured modes alone | 1.05e-3 s/kg | 1.05e-3 s/kg |
| W (floor unscaled, heave only) | 1.14e-2 | 1.14e-2 |
| B (floor at steel level, rocking residue) | 3.20e-3 | 5.35e-3 |
| the Fylde's measured, the piezo design's `G` | | 1.59e-3 |

The floor entered as heave alone, and the measured rocking residues stop at
2.2 kHz, so the parallel polarisation, which reads the bridge's rocking times
`(h/a)^2`, met no bridge loss above it.

### What changed

`configureBridge` and `bridgeMobilityTable` scale the plate section's residue
by the same `bridgeMobilityScale * steelTopMobilityRatio` as the modes (0.207
as shipped: the by-ear 0.011 is 2.3e-3 s/kg at steel level) and give it a
rocking residue equal to its heave one. The argument for the rocking residue:
above the archive's 2.2 kHz corner its two side records disagree, the bridge
no longer moves as a rigid body, and two points moving independently with
point mobility `Y` have heave `Y/2`, normalised rocking `Y/2` and no cross
term in these coordinates. The residue matrix is diagonal and positive
semidefinite, so every port stays passive: a string's normal port gains
`G k(f) (1 + u^2)` and the parallel port `(h/a)^2 G k(f)`, `k` the section's
own shape (`testPlateConductanceFloorReachesBothPorts` checks both to 1% from
the documented design, independently; they agree to 2e-7).

What the bridge takes from a string per second (analytic, default controls,
48 kHz; normal-plane bridge loss, dB/s, band means):

| | 1-2 kHz | 2-3 kHz | 3-4.5 kHz | 4.5-6.8 kHz | 6.8-9 kHz |
|---|---:|---:|---:|---:|---:|
| E2 open, W | 8.6 | 10.4 | 14.1 | 16.8 | 15.6 |
| E2 open, B | 6.3 | 5.9 | 7.8 | 9.6 | 8.8 |
| E4 open, W | 8.8 | 10.1 | 13.5 | 15.8 | 14.6 |
| E4 open, B | 6.3 | 5.6 | 7.5 | 9.0 | 8.1 |
| E2 open, parallel plane, W | 0.2 | 0.0 | 0.0 | 0.0 | 0.0 |
| E2 open, parallel plane, B | 0.3 | 0.3 | 0.3 | 0.4 | 0.4 |

The parallel plane's share is small because `(h/a)^2` is; what changes its
late decay is mostly section 2 and 3, not the floor.

### The by-ear value and the piezo

`bridgeConductanceFloor` keeps its by-ear 0.011; what it means at the strings
changed. At the configuration it was chosen on (Dreadnought, Spruce, Finger,
stereo microphones) the plain strings' early upper-partial decay, which the
floor set, is kept: over 0.03-0.3 s, at 1-2 / 2-3.5 / 3.5-6 / 6-10 kHz, W
decays at 21.3 / 23.4 / 38.7 / 52.9 dB/s and B at 20.7 / 22.9 / 36.9 / 53.1
(section 5.4). The constant loss angles of section 3 carry what the scaled
floor no longer does early; late, the plain strings now keep fading (section
5.4).

The piezo design (`PiezoDesign`, `G` = 1.59e-3 s/kg) is not changed. The
bridge's real conductance is now 2.0-3.4 times it at 5-7 kHz, from 7.2 times;
the remaining factor is the by-ear plateau (0.011 against g21's measured
missing conductance 0.00151, Docs/decisions.md 2026-09-04), which is a
listener's choice and is left for the ear.

## 2. Equal intrinsic loss in both planes

Two authored factors split the string's own loss per round trip, 0.9995 on
the normal plane and 0.9988 on the parallel one, with the parallel plane's
broad and high shelves 6% and 8% deeper. A string's intrinsic loss is the
same in both planes: measured on a symmetric frame, "the results are similar
for both polarizations" (Paté, Le Carrou and Fabre, "Predicting the decay time
of solid body electric guitar tones", JASA 135(5) (2014) 3045-3055, Sec.
III.D). Both planes now carry `stringRoundTripGain` = 0.99915, the geometric
mean of the two factors, the same shelves and the same loss sections, in the
string loops and the retained tails; they differ only where their
terminations do, the saddle's heave and rocking ports. The round-trip gain is
still an authored value.

The sustain `steel.fundamentalT60Scale` was chosen by ear against: at the
default construction the fundamentals' decay over 0.3-3.5 s stays within 1.4
dB/s of W's from E2 to B4, A4 excepted (3.5 dB/s faster); on the top string's
E5 and B5 each plane stays within 2.8 dB/s, and the beating of the two planes
reads 3.9 and 4.7 dB/s slower (section 5.5).

## 3. The strings' constant loss angles

### Physics and provenance

Paté et al. (Sec. III.A, after Valette) give an isolated string's damping as
air friction, the visco- and thermo-elastic loss of its bending stiffness,
and two constant loss angles: dislocation in the metal, "well described by a
factor Q_disl, which is constant over the audio frequency range", and, in
wound strings, dry friction between successive turns, which "comes out as a
delay between the slope dz/dx and the shear force T(dz/dx) in the string,
hence the effect is the same as that of a complex tension T(1 + j delta_W)".
Fitted to a d'Addario EXL110 G string (plain, 0.43 mm) at every fret, every
partial under 1 kHz, `Q_disl` = 5500 (Sec. III.D).

On a stiff string tension carries `1/(1 + B n^2)` of partial n's restoring
force and bending the rest (the energy split Valette's and Woodhouse's bending
loss `eta B n^2/(1 + B n^2)` already uses), so a complex tension loses
`delta_W` on the first share as the bending loss loses `eta` on the second:

    sigma_n = pi f_n [delta_disl + (delta_W + eta B n^2) / (1 + B n^2)]
            = pi f_n [delta_disl + delta_W + (eta - delta_W) B n^2 / (1 + B n^2)]

The engine had the bending term (`bendingLossSection`) with one factor per
construction, the wound strings' 0.035 chosen by ear on 2026-09-28 to carry
"grime and corrosion between the windings" as bending loss. That law rises as
the cube of frequency: across the four wound strings at 2.8 kHz it spread 8:1,
and on the low E it took 50, 167, 431 and 864 dB/s at 1-2, 2-3, 3-4.5 and
4.5-6.8 kHz (analytic, intrinsic only), against 21, 34, 57 and 115 now.

### What changed

- **Dislocation, every string:** `delta_disl = steel.frequencyLossScale /
  5500`, so the by-ear 0.52 is a Q of about 10600, growing with String Age as
  the broad shelf it replaces did, `(1 + 1.35 age) / (1 + 1.35 x 0.15)`
  (pivoted on the default age). The broad one-pole shelf, an authored
  `72 x 1.65e-4 (1 + 1.35 age) x frequencyLossScale` per round trip above
  14.3 f0 that its own comment said was "not a per-string realization" of a
  measured loss, is retired: its mix is zero, its one-pole is not run, and it
  adds no lag to the tuning or dispersion designs.
- **Winding friction, the wound strings:** `delta_W =
  steelWoundFrictionLoss`, the calibration slot that held
  `steelWoundBendingLoss` (renamed; the vector keeps its 24 values and
  layout; its bound is now [0, 0.01]). String Age scales it as it scaled the
  wound strings' loss before, `max(0.2, 1 + 8 (age - 0.15))`: 0.2 of it on
  fresh strings, 7.8 times it at age 1.
- **Bending, every string:** `eta = steelPlainBendingLoss` (by ear,
  0.002334375), on a wound string less the friction its tension takes over,
  `max(0, eta - delta_W)`.

### Realisation

The two forms are two sections in cascade, each following a law it can:

- the existing all-pole bending section (`bendingLossSection`) on the factor
  `eta - delta_W`, unchanged in design;
- a new constant-loss section (`constantLossSection`) on `delta_disl +
  delta_W`. A constant loss angle is a loss per round trip that grows as the
  first power of frequency, which no all-pole section follows (its
  `|H|^-2` rises at least as `omega^2`). Two relaxations can, as a spectrum of
  relaxation times stands in for a constant loss angle over a band in
  viscoelastic models: `|H|^-2 = 1 + (a u + b u^2) / ((1 + u/u1)(1 + u/u2))`,
  `u = 4 sin^2(omega/2)`, a section with two poles and two zeros, passive and
  minimum phase for any corners when `a >= 0` and `a + 4b >= 0`. The corners
  are tried from a small grid spread over the band, `a` and `b` fitted for
  each by weighted least squares in relative error on the partials from where
  the law adds 3 dB/s to where it adds 300 dB/s (with a lightly weighted tail
  to 20 kHz or 0.45 fs), and the pair that meets the law best at its worst
  partial is kept.

Both run as `y = x - (1 - z^-1)(g0 + g1 z^-1) / A(z) x` (`g0 = 1 - g`,
`g1 = n2 - a2`; for the bending section `n2 = 0`), the same transfer function
with exactly unit gain at DC in any precision: at the highest host rates their
poles close on `z = 1`, where the direct form's DC gain rested on float
rounding (4e-6 above one at 768 kHz). Both are designed at the physical
playing frequency and the host rate with the dispersion (their phase enters
the dispersion fit and the tuning), ramp with the other intrinsic
coefficients on a discrete retune, seed from the wave they meet when switched
on under a sounding string, are scaled with the stored waves, and are copied,
captured and reset with the loop. Measured (tests, section 7): the constant
section adds `pi (f_n/f0)(delta_disl + delta_W)` per round trip within 18% on
18411 partials where that adds 5-250 dB/s, within 0.71 dB/s below, at 44.1-192
kHz, String Age 0, 0.15 and 1, with and without friction; the bending section
keeps its 12% over 20-160 dB/s; both are passive at every host rate 8-384 kHz
and in the extended layout to 768 kHz.

## 4. Calibration on the merged base

Only the value whose meaning changed was refitted: `steelWoundFrictionLoss`,
swept alone with every other value at its shipped one (FitPhysicalModel,
matched observation; mid in brackets):

| delta_W | training | validation | flat top |
|---|---:|---:|---:|
| 0.6e-4 | 7.0371 (7.1821) | 6.9532 (7.0016) | 6.7838 (7.2824) |
| **1.3e-4 (B)** | **7.0144 (7.1810)** | **6.9241 (7.0007)** | **6.8304 (7.3293)** |
| 1.8e-4 | 7.0127 (7.1874) | 6.9175 (7.0050) | 6.8782 (7.3758) |
| 2.5e-4 | 7.0397 (7.2344) | 6.9427 (7.0373) | 6.9778 (7.4631) |
| 4.5e-4 (C) | 7.1925 (7.4084) | 7.0135 (7.1417) | 7.3154 (7.7863) |
| 7e-4 | 7.4713 (7.7410) | 7.1859 (7.3330) | 7.7695 (8.2012) |

The fitted splits are flat within 0.1% from 1.3e-4 to 1.8e-4 and 0.3-0.4%
worse at 0.6e-4, which the flat-top rows prefer by 0.7%; 1.3e-4 is shipped.
(The sweep rows other than B and C, here and in the corner sweep below, were
rendered before the bending section's exact-DC form, which moves B's scores by
at most 0.007 and C's by at most 0.008.) The evaluation's
suggested `delta_W` of 0.7-1e-3, read from the Eastman's picked first-300-ms
band decays, is not what the per-partial decays support once the core's
bending loss and the dislocation are beside it: at 7e-4 the splits are 4-14%
worse and the damping audit's 0.6-3 kHz bands decay 9-26 dB/s too fast.

The plate floor's corner (`bridgeConductanceCornerHz`, fitted, not by ear) was
swept too, since the floor's level changed under it:

| corner | training | validation | flat top |
|---|---:|---:|---:|
| 800 Hz | 6.9867 | 6.9165 | 6.7749 |
| 1200 Hz | 6.9941 | 6.9299 | 6.7856 |
| 1600 Hz | 6.9989 | 6.9138 | 6.8020 |
| **2187.76 Hz (kept)** | 7.0144 | 6.9241 | 6.8304 |
| 3000 Hz | 7.0496 | 6.9364 | 6.8607 |

A lower corner buys 0.4% through the body term while the decay term gets
worse (training 3.70 to 3.79 at 800 Hz); the corner is kept where the
archive's records part.

Every value chosen by ear keeps its value: `bridgeConductanceFloor`,
`steel.fundamentalT60Scale`, `steel.frequencyLossScale`,
`steelPlainBendingLoss` and the rest of `BY_EAR`.
`steelWoundFrictionLoss` joins `BY_EAR` in `Tools/OptimizePhysicalModel.py`,
so no fitting stage moves it until a listener has heard B against C.

**C, the listening alternative.** The wound strings' 0.035 was chosen by ear
(Set 14, "snap then mellow"), and the friction law replaces it. At the
default controls the geometric-mean decay of the wound register's partials at
0.5-4 kHz over their first 0.03-0.3 s is 37.1 dB/s on W, 26.1 on B and 37.1
on C (`delta_W` = 4.5e-4): C keeps the listener's early snap; B follows the
recordings (section 5). C is in the listening sets as its own revision.

## 5. Before and after

Corpus: the shipping reference-bank export (archtop training and validation
rows, the eight never-fitted Eastman E1D flat-top rows), rendered models-only
with each revision's `AcustraPhysicalFitRenderer` and its shipped 24 values.
Scorer: `Tools/FitPhysicalModel.py` as on the integration branch
(`claude/awesome-shannon-eax46r`, WP-L), at its default `matched` observation;
`--observation mid` (this tree's scorer) in the second table.

### 5.1 FitPhysicalModel

Matched observation (default):

| | training | validation | flat top |
|---|---:|---:|---:|
| M `e51f6a9` | 6.6863 | 6.4755 | 6.2147 |
| W `a6f1ad8` | 6.7596 | 6.7089 | 6.2043 |
| **B** | **7.0144** | **6.9241** | **6.8304** |
| C | 7.1925 | 7.0135 | 7.3154 |
| B against W | +3.8% | +3.2% | +10.1% |
| B against M | +4.9% | +6.9% | +9.9% |

Mid observation:

| | training | validation | flat top |
|---|---:|---:|---:|
| M | 6.8630 | 6.5775 | 6.5853 |
| W | 6.9190 | 6.8121 | 6.5366 |
| **B** | **7.1810** | **7.0007** | **7.3293** |
| C | 7.4084 | 7.1417 | 7.7863 |

Terms, matched (training / validation / flat top):

| term | W | B |
|---|---|---|
| body | 13.761 / 13.672 / 12.774 | 14.564 / 14.095 / 14.430 |
| decay | 3.550 / 2.531 / 3.808 | 3.699 / 3.166 / 5.392 |
| harmonics | 10.512 / 10.409 / 6.644 | 10.563 / 10.632 / 6.474 |
| attack | 7.634 / 8.851 / 6.012 | 8.074 / 8.594 / 5.772 |
| tuning | 2.155 / 2.364 / 2.386 | 2.151 / 2.364 / 2.399 |
| pitch trajectory | 0.801 / 0.205 / 0.589 | 0.785 / 0.198 / 0.588 |
| dynamics | 0.036 / 0.017 / - | 0.037 / 0.018 / - |

Where it comes from, step by step from W's behaviour (scratch build with
switches, before the exact-DC form, so its last row reads B within 0.005;
mid observation; training / flat top):

| step | training | flat top | body (training) | decay (training) |
|---|---:|---:|---:|---:|
| W's behaviour | 6.9206 | 6.5355 | 13.972 | 3.449 |
| + floor at steel level with rocking residue | 7.4162 | 7.3493 | 15.848 | 3.498 |
| + equal planes | 7.5060 | 7.3246 | 15.843 | 3.959 |
| + dislocation for the broad shelf | 7.5422 | 7.5485 | 15.966 | 3.942 |
| + winding friction for the wound bending factor (= B) | 7.1788 | 7.3249 | 14.555 | 3.753 |

The floor's scaling costs the most, all of it in the body term: the body
term reads the normalised band levels over 0.08-0.9 s, and with the top no
longer over-damped at the bridge the long-term spectrum is brighter. On the
wound register the model is already too bright there (model minus recording,
dB, after normalisation; training rows MIDI <= 58, 1.5-2.5 / 2.5-4 / 4-6 kHz):
W +4.9 / -1.6 / -14.0, B +7.9 / +10.7 / -0.2; flat top +10.3 / +7.9 / -5.8 and
+12.9 / +20.4 / +8.3. The unscaled floor was standing in for an excitation or
capture that is too bright at 1.5-4 kHz on the wound strings; the Eastman
finger rows had said the opposite at the attack (README, Known gaps), so this
is not settled here. The decay term's change has two sources: the low
partials (H3-H8, the per-partial robust fit over 0.12-4 s) were already
decaying faster than the archtop's and B adds 1-4 dB/s there (the equal
planes and the frequency-proportional losses reach them); and on the flat top
the 3.7-8 kHz band's target is the recording's noise floor (2 dB/s clipped
on most rows), which a faster model decay is charged for. Adding each
flat-top recording's own floor to the model (a diagnostic, not the
benchmark): flat top 5.3776 (W) and 5.9282 (B), decay term 1.408 and 1.554.

### 5.2 Damping audit (`Tools/AuditDampingCurve.py`, archtop training and validation)

Median early decay, model minus recording, dB/s:

| band, Hz | recording | M | W | B | C |
|---|---:|---:|---:|---:|---:|
| 80-120 | 5.1 | +3.6 | +6.3 | +6.5 | +6.5 |
| 120-180 | 9.6 | -1.5 | +1.2 | +1.4 | +1.4 |
| 180-270 | 12.0 | -2.8 | -3.3 | -2.7 | -1.5 |
| 270-400 | 15.0 | -1.6 | -1.1 | -1.9 | -0.3 |
| 400-600 | 20.5 | -8.0 | -7.2 | -7.4 | -2.2 |
| 600-900 | 14.2 | -2.1 | -1.2 | +1.0 | +6.8 |
| 900-1350 | 22.3 | -0.1 | -0.4 | -4.0 | +3.0 |
| 1350-2000 | 26.9 | -3.6 | -2.7 | -5.6 | +4.6 |
| 2000-3000 | 27.0 | +3.4 | +4.8 | -1.2 | +14.0 |
| 3000-4500 | 27.3 | -1.4 | -0.8 | +2.6 | +3.4 |
| 4500-6800 | 38.6 | -9.1 | -9.1 | -1.5 | -1.4 |
| 6800-9000 | 49.1 | -9.8 | -10.0 | +1.3 | +0.9 |
| mean absolute | | 3.9 | 4.0 | 3.1 | 3.8 |

The 80-180 Hz rows are WP-B's (its A0 and T1 moves), not this work's. B
gives up 0.9-2 kHz (3-4 dB/s slower than W, which was closer) and 3-4.5 kHz
(2.6 dB/s too fast where W was 0.8 too slow) for 2-3 kHz (from 4.8 too fast
to 1.2 too slow) and 4.5-9 kHz (from 9-10 dB/s too slow to within 1.5).

### 5.3 The never-fitted flat-top rows, per partial

AuditDampingCurve's own partial admission on the eight Eastman E1D rows,
median decay over the audit window 0.15-1.2 s, dB/s:

| wound register (MIDI 40-58) | 0.2-0.6 | 0.6-1.0 | 1.0-1.5 | 1.5-2.2 | 2.2-3.3 | 3.3-5.0 kHz |
|---|---:|---:|---:|---:|---:|---:|
| recording | 7.2 | 13.0 | 18.0 | 21.5 | 22.1 | 30.5 |
| M | 12.5 | 16.8 | 26.6 | 33.2 | 50.3 | 72.9 |
| W | 12.2 | 18.3 | 23.9 | 35.4 | 46.4 | 61.0 |
| B | 12.2 | 16.6 | 20.4 | 23.2 | 27.6 | 38.1 |
| C | 16.6 | 23.9 | 29.1 | 37.0 | 45.1 | 61.6 |

| plain register (MIDI 64-83) | 0.6-1.0 | 1.0-1.5 | 1.5-2.2 | 2.2-3.3 | 3.3-5.0 | 5.0-7.5 | 7.5-10 kHz |
|---|---:|---:|---:|---:|---:|---:|---:|
| recording | 14.0 | 16.1 | 16.8 | 20.3 | 27.8 | 42.8 | 62.4 |
| M | 15.8 | 16.9 | 15.7 | 20.9 | 22.9 | 32.9 | 41.9 |
| W | 15.4 | 18.3 | 16.4 | 21.4 | 22.5 | 32.8 | 41.8 |
| B | 14.8 | 17.9 | 19.8 | 23.0 | 30.6 | 39.2 | 53.3 |
| C | 14.7 | 17.9 | 19.2 | 23.0 | 30.6 | 39.2 | 52.4 |

And over the first 0.02-0.3 s (the same admission), the wound register at
1.5-2.2 / 2.2-3.3 / 3.3-5 kHz: recording 25.6 / 33.0 / 27.6, W 33.6 / 47.3 /
72.4, B 24.6 / 27.5 / 39.1, C 37.6 / 45.7 / 72.6.

### 5.4 The fade: does the tone stay static?

The listener chose "gentle sustain warming" and found the "tone stays too
static as it fades" (Docs/decisions.md, 2026-10-09). Read here as how fast a
note's 2-6 kHz band falls against its 0.1-1 kHz band (dB/s, negative is
darkening, median over notes; recordings read only while their 2-6 kHz band
stands 10 dB over its own floor):

| | 0.3-1.5 s | 1.5-3.5 s |
|---|---:|---:|
| default controls, plain notes: W | -4.0 | -4.0 |
| default controls, plain notes: B | -11.5 | -7.8 |
| default controls, plain notes: C | -11.1 | -7.6 |
| default controls, wound notes: W | -12.0 | -10.3 |
| default controls, wound notes: B | -14.1 | -13.0 |
| default controls, wound notes: C | -26.3 | -6.5 |
| flat-top recordings, MIDI 64-83 (4 rows) | -12.6 | (under floor) |
| flat-top models, MIDI 64-83: W / B | -4.5 / -11.1 | -3.8 / -7.8 |
| archtop training recordings, MIDI > 58 (30 rows) | -12.8 | -8.2 (7 rows) |
| archtop training models: W / B | -4.9 / -7.7 | -3.4 / -6.4 |
| archtop validation recordings, MIDI > 58 (8 rows) | -4.7 | -0.1 (5 rows) |
| archtop validation models: W / B | -5.2 / -8.4 | -4.0 / -5.7 |
| archtop training recordings, MIDI <= 58 (16 rows) | -20.6 | (under floor) |
| archtop training models: W / B / C | -16.8 / -16.8 / -29.3 | -6.6 / -10.4 / -0.1 |

Default controls: Dreadnought, Spruce, Finger, stereo microphones, velocity
0.7, 4 s per note; wound notes MIDI 40, 45, 50, 52, 55, 57, 62, 67 and plain
59, 64, 69, 71, 76, 83. Per-partial decay of the same notes, median per band,
dB/s:

| wound notes | window | 0.1-0.4 | 0.4-1.0 | 1.0-2.0 | 2.0-3.5 | 3.5-6.0 | 6.0-10 kHz |
|---|---|---:|---:|---:|---:|---:|---:|
| W | 0.03-0.3 s | 11.4 | 16.6 | 28.8 | 57.0 | 98.1 | 58.6 |
| B | | 11.6 | 16.6 | 22.1 | 30.8 | 44.5 | 61.9 |
| C | | 12.7 | 22.4 | 31.8 | 47.8 | 76.0 | 88.9 |
| W | 0.3-1.2 s | 9.5 | 16.1 | 22.2 | 35.2 | 37.8 | 43.8 |
| B | | 10.8 | 15.7 | 20.8 | 29.9 | 39.7 | 56.3 |
| C | | 12.0 | 21.9 | 31.3 | 46.3 | 67.0 | 50.4 |
| W | 1.2-3.5 s | 9.3 | 12.2 | 20.0 | 26.0 | 25.7 | 32.2 |
| B | | 10.1 | 14.2 | 20.8 | 28.0 | 36.0 | 47.0 |
| C | | 10.7 | 20.2 | 29.8 | 38.4 | 30.6 | 43.9 |

| plain notes | window | 0.4-1.0 | 1.0-2.0 | 2.0-3.5 | 3.5-6.0 | 6.0-10 kHz |
|---|---|---:|---:|---:|---:|---:|
| W | 0.03-0.3 s | 16.2 | 21.3 | 23.4 | 38.7 | 52.9 |
| B | | 17.8 | 20.7 | 22.9 | 36.9 | 53.1 |
| W | 0.3-1.2 s | 11.9 | 14.0 | 20.8 | 29.4 | 43.8 |
| B | | 12.4 | 18.4 | 23.2 | 34.4 | 52.2 |
| W | 1.2-3.5 s | 14.2 | 13.3 | 16.8 | 23.4 | 35.3 |
| B | | 13.0 | 15.7 | 21.0 | 29.1 | 42.9 |

(C's plain notes are B's within 0.6 dB/s over the first 0.3 s and within
2.3 later, where the open wound strings ring along.) So, measured: yes, the
upper partials fade more through the note. W's wound strings lost most of
their top in the first 0.3 s (98 dB/s at 3.5-6 kHz) and then decayed at 26;
B's decay at 45 and then 36, more evenly through the note. W's plain strings
darkened at 4 dB/s from 0.3 s to the end; B's darken at 11.5 and then 7.8,
about the rate of the flat-top and archtop training recordings. Whether that
is "more natural" is the listener's question.

### 5.5 Sustain

Fundamentals' decay at the default controls, 0.3-3.5 s, dB/s (B minus W):
E2 +0.04, A2 +0.60, D3 +0.04, E3 -0.09, G3 -0.55, A3 +1.16, B3 +0.73, C4
+0.89, D4 +0.48, E4 +0.61, G4 +1.37, A4 +3.53, B4 -1.29, E5 -3.91, B5 -4.74.
On E5 and B5 each plane alone moves 1.5-2.8 dB/s (normal 20.8 to 18.0 and
15.7 to 13.9, parallel 11.7 to 9.3 and 12.7 to 11.2); the rest is how the two
planes beat. The whole note's level decay over 0.1-1.0 s moves from -8.0
dB/s (B5) to +2.5 (D4), the notes from B3 to G4 +0.8 to +2.5 and the low
strings' within 1.3.

## 6. Checks

- Passivity: every string loss section's cascade `|H| <= 1` from 10 Hz to
  Nyquist at 8, 11.025, 22.05, 44.1, 48, 88.2, 96, 192 and 384 kHz and in the
  extended layout to 768 kHz, at String Age 0, 0.15 and 1 (1080 sections; DC
  identities to 1.5e-7); the bridge ports stay positive real with the floor's
  rocking residue (diagonal positive semidefinite).
- Rates: the plate floor's own decay contribution moves 1.05% across 44.1-192
  kHz (20% at 11-12 kHz, bound 30%); the constant section's law holds at
  44.1-192 kHz.
- Block and partition invariance, cache parity, retune continuity, reset,
  clear, copy, capture and tail restoration: the new section's state is in
  every path the bending section's is (`StringLoop::reset`, `scaleStoredWaves`,
  tail capture, the twelve-coefficient retune ramp); RetuneContinuity,
  PitchGeometryCache, RepeatedPluckRealism and StringPitchRealism check them
  and pass.
- Pitch: the sections' phase enters the dispersion design and the tuned
  delay; the bending and constant sections each leave the fundamental's
  pitch within 0.01 cent and its decay within 1e-3 dB per pass of the loop
  gain's request (Engine).
- C++17: `AcustraDSPCxx17` builds (GCC 13.3), and both DSP sources pass
  Clang 18 and GCC 13 with the Rack toolchain's `-std=c++17 -Wall -Wextra
  -Werror` and `-Wshadow`.
- CPU: one more biquad per loop, the broad one-pole no longer run. Not
  measured with the paired protocol; the Engine suite's six-string realtime
  ratio reads 0.0627 against W's 0.0630 (unpaired, indicative only).

## 7. Test changes and why

New tests:

- `testConstantLossFollowsItsLaw` (Engine): the constant section against
  `pi (f_n/f0)(delta_disl + delta_W)` per round trip, 20% over 5-250 dB/s and
  0.75 dB/s below, at four rates, three ages and three calibrations; the
  fundamental's decay and pitch unchanged; an open string's fundamental
  exactly at its requested T60 times 0.99915.
- `testStringLossSectionsArePassiveAtEveryRate` (Engine), `stringLossIsPassive`
  (ExtendedSampleRates): the cascade's passivity and DC identities, 8-768 kHz.
- `testStringLossIsTheSameInBothPlanes` (Engine): both planes carry the same
  sections, loop gain and shelves at four ages and three rates; String Age
  ages a wound string's friction (x3.4 at 2 kHz from age 0.15 to 1) and only
  the dislocation of a plain one (x1.85).
- `testPlateConductanceFloorReachesBothPorts` (Engine): the floor at the normal
  ports as `G k (1 + u^2)` and at the parallel port as `(h/a)^2 G k`, to 1%,
  `k` from the documented design.
- StringDecayRealism: the constant section follows the sounding frequency
  through a bend (20% over 5-250 dB/s), as the bending section does (12%).

Changed tests, each against W's behaviour first:

- `testBendingLossFollowsItsLaw`: the wound case's factor is the plain eta
  less the friction (0.006 - 0.001), as the bending section now carries it.
- `testThePlateFloorDampsAlikeAtEveryRate`: reads the decay the floor adds
  (bridge with it against without), 1.05%, bound unchanged at 2.5%. It read
  the whole bridge's decay, which the unscaled floor dominated; scaled, the
  total also shows the measured modes' own rate dependence (192 kHz keeps
  modes above 0.45 x 48 kHz), 2.9% (1.9% under the unscaled floor).
- `testPlateConductanceFloorDampsOnlyTheUpperBand`: its 0.004 is restated in
  the floor's units (0.004 / 0.207), the same 0.004 s/kg at the heave port.
- `testASlewingDelayDoesNotClickAboveFourteenKilohertz`: with the friction
  law a still E3's partials above 14 kHz ring past 0.25 s, where a bend moves
  them: 1.61 on the shipped calibration, the same with the loss sections held
  at their unbent design (not a redesign's transient). It is now read on a
  string whose top band has gone by then (every string's bending factor at
  0.035, no friction), 1.03 / 1.04 / 1.01 at 44.1 / 48 / 96 kHz; bound 1.5.
- `testMaterialCalibrationChangesStringAndPluckDescriptors`: the frequency
  loss now reaches the high shelf and the constant section (its broad-shelf
  branch is retired); checked at 8 kHz on the low E with no friction.
- RepeatedPluckRealism tremolo: the onset bound compared each stroke's step
  with the larger of the isolated attack's and the ringing train's; a new
  attack on a ringing string superposes the two, which the triangle
  inequality bounds by their sum. It held only while a wound string's top
  died within one 30 ms stroke.
- Capture, piezo clip aliasing: the hard Pick strum at the bridge at Touch 1
  swings 0.90 as deep into U1B as it did (-2.94 V against -3.28 V at 44.1
  kHz, unit weights) and more broadly (the default Touch's 1.07 as deep), so
  the drives 1.25 and 1.30 no longer clipped it. It is driven from where it
  first clips, 1.35-1.51, 0-4 host samples past the swing at 44.1 kHz and 0-6
  at 48 kHz (the default Touch's 2-4; it read 1-2 before), all under the
  unchanged -60 dB (-62.2 to -68.7). Scaled 1.15 times instead, as deep as
  before in volts, its deepest clips went 9 samples past and read -58 to -60
  dB.
- Capture, room ring-out (WP-G's test, touched minimally): the dry reference
  is the instrument's idle flush, which waits for the body and the piezo
  under its construction trim to fall below 1e-11; the regenerated tables
  lower the default construction's piezo trim 0.9 dB, which moves the dry end
  0.23 s earlier and the room's end not at all, and the string change moved
  the room's end 0.24 s later: the room adds 2.67 s (2.24 s on a6f1ad8)
  against a 2.5 s bound, now 3 s.
- Release, the hand-back to the open string: its fretted-to-open
  reconfiguration makes steps of its own (the second dispersion section's
  switch-out and the delay's 6 ms slew under the damped wave, both allowed at
  a fret change, `switchSecondDispersion`). The test read them at 15-21 dB
  over the local top-band floor only because the low E's 0.035 bending law
  took 10-30 dB a round trip off that band; in the same performance on
  a6f1ad8 the D string reads 25-27 dB and the high E 26-29 dB at 44.1 and 48
  kHz, over its 25 dB bound. With physical loss the low E reads 31-39 dB (the
  D 28-37, the high E 24-27), while the one-sample port cut the test was
  written to catch reads 69-76 dB on this low E today (the comment's 30-40
  dB were measured on an earlier engine). The bound moves from 25 to 50 dB,
  between the two; the D string's cut, 42-51 dB against its 28-37, is too
  close to it to be added as a second case. Both steps were isolated with
  switches in a scratch copy: holding the second section through the
  hand-back removes the immediate step, slowing the slew eight-fold the later
  one (23/19/22 dB at 44.1/48/96 kHz with both).
- BodyRealism's independent tuning observer adds the plate floor's rocking
  residue it now has.
- The PhysicalFitRenderer smoke vector's slot 23 (`.035`, the old wound
  bending factor) is the friction's `.00013`.
- Construction loudness: Auditorium Maple Finger on the mono microphone
  played 3.02 LU from the default (2.81 on a6f1ad8; bound 3). The tables are
  regenerated with `Tools/CalibrateConstructionLoudness.py --write-header`
  from the `296924c` build (the strum-constrained write refuses, as it must
  keep the default construction's piezo trim, which moves 0.9 dB). Rebuilt,
  `--check` passes with the native strum guards: every cell and capture
  within 1 LU, every Pick cell at least 1 dB under the limiter's knee on the
  microphones (the piezo's 5/12, within the 0.9 LU it may give up).
  Most of the drift the new tables absorb was already on a6f1ad8. With the
  same tool and the same (main's) tables, the cells as rendered spread about
  the default construction, in LU (median): on a6f1ad8, stereo microphones
  -0.44 to +2.36 (+0.84), mono +0.11 to +2.71 (+1.40), piezo +0.10 to +1.52
  (+0.90); on `296924c` before the regeneration, -0.40 to +2.39 (+1.06), +0.18
  to +2.77 (+1.66) and +0.81 to +1.75 (+1.31). This work adds 0.2-0.4 LU to
  the medians, most on the piezo. The fit corpus renders are byte-identical
  with the new tables. The integrator regenerates them after merging.
- RetuneContinuity, StringPitchRealism, StringDecayRealism and
  RepeatedPluckRealism snapshots carry the new state (twelve intrinsic
  coefficients, the sections' histories).

## 8. Reproduction

Build (this tree; GCC 13.3, Release):

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF \
  -DACUSTRA_BUILD_TOOLS=ON -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE=<python with numpy/scipy> -DACUSTRA_REQUIRE_PYTHON_TESTS=ON
cmake --build build-dsp --config Release --parallel 2
```

Corpus and scores (W and M from their own revisions' renderers with their own
shipped values; B's 24 values below; C's with slot 23 `0.00045`):

```sh
./build-dsp/AcustraPhysicalFitRenderer OUT 1.0 1.0 0.754677154 0.0 0.749355465 \
  1.53 0.52 0.4883279315 2.2130696796 1.8 1.10625 -0.0706290118 4.0 \
  0.00773577847 -0.0597851562 2.28586032 0.011 2187.76023 0.00325 0.58203125 \
  0.85859375 0.0001162109375 0.00013 0.002334375
# later variants: the same with --models-only before OUT
python3 <integration-branch>/Tools/FitPhysicalModel.py OUT/train.json   # matched
python3 Tools/FitPhysicalModel.py OUT/train.json                       # mid
python3 Tools/AuditDampingCurve.py OUT
```

The song sets (section 9), each from two frozen trees:

```sh
python3 Tools/BuildRealismListeningTest.py \
  --baseline-source <a6f1ad8 tree> --candidate-source <this tree> \
  --baseline-commit a6f1ad8e8ad383d6b8d5f613e3243fb14189fede \
  --candidate-commit a5cfa55ac849476d5958e85a83043bfb5fd6ee9e --out <new folder>
```

The scratch diagnostics behind sections 1, 5.3-5.5 (not committed) read the
same corpus renders or default-control renders of single notes with the
engine's test access: per-partial decay with AuditDampingCurve's admission in
early and audit windows; the 2-6 kHz / 0.1-1 kHz level slope from 100 ms Hann
frames; per-plane renders that empty one loop after the pluck; the analytic
per-plane budget from the configured loops and `bridgePortMobility`; and the
decomposition, a copy of the merged source with environment switches for each
of the four changes. Their numbers are quoted with their windows above.

## 9. Listening sets

Nobody has listened. The sets are in the session's scratch space,
`scratchpad/listening/wp-a/` (not in the repository), with a README that
names the labels, binaries, controls, every trim and the questions below, and
a manifest per set. All are 48 kHz stereo; each passage is gain-matched
across its variants with one scalar gain per file to a common BS.1770
loudness (ffmpeg's loudnorm measures; -20 LUFS, lower where a file needs 2.5
dB of true-peak headroom); raw float renders are kept at native level.

| Set | Variants | What |
|---|---|---|
| `notes-M-W-B-C` | M, W, B, C | AcustraPerformanceRenderer, Dreadnought, Spruce, Room 0: open strings then E5 and B5 (Finger; Pick), the wound strings at the 5th and 12th frets, held E, Am, C and G chords, open strings on the piezo |
| `gestures-M-W-B-C` | M, W, B, C | the repository's eight audible-realism passages (Pick strums with fast soft returns; slid attacks with Finger, Pick and Thumb), dry and Room 0.5 |
| `songs-W-vs-B` | W, B | `Tools/BuildRealismListeningTest.py`, five songs, dry and Room 0.5, blind |
| `songs-B-vs-C` | B, C | the same, B against C |

Each variant renders with its own build of its own revision (C's own build
renders exactly what B's renderer does with `steelWoundFrictionLoss` set to
4.5e-4, checked sample for sample). Native levels, B against W: -0.8 to +0.3
dB on the microphones (B 0.5-0.8 dB quieter on the Pick passages), 0.8 dB
quieter on the piezo, as the regenerated tables lower its trim 0.9 dB.

Questions for the ear, not findings:

- W against B: does B's continuing fade of the upper partials (section 5.4)
  sound more natural, and does "the tone stays too static as it fades" still
  hold? Is B too bright on the wound strings at 1.5-4 kHz, as the benchmark's
  body term says against the recordings (section 5.1)?
- B against C: the wound strings' early "snap then mellow" (C) or the
  recordings' per-partial decay (B)?
- E5 and B5 (notes 01 and 02): their fundamentals decay 3.9-4.7 dB/s slower
  on B, A4 3.5 dB/s faster (section 5.5). Is the top string's sustain still
  right?
- The piezo (notes 05): the bridge's real conductance at 5-7 kHz is now
  2.0-3.4 times the Fylde's measured one, where it was 7.2 times (section 1).
  Does the pickup sound change with it?

## 10. Limits, risks and open questions

- **The benchmark is worse.** B scores 3-10% worse than W, almost all of it
  the body term from the floor's scaling. The physics says the unscaled floor
  over-damped the top; the corpus says the model is then too bright at 1.5-4
  kHz on the wound strings. If the listener hears B as too bright, the next
  lever is the excitation or capture brightness, not the floor's scale.
- **A by-ear choice departs.** B does not keep the listener's 0.035 "snap
  then mellow" on the wound strings: their first 0.3 s decays at 26 dB/s
  (geometric mean, 0.5-4 kHz) where W's did at 37. C keeps it and measures
  as B does on the plain strings. B against C is the question for the ear.
- **The floor's level stays by ear.** At steel level the floor is still about
  7 times g21's measured missing conductance; the piezo's bridge reads 2-3.4
  times the Fylde's.
- **The corpora disagree on the friction.** The fitted splits are flat over
  1.3-1.8e-4, the flat-top rows prefer less, the evaluation's picked-Eastman
  band reading suggested 0.7-1e-3. Per partial, B sits between the archtop
  and the flat top.
- **Sustain at the top.** The top string's E5/B5 fundamentals decay 3.9-4.7
  dB/s slower as their planes beat, A4 3.5 faster.
- **The hand-back steps.** The Release fixture's bound moved (section 7) because
  the low E no longer filters the hand-back's own steps. They were there
  before on the D and high E strings (25-29 dB on a6f1ad8); a smoother
  switch-out of the second dispersion section and a slower slew for an idle
  hand-back would take the low E's to 19-23 dB, but that is retune code
  outside this work.
- **Two fixtures in other areas moved:** the room ring-out bound (WP-G's) and
  the construction loudness tables (regenerated by their tool; most of the
  drift they absorb was already on a6f1ad8, section 7).
- **Not measured:** CPU with the paired protocol; plug-in formats (no JUCE
  build in this work package); listening.

## 11. The blend: winding friction 4.5e-4, awaiting its A/B

### The listening

On 2026-10-10 the user compared B (`30a0bd5`) with main (`0adcfd2`) blind:
five songs and five passages, dry, each pair matched by one whole-file gain.
They approved B overall. They preferred B on two songs (the fingerpicking
and the repeated-note groove) and on the piezo passage. They preferred main
on the strumming song, on the open strings fingered and picked, and on the
wound strings, and called the held chords even. Their comments, with the
blind labels resolved:

- open strings, fingered: "A is too bright but it might sound better if more
  blended with B" (A was main);
- open strings, picked: "A is too much" (A was B);
- wound strings: "B is too much" (B was B);
- held chords: "both are good".

Asked to choose, they took a blend between main's brightness and B's, then a
short blind A/B of the open strings, the wound strings and strumming.

### The blend

Winding friction 4.5e-4, C's value, puts the open and wound passages'
brightness about halfway from main to B. Brightness here is band power at
1.5-4 and 2-6 kHz over 0.1-1 kHz, at the attack (0-150 ms) and in the
sustain (0.3-1.5 s), averaged in dB over the passages' notes.
- **C** (the calibration on B's binary): 0.48, 0.59 and 0.44 of the way,
  on the fingered open strings, the picked open strings and the wound
  strings.
- **The committed blend (`a3057dd`),** with the corner set below: 0.47,
  0.58 and 0.44. The attacks sit 0.48-0.71 of the way and the sustains
  0.17-0.54.

`FittedPhysicalData.h` quotes C's 0.44-0.59. The
construction loudness tables were regenerated by their tool. That run was
unconstrained, since the default's piezo trim moves. `--check` passes with
the native strums, on the source with the corner set below.

### Three suites failed, and the fixes

Each failure was diagnosed against B. A copy with only the friction value
changed reproduces every failing number, so the regenerated tables play no
part.

1. **Engine, the constant-loss law: a fit limitation that predates the
   blend.**
   - On bands of two decades (the wound strings at higher friction or
     String Age), the corner set's least-squares fit left an unbalanced
     error.
   - At 1.3e-4 it already missed by 20.6-23.8% at Ages 0.3-0.7, which the
     test did not sample. At the default Age, any friction from about 3e-4
     to 8e-4 missed.
   - The blend's worst was -24.6%, on MIDI 40's 26th partial at 96 kHz:
     24.7 dB/s where the law asks 32.7.
   - The corner set is now {-0.25, 0.10, 0.15, 0.20} × {0.90, 0.95, 1.00,
     1.05, 1.50}, still 20 pairs. Over Ages 0-1 the worst is 19.2% at 4.5e-4
     and 18.6% at 1.3e-4 (24.6% and 24.8% before). Two relaxations can't do
     better than about 16.5% on the failing band.
   - The test now also samples Ages 0.3, 0.45 and 0.6: 32,238 partials,
     worst 19.16%, and under 0.69 dB/s below the band.
   - A denser set of 104 pairs fitted closer but was rejected. It tripled
     the six-string slide callback, to about 700 µs at p95, over the 667 µs
     budget at 96 kHz, and it failed StringDecayRealism.
   - The new set changes every string's loss slightly. The A/B renders
     include it.
2. **Capture, the piezo clip's aliasing: the test's drives, not the
   circuit.**
   - The default-Touch drives (1.25-1.45) sat just past where that strum
     first reaches U1B's swing.
   - The blend's strum is less peaky. Its deepest swing at unit weights fell
     from 0.96 and 0.95 of the swing to 0.78 and 0.72 (44.1 and 48 kHz), so
     at drives of 1.25-1.35 it never clipped, and the test's precondition
     failed.
   - The drives now start where both rates clip: 1.40-1.60, which is 3-7
     host samples past the swing at 44.1 kHz and 1-9 at 48 kHz.
   - The aliasing reads -64.1 to -68.2 dB and -64.8 to -81.1 dB, with means
     5.1 and 6.3 dB under a bare clip. The bounds are unchanged.
   - The headroom promise holds with more room. The hottest reference strum
     falls from 2.07 to 1.83 V open-circuit, and the least headroom rises
     from 0.20 to 0.62 dB.
3. **RepeatedPluckRealism, String Age automation: a real regression.**
   - Per 0.001 of Age, the wound strings' loss angle now moves 3.7e-6, where
     it moved 1.15e-6 at 1.3e-4. Their section also carries more of the wave:
     1 - g is 0.185 against 0.078.
   - An Age change jumped the section's coefficients, so each 0.001 step
     moved the merged G2 by 2.4-2.8e-5 of full scale, about -91 dBFS and
     five times B's. That is 3.1-3.6e-4 of its peak, against the 2.7e-4
     bound.
   - String Age now glides (`configureVoice`'s continuous revision). An Age
     change starts a one-round-trip transition of the loop's loss and
     intrinsic sections when none is running. When one is running, it
     revises that transition's target within the remaining deadline.
   - So a host automating Age every block never postpones the glide, and a
     retune still restarts it.
   - The single step now reads 1.0e-4 of the peak, about 8e-6 of full
     scale.
   - A new test automates Age every 64-sample block, at 0.0003 a block (the
     whole range in 4.4 s). It saw 12 glides and 84 revisions. None moved
     its deadline, and all ended exactly on the last target within one round
     trip.

### The A/B renders

The blind page compares main (M, `0adcfd2`) with the blend (D, `a3057dd`).
It sits in the session's scratch space, `wpa-listen/ab-blend/`, with its
manifests:
- **Song:** the strumming song, `Tools/BuildRealismListeningTest.py
  --track 02-open-road-strumming`. Each revision's DSP was verified byte
  for byte against its commit and compiled directly.
- **Passages:** the fingered open strings, the picked open strings and the
  wound strings, from this report's listening tool (section 9). The
  renderers' SHA-256 prefixes are `0f3818a6` (M) and `d77724cc` (D).
- **Mode and matching:** dry only. Each pair is matched by one whole-file
  gain.

### Checks and limits

- **Suites:** the full JUCE-free Release suite passes on `a3057dd`, all 71
  CTest suites, with the Python tool tests required. `AcustraDSPCxx17`
  builds.
- **Benchmark:** FitPhysicalModel's figures in sections 4-5 are B's, and
  C's row in section 4 was taken on the package's base. The blend on the
  merged base, with the new corner set, has not been re-scored.
- **CPU:** the corner set's cost is unchanged by construction, still 20
  pairs. One unpaired, indicative run put the slide callback's p95 at
  208-281 µs, against 217-282 µs before. That is not the paired protocol.

## 12. Picked notes keep main's top end: half the friction, half the bending loss

### The listening

The user heard the blend (D, `a3057dd`) blind against main (M, `0adcfd2`):

| Passage | Preferred | Naturalness | Comment |
|---|---|---|---|
| Open strings, fingered | M | 5 / 5 | "very close" |
| Open strings, picked | M | 6 (M) / 3 (D) | "B absolutely not for this one" (B was D) |
| Wound strings | D | 5 / 5 | "both very close" |
| Strumming song (Pick) | D | 5 / 5 | "both sound a bit robotic" |

They asked for a revision: "something between A/B", with "baseline for
picked".

### What made the picked notes bad

On the picked open strings, D's wound strings (E2, A2, D3, G3) rang at
4-16 kHz far above M's, relative to each note's 0.1-1 kHz:
- +16 to +27 dB at 50-400 ms;
- the low E's 2-4 kHz +16 to +23 dB over 0.15-1 s.

The plain strings differed by under 2 dB in the same windows. The cause is
the loss law:
- M's wound strings lost their top through bending, whose rate grows as the
  cube of frequency until B n^2 nears one.
- The friction that replaced it grows only linearly.

A finger excites little above 4 kHz, which is why the fingered passages
were close.

### The fix

The wound strings now carry both mechanisms:
- the winding friction on the tension (constant section);
- a bending loss of their own, `steelWoundBendingLoss`, in the bending
  section in place of the plain steel's.

Both age together. Physically, a wound string's turns slide on one another
as it flexes, and the recordings' 20-300 ms decay measures its bending loss
at 0.10 against plain steel's 0.006.

A probe engine scanned the pair. Positions run from M (0) to D (1) in
1.5-6 kHz brightness. The picked figures are D's wound strings (MIDI 40-55)
against M.

| eta_W, delta_W | Fingered position | Picked position | Picked 4-16 kHz, 0.05-0.4 s | Picked 2-4 kHz, 0.15-1 s |
|---|---:|---:|---:|---:|
| 0.035, 0 | +0.15 | +0.11 | -7.5 dB | +0.6 dB |
| 0.025, 2.25e-4 | -0.16 | +0.14 | -4.8 dB | +1.3 dB |
| 0.015, 3e-4 | +0.39 | +0.52 | +1.1 dB | +3.4 dB |
| **0.0175, 2.25e-4** | **+0.50** | **+0.53** | **-0.1 dB** | **+3.3 dB** |
| 0.02, 2.25e-4 | +0.27 | +0.39 | -1.8 dB | +2.6 dB |
| D: 0 (core 0.0023), 4.5e-4 | +1.00 | +1.00 | +16.0 dB | +5.9 dB |

The chosen pair is the midpoint of both mechanisms: half M's bending loss
and half D's friction.
- **Picked wound strings:** their 4-16 kHz over 0.05-0.4 s matches M within
  0.1 dB. Per note, E2's and A2's 4-8 kHz still read +5 to +7 dB at
  50-150 ms and fall under M's after.
- **The 2-4 kHz band:** sits halfway, as it does on every technique.
- **Brightness:** the fingered open strings sit 0.50 of the way from M to D,
  and the wound passage 0.60.

M's full bending loss, with or without friction, overshoots: the dislocation
angle and the other changes of sections 1-3 already darken the top, and the
picked wound strings fall 7.5-8.2 dB under M's 4-16 kHz.

No single loss law gives the pick exactly M's spectrum and the fingered
notes D's middle. Either the friction or the bending loss would have to know
what plucked the string. The string loss here is the string's alone.

### Checks

See the commit and its pull request for the full suite, the regenerated
construction tables and the A/B renders.
