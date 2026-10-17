# Physical model evaluation: body, wood, microphones and piezo — 2026-10-10

Scope: the steel-string instrument after the Bellido 1978 removal, i.e. the
Original body (g21 morphed by Shape, Body Material and the steel blend), the
six steel strings as they load and drain into that body, the Stereo/Mono
microphone capture with Room, and the under-saddle piezo with Piezo Mix. The
user asked for issues, improvements and missed phenomena. Nothing here has
been implemented; every proposal needs its own matched before/after evidence
and, where it changes sound, a listening decision (CLAUDE.md).

Method: four independent code/physics reviews (body and wood; strings and
their coupling; microphones and room; piezo), each with light numerical
probes, followed by spot verification here. The Original's code paths render
byte-identically before and after the Bellido removal (removal record), so
probe numbers taken on the pre-removal library apply unchanged.

Evidence labels used below:

- **[V]** verified in this session: read in the current code, or reproduced
  from a fresh render.
- **[P]** measured by a review probe linked against the pre-removal library
  (throwaway, not committed). Indicative; reproduce before acting.
- **[I]** inferred from physics or literature; needs a measurement.

## The ten findings that matter most

Ranked by expected audible weight; the first column is the finding's number
in the sections below, which the cross-references use.

| Finding | Issue | Area | Evidence | Expected audible weight |
|---|---|---|---|---|
| 1 | The saddle-to-anchor stub is a spring to ground, creating false string drains above A0 and T1 | strings/body | V (code), P (numbers) | High |
| 11 | The capture contour was fitted on the L/R average of a spaced near-field pair, whose 1.25 kHz band cancels | microphones | V (mechanism, −8.5 dB) | High |
| 6 | High-frequency decay is set by an unscaled, heave-only conductance floor | strings/body/piezo | V (values), P | High |
| 7 | Wound-string friction follows a bending (≈f³) law instead of constant Q | strings | V (code), project data | Medium–High |
| 2 | Plate-mode level across Shapes scales the wrong way (peaks ∝ area²) | body | V (code + derivation) | Medium–High |
| 12 | The ×4 air-mode gain scales single modes and undoes the bridge microphone's cancellation | microphones/body | P | Medium |
| 4 | Body Material moves every mode of the top and cavity; back/side wood is not represented | wood | V (code), I | Medium |
| 13 | Room: the late field starts at ≈25 ms after early taps end at 17 ms; it is fed the cancelling mid | room | V (code), P | Medium |
| 15 | The piezo's saddle resonance assumes 1.59e-3 s/kg while its strings see ≈0.011 s/kg | piezo | P, V (constants) | Medium |
| 19 | Three control smoothers settle on subnormals (≈6.7e-43) when moved to zero | engine | V (code + arithmetic) | CPU only |

## Body, Shape and wood

### Verified and strongly supported issues

