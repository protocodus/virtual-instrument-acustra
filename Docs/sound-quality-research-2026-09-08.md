# Sound-quality research audit — 8 September 2026

The strongest immediate experiment is **joint identification of string loss and
dispersion at the actual loaded bridge**. The next distinct attack experiment
is **a moving, compliant plucking contact with a physically consistent loaded
initial state**, conditional on identifying its release trajectory. Neither
recommendation is evidence that a candidate already sounds better.

This audit considers primary work available through 8 September 2026 and the
source at `dfa100295fde29b8df1c467d9401e07417059ed9`. It supplements
[the earlier solver review](physical-modelling-review-2026-09-08.md), which
already gives the recent regulated-SAV algorithms and implementations. It
does not propose controls, additional instrument features, or a commercial
quality ranking. Numerical acceptance thresholds below are **proposed
experiment gates**, not published audibility thresholds or completed results.

## Evidence and practical priority

| Priority | Missing or incomplete mechanism | Evidence and real-time consequence |
| --- | --- | --- |
| 1 | Per-string, length- and frequency-dependent decay, fitted jointly with dispersion and bridge loading | Measured modal frequencies and decays directly constrain it. Acustra already has loss filters, measured nylon bending rigidity and stiff-string dispersion; the improvement is their identification and causal realization, not another generic brightness control. Offline design/cache can leave the audio loop small. |
| 2 | Plucking-hand loading, slip and temporal release with consistent initial displacement/velocity | Guitar measurements establish that trajectory and mounting affect attack; recent passive contact algorithms supply a credible solver. Actual finger/nail/pick parameters are not supplied by the current recordings. A short-lived local interaction can be cheaper than replacing every string, but requires a physical power port and measurement before adoption. |
| 3 | Local nonlinear energy exchange between transverse modes, and eventually dynamic axial coupling | The 2025–2026 SAV work reviewed previously addresses a real limitation of a global pitch surrogate. It still needs convergence, coupled-junction tuning and six-string callback qualification. A stable isolated solver is insufficient. |
| 4 | Two radiating transverse planes with measured reciprocal loading | Physically important for beating and multi-stage decay; not identified by a normal-impact heave/rock matrix. Invented horizontal admittance fractions do not close the measurement gap. |
| 5 | Fret collisions, winding friction and contact-dependent dissipation | Relevant when actual displacement reaches a measured barrier or actual sliding occurs. Current mixed microphone residuals do not identify a friction-force gain. Added noise without that identification would not establish realism. |

These are priorities for this implementation, not a ranking of scientific
papers. The lower-cost candidates should be judged on recorded nylon and steel
separately; a pooled improvement can conceal a worse low E or nylon treble.

## What the recent work actually establishes