**1. The anchor stub acts as a ground spring (High).**
`bridgePortMobility` combines the body mobility with the six tail stubs as
`(Y⁻¹ + K/s)⁻¹` (`AcustraEngine.cpp`, "Body and anchor are in parallel at the
saddle") [V]. With `bridgeTailLengthMetres` at its 3.25 mm floor
(`FittedPhysicalData.h`), K = ΣT/L ≈ 220 kN/m. Above each body resonance the
body is mass-like, so `sM + K/s` resonates there: the string-side conductance
peaks 1.8–3.1 semitones above A0 and 2.9–4.3 above T1, 8–23× the bare
bridge's on the Dreadnought and up to 34× on the Jumbo, against the
project's own <3× gate of 2026-09-02 [P]. A pin bridge carries both ends of the
saddle-to-pin segment, so no such ground spring exists; the decision log
already calls one "double-counted geometry".

The 3.25 mm value was pinned back from the fit's 59.1 mm because of *nylon's*
late chord pull (8.7 vs 0.8 cents) [V, `FittedPhysicalData.h`]. Nylon is
retired. At 60 mm on the current bridge every Shape passes the 3× gate and
steel chord pull is no worse (worst 7.0/6.3 vs 7.8/8.1 cents) [P]. Renders
with the stub show G2–G#3 blooming 5–6 dB and decaying at 17–19 dB/s, E2/D3/E3
6–11 dB weak in the attack, and E2's fundamental 6–14 dB under its H2/H3 —
about the deficit the ×4 air gain was later chosen by ear to cover [P].

Proposal: remove the heave/rock ground spring (keep at most a lateral
crown-to-pin term), add the port-conductance gate as a test, then re-audition
the choices made on top of the stub (air gain ×4, spring-back 0.8, wide
anchor). Caveat: with drains aligned, the wide anchor's A0 (84.7 Hz) sits
47 cents above E2 and E2 decays at ≈20 dB/s against 4.5–8.6 dB/s in the
recordings [P]; the stub and the low A0 currently compensate each other, so
test a consistent pair (no ground spring and A0/T1 near the published
≈95–100 / ≈190 Hz) in a new blind set rather than reverting a listener choice.

**2. Plate-mode level law (Medium–High).**
`bodyShapeMorph` sets `plateFrequency = 1/areaRatio` and
`plateLevel = areaRatio` [V]. For a fixed-thickness top, modal mass ∝ A and
∫φ dA ∝ A, so a mode's peak pressure per unit force is independent of size;
with the engine keeping each mode's continuous residue when it moves a pole
(peak ∝ residue/ω), a size-neutral peak needs residue ∝ 1/A. The shipped law
gives peak ∝ A²: every mode above T1 is about −4.7 dB on the Parlor, −2.0 on
the Auditorium and +1.6 on the Jumbo relative to physics, so small bodies come
out darker and large ones brighter (Parlor −2.0/−3.7 dB at 0.5–2/2–5 kHz) [V
derivation, P numbers]. The bridge side keeps residues fixed (mass unchanged),
which is inconsistent with the coupled A0/T1 model's own mass scaling.
Proposal: `plateLevel = 1/areaRatio`; scale plate-mode bridge residues
(heave ∝ 1/A, rock ∝ 1/A², cross ∝ A^-1.5, all PSD-preserving); regenerate
the loudness table. No CPU.

**3. Absolute A0/T1 placement (Medium).**
The documented steel results are A0/T1 = 111/184 (Parlor), 96/171
(Auditorium), 85/159 (Dreadnought), 76/146 Hz (Jumbo) [V, README]; published
air resonances run about 110–125, 100–115, ≈95–100 and <90 Hz with a
dreadnought T1 near 190 Hz. The spread between sizes is right; the level
sits ≈10 Hz low because of the wide anchor chosen by ear while the stub
(finding 1) was in place. Depths also mix conventions (mean depth on the
D-28, tail depths elsewhere); mean depths raise T1 3.8–5% [P].

**4. Body Material (Medium).**
`woodSpecs` multiplies every radiation and bridge mode — A0, T1 and the plate
modes — by one frequency/Q/brightness/radiation factor (±1–2.5% frequency)
[V]. On a steel-string guitar the wood choice usually names the back and
sides under a spruce top; their audible effect is concentrated in the back
resonance and its coupling below ≈400 Hz, and blind tests on matched guitars
found little perceptual effect (Carcagno et al. 2018, the Fylde source) [I].
As a top wood the numbers would also be off (mahogany/maple tops have ≈17–20%
lower sound speed and 4–6 dB lower radiation ratio c/ρ than spruce, and
maple's Q ×1.08 has no material basis) [I]. Proposal: make Body Material the
back/sides through a back-plate oscillator (missed phenomenon 1), or relabel
it as an abstract voicing.

**5. Smaller items.** The Q ≥ 4 floor plays g21's measured Q 2.47 mode at
287 Hz with a 4.2 dB higher peak in the band the capture contour then cuts
[P]. The anchor transform's index-parity detune splits g21's and the joint
body's two air modes (90.8 and 90.3 Hz measured) to 62 cents apart [P];
key it by frequency or twin. The capture contour is fixed in frequency and
partly cancels each Shape's A0/T1 move (it was fitted on dreadnoughts) [V].

## Strings and their coupling to the body

**6. High-frequency loss rests on an unscaled, heave-only floor (High).**
The plate conductance floor ships at 0.011 s/kg (`FittedPhysicalData.h`) [V].
The measured modal residues are scaled by 0.755 × 0.274 to steel level; the
floor is not, and it is ≈7× both g21's missing conductance and the Fylde's
1.59e-3 s/kg [P, V for the constants]. It enters as heave alone, and the
measured rocking residues stop at 2.2 kHz, so the parallel plane — 75% of a
default pluck — sees ≈0 dB/s of bridge loss above ≈2 kHz while the normal
plane sees 9–32 dB/s from the floor [P]. Renders: the parallel plane decays
17.8–20.2 dB/s in 3–6 kHz against 27–30 for the normal plane and dominates the
late upper partials [P]. The floor's decay rate also grows as 1/L up the neck
[I]. Proposal: scale the floor (or replace it with g21's missing 1.5e-3 ×
0.274), give it a PSD rocking residue, and move the remaining high-frequency
loss into the strings' own law (finding 7).

**7. Wound-string friction law (Medium–High).**
`bendingLossRate` is πf·η·Bn²/(1+Bn²) [V]; wound-string grime loss is applied
through it, so it rises ≈f³ at low partials. Winding friction acts on tension
(Valette; Paté, Le Carrou and Fabre, JASA 135 (2014)): a constant loss angle,
≈f. The project's own Known-gaps numbers for the Eastman wound strings (86,
153, 300 dB/s at 2–4, 4–8, 8–12 kHz: ×1.8 then ×2.0 per octave) fit a
constant Q ≈ 900, while the shipped law gives ×4–6 per octave and an 8:1
spread across strings at 2.8 kHz [V numbers from README, P engine rates].
Proposal: σₙ = πfₙ[δ_W + η_B·Bn²/(1+Bn²)] in the same section (no CPU), δ_W
≈ 0.7–1e-3 on wound strings scaled by String Age.

**8. Authored losses set most of the decay (Medium).**
The loop gain targets one T60 (≈8.1 s) for every string and partial, and the
bridge contributes 0.4–1.3 dB/s of each open string's fundamental decay
against 7.8–8.8 dB/s of the strings' own [P]. Valette's law with physical
inputs predicts per-string fundamentals of 3.0–7.5 dB/s that differ by string
[I]. With the body's share this small, decay contrast between partials on and
off body modes is compressed, consistent with the measured "ring gain" of
3.8/4.5 dB against the recordings' 6.9–8.5 [P]. The per-plane factors
(normal 0.9995, parallel 0.9988 per round trip, deeper parallel shelves) are
documented as authored [V]; a string's intrinsic loss is the same in both
planes (Paté §III.D), so any plane difference should come from the nut, fret
or finger instead.

**9. Inharmonicity scale (Low–Medium).** `stiffnessScale` 0.749 puts plain
strings 16–29% under their geometric B (B3's H12 stretch 5.6 instead of 7.5
cents) [P]. Fix plain strings at the physical value and re-measure wound B.

**10. Delay glide stops short (Low).** The float glide stalls up to 2.25
samples short at 768 kHz (+0.42 cents) after an attack glide [P]; snap within
1e-3 sample or run it in double.

## Microphones and room

**11. The capture contour fit observes a cancelling mono sum (High).**
`Tools/FitCaptureVoicing.py` (and the physical-fit and benchmark tools) score
`model.mean(axis=1)` [V]. The two microphones are near-field omnis 20 cm
apart; on g21's 1.23–1.37 kHz plate modes their residues are 130–180° apart.
On the default Dreadnought's strummed E chord the 1.25 kHz third octave
correlates at −0.79 and the L/R average loses 8.5 dB against each channel [V,
fresh render]. Fitting that average to coincident-pair or single-mic
references therefore boosts the band: each channel ends up ≈+5–6 dB hot at
1.25 kHz at default Width and the Mono mic ≈+7–9 dB [P], consistent with the
known "Finger attack too bright" gap. Proposal: score per-channel power for
stereo references and the Mono mic against single-mic references, report
mono-sum retention separately, refit with a separate Mono contour, then
blind-A/B against the accepted voicing.

**12. The ×4 air-mode gain scales single modes (Medium).** It multiplies the
85–145 Hz modes' left-microphone residues (`configureBody`) [V], the same
defect class fixed for the contour on 2026-10-08: below A0 the bridge
microphone's A0 tail cancels against T1 and the 287 Hz mode. Left E2 H1 rises
+8.9 dB on the Dreadnought (where it was chosen) but +18.9 on the Parlor, and
the moment path gains notches (−15 dB at 110 Hz on the Auditorium) [P]; the
hard window also splits the joint body's two A0 modes. Proposal: a filter on
the summed left pressure tracking the configured A0 (frequency and Q from
`radiationModePole`), matched to today's Dreadnought E2, re-auditioned
across Shapes. Reconsider together with finding 1, which removes most of the
E2 deficit the gain was compensating.

**13. Room (Medium).** [V, code] The late field is read from FDN lines of
17.3–41.3 ms fed 8 ms after input, so it starts ≈25 ms, while the early taps
end at 17 ms; 48% of late energy then arrives in 25–50 ms [P]. It is driven
by the coherent mid of the two near-field microphones, inheriting their
antiphase holes (B3's 1.25 kHz partial is 31 dB down in the room while
present in each dry channel) [P]. Late L/R coherence is 0.1–0.5 in every band,
where a 20 cm pair in a diffuse field gives 0.97/0.87/0.53 at
125/250/500 Hz; side reflections carry up to 7.8 dB ILD and no ITD [P]. The
absorption one-poles run to the internal Nyquist, so 8 kHz T30 is 0.254 s at
48 kHz and 0.297 s at 192 kHz [P]. Proposals: start late output from the
diffused input (or line fractions) at ≈8–10 ms with a matched envelope; feed
L and R on orthogonal input vectors; high-pass the wet side and time-pan
side taps; design the absorption filters rate-independently.

**14. Room as distance (Low).** Room raises reflections but keeps the
0.32 m reflection geometry and leaves the 10 cm direct sound unchanged; fine
as a send, misleading as "a microphone well out in it" [P].

## Piezo

**15. Two conductances (Medium).** The saddle filter's damping uses the
Fylde's G = 1.59e-3 s/kg (`PiezoDesign`), 73% of the saddle's damping; the
bridge the same strings load shows Re Y ≈ 0.011 s/kg over 5–7 kHz, set by the
floor of finding 6 [P, V for the constants]. With 0.011 the saddle resonance
would be Q 0.62 (+1.5 dB at 4.3 kHz) instead of the shipped Q 3.26 (+10.6 dB
at 5.85 kHz). Not a code bug: decide which conductance is physical (finding 6
suggests the floor is not), then document or A/B. Listener history: Set 22
chose the current sound; Set 20's note asked to soften it.

**16. Released static force (Medium).** The release step drives the bridge
but deliberately not the piezo sum, "its preamp's headroom was set without
it" (`process`, 2026-09-30) [V]. A step on the saddle crown passes through the
element. Rebuilt, it adds 20–60 Hz energy 6–12 dB under the attack and costs
0.4–1.7 dB of headroom; the worst reference case keeps 0.17 dB and nothing
clips [P]. Worth a re-trim and a blind A/B now that headroom is 1.9 dB on
player strums [P].

**17. Smaller items.** The force derivative x[n] − x[n−fs/48k] droops 0.6 dB
at 10 kHz and 2.6 dB at 20 kHz, uncompensated in the analog-designed saddle
filter (fold |jωτ/D| into `designPiezoSaddle`'s target) [P]. U1A can clamp
before U1B below ≈41/107 Hz (positive/negative peaks), contrary to comments;
unreachable in normal play [P]. README's axial-force sentence describes a
removed path, and its 20 Hz and headroom figures are stale [P]. The 3 kHz
aliasing gate passes by 0.23 dB [P].

**18. Parallel-plane leakage (plausible).** The piezo hears the parallel
plane −44…−52 dB under the normal one in the attack, correct for a uniform
strip on a rigid seat [P]. Real saddles lean and seat unevenly; a −20…−30 dB
leak would change Capture Piezo's decay and its Touch response. Needs a
recording with controlled pluck direction; the model is a gradient × (h/a) ×
F_parallel term per string.

## Engine housekeeping

**19. Subnormal smoothers (CPU).** `bodyAmount_`, `width_` and `outputGain_`
glide without the snap rule `piezoMix_` and `roomAmount_` use [V]. Moved to
zero they stall where k·α < ½ ulp, k < 0.5/α ≈ 480 at 48 kHz, i.e. at
≈6.7e-43 [V arithmetic]; the review measured ≈17% slower processing without
FTZ/DAZ [P]. JUCE's `ScopedNoDenormals` hides it; the Rack and offline tools
may not. Use the existing snap rule.

## Missed phenomena, ranked by audible value against cost

1. **Top–air–back three-oscillator body.** The back's first mode
   (≈200–260 Hz) couples through the cavity, moves A0 and radiates; it is
   what Shape and back wood change most. A 3×3 eigen-solve in
   `coupledLowBodyPair`, one radiation pole and one PSD bridge residue; <1%
   CPU. Avoid double-counting g21's own back modes. Gives Body Material a
   physical meaning (finding 4).
2. **Neck and fret admittance (dead spots).** On guitars the neck's
   conductance at the fret can exceed the bridge's by far (Paté et al. predict
   G3 dead spots at frets 9/12 from Re Y_neck). A per-note reflection biquad
   from 3–5 neck modes at the fret position; ≈0 CPU; replaces the fitted fret
   T60 slope (which sits on its bound and runs the wrong way, +72% T60 at the
   12th fret). Needs a flat-top neck measurement.
3. **A physical saddle termination** (finding 1) with A0/T1 re-placed to
   published values and radiation damping following Shape (radiation
   resistance ∝ area²). No CPU.
4. **Microphone distance and pattern.** Directional patterns (proximity
   shelf ∝ 1/(kr), ≈180 Hz at 0.3 m; ≈4.8 dB less diffuse room), distance-
   driven reflection taps and direct-to-reverberant ratio from the critical
   distance (≈0.6–0.9 m here), and air absorption. Low CPU; medium risk,
   since g21's 10 cm near-field data do not extrapolate cleanly.
5. **The player's body on the Original.** Torso/forearm damping of the back
   and shadowing of its radiation. The removed classical-only loading law
   was authored, not measured; the Original's fit already absorbs an unknown
   player load, so this needs a held/free measurement first.
6. **Tension-modulation axial force at the saddle, piezo only.** The
   under-saddle "zing": slope² × EA/(2L) × sin(break angle), optionally
   through 1–2 longitudinal resonators (low E ≈1.4 kHz). A piezo-only tap
   leaves strings and microphones untouched (the 2026-09-04 rejection was of a
   force fed into the junction). ≈20–40 flops per string per sample; level
   needs a measured under-saddle pluck (quasi-static estimate: 2f force
   −16…−10 dB under the fundamental for 1–2 mm plucks [I]).
7. **Constant-Q per-string loss law** (findings 6–8): air + dislocation +
   winding friction, with the broad loss filter retired. No CPU.
8. **Cavity modes A1/A2** scaling with body length/width (c/2L) rather than
   the plate law. Zero CPU; needs the bank's cavity modes identified.
9. **Small-room low modes** below the room's Schroeder frequency (≈180 Hz):
   4–6 resonators. <1% CPU; risk of boom on particular notes.
10. **Piezo product controls**: a polarity switch for Piezo Mix (the
    mic/piezo correlation is −0.95 at 79 Hz and −0.8…−0.9 at 198–315 Hz on the
    default Dreadnought, so the blend digs −2…−7 dB holes; inverted it adds
    3 dB with no hole below 1 kHz, though the Parlor prefers today's
    polarity) and an optional 1 MΩ passive input load (the familiar thin
    ≈86 Hz corner) [P].
11. **Smaller string effects**: pitch rise from pressing the string to the
    fret (a few cents at low frets), and false beats from a small B
    difference between planes growing with age.

Not recommended: re-adding longitudinal/torsional string waves to the
junction. The axial path was removed on 2026-10-09 as shipping-zero; it
would need a measured break angle, a bridge pitch degree of freedom the
heave/rock bridge cannot take, and a physical displacement scale.

## What is right and should stay

- The Christensen–Vistisen top/air algebra (sum/product identities,
  eigenvectors, V/A_p forcing); an independent replication matches the
  engine's poles to 0.01 Hz, and the morph is the identity at the anchor [P].
- The fixed-thickness frequency law and the Helmholtz end correction, used
  consistently across sizes.
- A passive bridge: PSD residues, positive damping, prewarped bilinear modes,
  non-negative blend weights; idle strings are junction members; per-string
  lever arms and the (h/a)² parallel port.
- The string set: EJ16 tensions within 0.3%, impedances and masses
  consistent, wound diameters reproducing Järveläinen–Karjalainen's B; a
  passive loop (Thiran fractional delay, loss sections ≤ 1); partials H1–H12
  within ≈0.7 cents of the stiff-string law; rate consistency within 1–5%
  from 44.1 to 384 kHz; physically sound attack glide and Grimes bends [P].
- The capture contour as a causal double-precision filter on summed
  pressure (RBJ coefficients check out; ±0.08 dB from 44.1 to 384 kHz), per-
  bank filter histories through fades, one 7-sample time base for
  microphones and piezo, the mid/side Width law, and no proximity effect in
  the dry path, correct for omni pressure microphones [P, V].
- The piezo circuit matches ESP Project 202 Fig. 1 part for part; the saddle
  filter derivation is correct (independent design within 0.57 dB at 44.1 kHz,
  0.001 dB at 384 kHz); trapezoidal/bilinear discretisation and BLAMP
  antialiasing are correct; construction dependence is weak, as it should be
  for a saddle-force sensor [P].
- The room is energy-normalised, decimated to ≤64 kHz, lossless-FDN based, an
  exact no-op at zero, and never reaches the piezo [P, V].

## Suggested order of work

1. Finding 19 (snap the smoothers): mechanical, no sound change except at
   exact zero targets.
2. Findings 1, 3 and 12 together (physical termination, A0/T1 placement, the
   air gain as a filter): one coherent body/termination candidate, matched
   renders against the recording benchmark, then a blind set.
3. Findings 6–8 (strings' own loss law and the floor's scale/rocking
   residue), refitting the fret slope afterwards.
4. Finding 11 (capture fit observation), then re-level constructions.
5. Finding 2 (plate level law) and missed phenomenon 1 (back plate / wood).
6. Room fixes (finding 13) and the piezo items, each with its own A/B.