**Measured loss and stiffness, 2024–2025.** Lampis, Mayer and Chatziioannou's
[2024 JASA Express Letters study](https://pub.mdw.ac.at/media/content_files/113201_1_10_zGOt7Rg.0034330.pdf)
(published 7 November; DOI 10.1121/10.0034330, §2.1) obtains string properties
from 12–15 breaking-wire plucks per cello string. It estimates stiffness from
partial spacing, and Q from narrowband decay, excluding poor fits. A reported
29% stiffness change on remounting illustrates a real nuisance variable.
Its [Zenodo dataset](https://doi.org/10.5281/zenodo.13374477) is associated with
bowed bridge-force experiments; this audit has not verified that all underlying
breaking-wire records are downloadable. Do not describe it as a ready guitar
pluck dataset.

Lampis, Chatziioannou and Scavone's
[2025 follow-up](https://caml.music.mcgill.ca/lib/exe/fetch.php?media=publications%3Alampis_ismra2025.pdf)
is **Experimental analysis of cello string types: influence on playability and
tonal characteristics using Schelleng diagrams**, published 23 September 2025,
DOI 10.1121/2.0002111. Its running header uses a different short title.
Eight custom G2 strings vary core, winding and nominal tension; repeated plucks
monitor mechanical properties. Damping varies appreciably between materials
and modes. The paper explicitly limits the statistical generality of its small
string sample. Transfer the measurement procedure, not its cello coefficients,
bow-force law or brightness correlation. Its data statement points to
[the authors' project page](https://iwk.mdw.ac.at/projects/fwf-the-bowed-string/);
that page did not resolve through the research browser in this audit.

**Passive friction, 2024–2025.** Van Walstijn, Chatziioannou and Athanasopoulos,
[IFAC 2024](https://pureadmin.qub.ac.uk/ws/portalfiles/portal/613541822/1-s2.0-S2405896324010255-main.pdf),
DOI 10.1016/j.ifacol.2024.08.283, give a non-iterative, passive contact-damping
and dry-friction scheme for a **two-dimensional one-mass oscillator**. This is
a solver testbed, not measured guitar-finger validation. Equations 43 and
48–60 specify the normal update, admissibility restriction and tangential
stick/slip solve. Their tests report deteriorating accuracy near zero
restitution without substantial oversampling.

Matusiak and Chatziioannou's
[2024 measured bowed-transient study](https://pub.mdw.ac.at/media/content_files/1135_1_10_MdSYDl3.0028228.pdf)
(15 August; DOI 10.1121/10.0028228) uses finite bow width, compliance, torsion
and elasto-plastic friction. Good individual reconstructions require inverse
identification; fixed friction values fail to describe all bowing conditions.
Matusiak, Chatziioannou and Van Walstijn's
[2025 refinement](https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2025.1525044/full)
(21 May; DOI 10.3389/frsip.2025.1525044, §§3–6) supplies a passive formulation.
These are reasons to model contact state carefully, not grounds for assigning
rosin/bow coefficients to skin, nail or a plectrum.

**Guitar attack measurements.** Pluta et al.'s
[2025 journal study, posted to arXiv on 23 June 2026](https://arxiv.org/abs/2606.24356)
varies plucking depth in 192 µm steps and compares plectra. Mounting and
trajectory alter attack and harmonics. It supports separating depth, speed,
contact geometry and released velocity; it does not identify single-note
release time from MIDI velocity. The
[2025 Sensors comparison](https://www.mdpi.com/1424-8220/25/21/6514)
also shows that sensor choice and even a small optical target can alter what
is measured. Microphone pressure, bridge force and string displacement are
different validation observables.

**The July 2026 coupled-model claim.** Qinhan Jin's
[Springer article](https://link.springer.com/article/10.1007/s42417-026-02401-2)
is a published journal article dated **10 July 2026**, not a preprint.
The accessible primary abstract describes 15 string modes, 10 body modes,
RK4 string integration and Newmark body integration. It reports bridge
admittance agreement; experimental time-domain validation, listener studies,
wound strings and nonlinear dynamics remain future work. Full text is
subscription-only, and the visible data statement says parameters are in the
text; no public source implementation or raw validation archive was identified.
This audit could not verify its equations, boundary conditions or reproducible
audio. Its abstract therefore supplies no basis for replacing the existing
measured six-string instrument or claiming superior sound.

## Experiment 1: identify decay without hiding dispersion or bridge errors

The older guitar-specific foundations remain more actionable than an unrelated
newer coefficient table. Woodhouse's
[2017 nylon study](https://www.repository.cam.ac.uk/items/6be40e74-3d92-4252-a5a4-9c2475626a19)
shows that accurate linear damping/stiffness alone does not explain the full
light-gauge brightness difference; axial coupling also leaves a residual,
with contact/buzz implicated. Kodama et al.'s
[2023 guitar study](https://www.jstage.jst.go.jp/article/ast/44/3/44_E2244/_pdf)
measures plain-string decay at frets 1 and 13 and relates the shortening effect
to complex bending modulus. Its low-frequency mechanical measurements do not
establish frequency-independent audio-band material constants, and its steel
loss modulus was below the stated measurement resolution.

### Quantities and implementation target

For an isolated fitted mode, define the **amplitude** decay by
`a_n(t) = a_n(0) exp(-sigma_n t)`. Then

```text
sigma_n = -s_n ln(10)/20        [s_n is a negative dB-amplitude slope, dB/s]
T60_n   = ln(1000)/sigma_n      [seconds]
Q_n     = pi f_n/sigma_n
z_n     = exp((-sigma_n + j 2 pi f_n)/Fs)
```

This fixes common factor-of-two and dB/s versus seconds mistakes. Fit complex
decaying sinusoids where two close modes beat; do not interpret a regression
through a beating magnitude envelope as either mode's damping.

The following are small-loss, pinned-end reference identities, **not the exact
loaded-junction equations**. With `k_n=n*pi/L`, mass per length `mu`, tension
`T`, second moment `I`, and complex bending modulus `E'+jE''`,

```text
omega_n^2       = (T k_n^2 + E' I k_n^4)/mu
sigma_bend,n    ≈ E'' I k_n^4/(2 mu omega_n)
1/Q_bend,n      ≈ E'' I k_n^2/(T + E' I k_n^2)
B              = pi^2 E' I/(T L^2)
f_n            = n f_1 sqrt((1+B n^2)/(1+B))
```

Consequently equal absolute frequency does not imply equal intrinsic decay
on different strings or frets. Preserve `(instrument, string, fret, mode,
amplitude, take)` rather than pooling independently by frequency. The existing
[damping audit](../Tools/AuditDampingCurve.py) and its doublet option are useful
diagnostics; pooled plots alone cannot identify a per-string physical law.

For a waveguide, `|H_loss(f_n)| ≈ exp(-sigma_n tau_roundtrip,n)` is only a weak-loss
design approximation. Dispersion and boundary phase affect the round-trip
group delay. Evaluate the realized **complex discrete poles of the complete
string/bridge network**, or estimate its freely decaying native response,
before accepting a design. Retuning frequency after changing loss without
including loss-filter phase is not equivalent.

Use nonnegative dissipation parameters and a causal contractive loop filter;
fit magnitude and phase jointly, with its coefficients designed off the audio
thread or cached over the existing physical parameter grid. Do not implement
a frequency-independent imaginary modulus as an unexplained time-domain
constant. A compact passive viscoelastic realization is acceptable only if its
actual discrete response fits the target decay and stiffness together.

### Data, constraints and proposed gates

1. Freeze the current measured body, bridge geometry, capture and contact
   initializer. Use already-authorized recordings to extract matched partial
   frequencies and early/late rates. Keep rejection counts, windows and noise
   estimates. The 2024 repeated-pluck protocol's `R² >= 0.98` decay-fit screen
   is a useful starting rule; add a predeclared 20 dB signal-to-noise threshold
   and report sensitivity to window choice. These latter choices are ours.
2. Distinguish intrinsic-string experiments with rigid terminations from
   played-guitar decays, which include body leakage and neighbouring strings.
   Fit the latter through the loaded model; do not subtract a microphone
   spectrum and call the remainder string loss. Preserve measured material
   and geometry bounds. Unknown string brand, age or played string remains
   unknown; a MIDI pitch is not a unique string/fret label.
3. Constrain the fit with per-string H1 and upper-partial errors as well as the
   pooled objective. Proposed acceptance: at least 10% reduction in median
   matched `|log(T60_model/T60_reference)|` on a frozen held-out set, with no
   string's median or pooled 90th percentile worsening by more than 5%.
   Report both decay windows. These are development gates, not perceptual
   thresholds. Preserve all existing tuning, dynamics and rate-invariance
   checks and separately report high-partial frequency residuals.
4. Split by recording source/instrument when possible, then hold out takes and
   pitches before fitting. Already-opened development clips cannot become a
   fresh blind set. Bootstrap uncertainty by instrument or take, not by
   treating hundreds of correlated partials as independent recordings.
5. Run gain-matched paired listening after freezing coefficients. Keep the
   exact shared scalar, raw levels and clipping/headroom. Report nylon and
   steel preference independently. No per-example spectral normalization.
6. Measure complete six-note initial/repluck callbacks at 48/96 kHz and
   32/64/128 frames against a frozen binary. Require no material p95 regression
   before adoption; preserve max/deadline misses. Average synthesis speed
   does not qualify an expensive note-on coefficient solve.

This is not a request to repeat the earlier Woodhouse-table substitution.
[The decision log](decisions.md) records that the 4 September version
over-damped the upper band, inherited an unmeasured age transformation and
roughly doubled nylon note-on design cost. It also records a pooled refit
that worsened low-E decay despite improving aggregate scores. The present
experiment changes the identification and realization checks that exposed
those failures; it must still demonstrate an actual improvement.

## Experiment 2: identify a moving plucking contact, not another smoothing kernel

The Gaussian initializer now models **spatial** contact. A contact lasting
finite time also changes phase and released velocity. These cannot generally
be recovered by making the spatial aperture wider. This concerns the plucking
hand's attack; ordinary MIDI note-off, repluck damping and the stopping finger
are separate existing mechanisms.

For a pinned, ideal string loaded at `a` with transverse displacement `d`, the
static reference is

```text
F0 = T L d/[a(L-a)]
U0 = F0 d/2
u(x,0) = d*x/a                 for x <= a
u(x,0) = d*(L-x)/(L-a)         for x > a
u_t(x,0) = 0
```

These simple identities are a validation fixture. The loaded body/finger
changes that equilibrium. In a displacement-wave representation the initial
right/left shapes are `u/2`; in a **velocity-wave** representation they are
`-c*u_x/2` and `+c*u_x/2`, so total initial velocity is zero. Verify the actual
engine wave convention and filter histories explicitly; copying a displaced
shape into a velocity buffer is not this initialization.

A distinct candidate uses measured contact trajectory `d_h(t)` and the local
string displacement, with penetration `delta=d_h-u_contact`. A minimal
unilateral compliant model has

```text
V_contact = kappa [delta]_+^(alpha+1)/(alpha+1)
N         = kappa [delta]_+^alpha max(0, 1 + eta delta_dot)
F_t       = -mu_f N sign(v_relative)     while sliding
|F_t| <= mu_f N, v_relative=0            while sticking
```

Here `kappa` has units `N/m^alpha`, `eta` has units `s/m`, and `mu_f` is
dimensionless. The signs are in the contact's local coordinates. This is a
continuous-model sketch; discretizing the `max` expression explicitly does
**not** inherit passivity automatically. The moving hand supplies work, so
the audit is `change in stored energy = external work - dissipation`, not
monotonically falling total energy while the hand is still loading it.

The 2024 contact paper's explicit scalar normal update is the implementation
reference: its equation 53 computes `s_x=x[n+1]-x[n-1]`, equation 52 advances
the square-root contact energy, and equations 59–60 constrain its derivative
and empty the energy state on separation. Equation 43 then resolves tangential
sticking/sliding. Apply the whole admissibility construction, not only the
appealing division in equation 53. Coupling this point interaction to a
waveguide requires deriving its local port compliance and shared power balance;
the paper's oscillator mass is not a string-segment mass to assign arbitrarily.

The authors' [MATLAB reference](https://github.com/mvanwalstijn/Mass-Barrier/tree/ad1b11d4ff6a9149c93f0dd74ec94cdb6e3d3fb2)
was inspected at commit `ad1b11d4ff6a9149c93f0dd74ec94cdb6e3d3fb2`
(24 January 2024). `simEXPfunc.m` and `simIMPfunc.m` provide explicit/implicit
comparators; `main.m` uses a 256×44.1 kHz reference. The example oscillator's
`m=0.001 kg`, `k=1000 N/m`, `r=0.002 Ns/m`, `kappa=1e7`, `alpha=1.25`,
restitution `0.1` and friction coefficient `1` are **test parameters, not
guitar-finger measurements**. Its optional contact-stiffness cap enforces
at least six samples per expected collision: changing it changes the model.
No license file is present at this revision; use the published mathematics
for an independent implementation rather than assume a permissive code license.

Required measured inputs are preload displacement or force, plucking position,
contact width/geometry, released normal/tangential velocity or release-force
trajectory, and enough force/displacement data to distinguish compliance from
friction. A contact-only high-rate simulation may fit these offline; a compact
release-state map can then initialize the existing string without a permanent
new per-sample solver. Parameter interpolation must preserve energy and phase.
No published 2024–2026 source found here identifies all of these for the
current nylon/steel recording corpus.

Before a full contact solver, an identifiable diagnostic is the released modal
phase. For an ideal linear mode under initially static force, with normalized
unloading rate `w(t)=-F'(t)/F0` and `integral(w)=1`, the released complex modal
amplitude contains `H_release(omega)=integral(w(t) exp(-j omega t) dt)`.
Its time-origin phase is arbitrary; relative modal phase is not. Separate it
from known body/filter phase and acoustic delay. This relation is a diagnostic
for a measured force trajectory, not permission to refit an arbitrary kernel
until the microphone sounds warmer.

Proposed acceptance gates are:

- Match a high-rate independent contact reference with positive denominators,
  nonnegative dissipation, correct separation, and no residual auxiliary
  energy after separation. Halve the time step twice; force impulse, release
  time and resolved modal amplitudes/phases must converge. Keep round-off
  tolerances explicit; energy stability alone does not prove trajectory accuracy.
- Match static displacement, preload force and zero initial velocity before
  release. An instantaneous, frictionless limit must recover the existing
  independent Gaussian/Fourier contact tests after accounting for convention.
  Equal total initial energy must not become a hidden gain increase.
- In a predeclared recorded-attack set, require at least 5% improvement in the
  matched first-100-ms spectral-envelope error separately for nylon and steel,
  with no more than 1% worsening of held-note decay, tuning or dynamics terms.
  Preserve signed pick-minus-finger spectral direction, not only contrast
  magnitude. Finger and thumb cannot be independently identified from a label
  that merges them.
- Preserve ordinary note-off, held-note, harmonic and MPE invariants, and the
  complete callback gates from experiment 1. Freeze before controlled listening
  or opening a fresh evaluation set. Stop if trajectory parameters cannot be
  distinguished from body response or initial amplitude.

The 30 August exact linear-unloading and half-cosine trials already failed
decay/harmonic gates. The 4 September radius/speed smoothing used strum travel
speed as a single-note release proxy and omitted beam compliance, Coulomb slip
and released velocity; it also failed. Those results are retained in
[decisions.md](decisions.md). A new attempt needs the missing physical state or
new measurements, not renamed smoothing or a chosen 5–10 ms contact duration.

## What cannot be inferred from the current body measurements

Two measured normal bridge inputs identify a heave/rock subspace. They do not
identify lateral translation or a reciprocal two-plane bridge matrix. With
normalized rocking coordinate `r=a*theta`, a horizontal force acting at saddle
height `h` projects by `h/a`; its driving mobility includes `(h/a)^2 Y_rr`.
Even the correct projection does not measure the missing geometry or lateral
mobility. The retained saddle-height trial already exposes mode-selection and
tuning problems. Neither `Y_hh=0.15Y_vv` nor a renamed gain makes this measured.

Similarly, an amplitude-squared axial resonator can produce sum/difference
frequencies without a reciprocal longitudinal/transverse solver. Its mere
presence is not validation of nonlinear intermodal transfer. Fret collision
requires actual displacement/action agreement; the prior barrier trials and
their corrected displacement interpretation must be consulted before a new
threshold is chosen. These remain reasons for a carefully bounded future
physical experiment, not mechanisms to add speculatively to the output.

## Commercial-reference feasibility

Official vendor documentation establishes capabilities and access conditions;
it does not establish comparative realism. No commercial rendering or purchase
was performed for this audit.

| Reference | Reproducible comparison that is feasible | Limitation |
| --- | --- | --- |
| [Pianoteq Classical Guitar](https://www.modartt.com/guitar) | Render an authored, explicit event list with a frozen preset and effects/capture state in an authorized installation. The official [9.2.4 trial](https://www.modartt.com/try) includes instrument packs, disables eight named notes and restarts after 20 minutes; screen the exact test pitches beforehand. | The model blends three instruments, including Fouilleul C9. Public demos use presets with adjustments; this audit did not identify exact MIDI plus state for those demos. They are listening references, not matched measured guitars. |
| [AAS Strum GS-2 2.4.6](https://www.applied-acoustics.com/strum-gs-2/resources/) | Official [Keyboard mode, §3.1](https://www.applied-acoustics.com/strum-gs-2/manual/) plays the supplied pitches without chord revoicing, in MIDI range 40–93. Use that mode and explicit note events; freeze articulation behavior. The vendor offers a [15-day trial](https://www.applied-acoustics.com/strum-gs-2/). | Guitar/Loop modes interpret chord and strumming controls. Exporting a loop does not automatically provide identical per-string performed notes; automatic legato behavior must be checked. |
| [Ample Guitar M Lite](https://www.amplesound.net/en/pro-pd.asp?id=7) or an authorized full Ample instrument | A sampled guitar offers a useful second synthesis class. The vendor's [Riffer documentation, §§6.7–6.9](https://www.amplesound.net/en/tutorial.asp), distinguishes channel and keyswitch export and whether humanization is exported. Preserve the actual resolved MIDI and string mapping. | Verify the installed edition/version rather than infer all full-product features from a shared product page. A sample library is a commercial reference, not an independent set of real performances for fitting Acustra. |
| Existing NI Strummed Acoustic audition | Preserve the already documented source instrument/version, dry settings, tempo, harmony and crop from `physical-fit-report.json`. | The prior comparison shares harmony/rhythm but uses NI's recorded voicings, accents and microtiming. It is not an identical-performance test and cannot support a market-leader claim. |

For a meaningful comparison, preserve exact note-on/off sample offsets,
velocities, pitch bends, keyswitches, string assignment where supported, preset,
plugin version, sample rate, effects, microphones and output gain. A shared
MIDI velocity does not establish equal physical pluck force. Report both a
fixed-MIDI test and, separately, a predeclared loudness-matched listening test;
do not optimize one competitor per clip after hearing the result.

The **real recordings remain the realism target**. Commercial instruments can
be rendered from the same verified transcription and scored against that
target, but guitar identity, capture and unknown technique constrain the
interpretation. For listening, randomize and blind order, include an actual
recording and a deliberately reduced-quality anchor, and ask for realism
separately from preference. A public unmatched demo can guide qualitative
listening; it cannot establish that Acustra beats the market or that any
particular missing equation causes the difference.
