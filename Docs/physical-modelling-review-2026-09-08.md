# Physical guitar modelling review — 8 September 2026

This is a targeted review of primary publications and publicly obtainable
implementations, checked on 8 September 2026. It is not a claim that one method
is universally the most advanced or that publication recency establishes
audible realism. The practical next solver experiment is a **local-slope
nonlinear string with regulated scalar auxiliary variable (SAV) integration**.
It addresses a mechanism the present pitch surrogate cannot produce: energy
transfer between string modes. Measured body identification and better pluck
contact remain separate, necessary work.

DAFx26 took place on **1–4 September 2026**, so its proceedings are current
publications at this cutoff, not future conference submissions. The official
[conference program](https://dafx26.mit.edu/program/) lists both the nonlinear
65-guitar paper and the differentiable Karplus–Strong paper.

## What Acustra already contains

This inventory describes the working source inspected on the review date;
candidate acceptance and measured outcomes belong in
[the implementation work log](realism-work.md).

| Mechanism | Present implementation | Remaining distinction |
| --- | --- | --- |
| String propagation | Two transverse waveguide loops, fractional-delay allpass, stiff-string dispersion and frequency-dependent loss | A pair of loops does not establish fully coupled two-plane nonlinear motion. |
| Nonlinear attack pitch | Steel uses a bounded Kirchhoff–Carrier slope-energy surrogate; nylon retains an authored pitch cue | Changing propagation delay from total slope energy cannot reproduce local nonlinear intermodal exchange. |
| Bridge and idle strings | Finite-impedance shared six-string force/moment junction with reciprocal mechanical feedback | A new string solver must retain these power ports and shared state. |
| Body and air | Fitted modal bridge/radiation responses; Original plus measured Bellido, Washburn, Santa Cruz OM and Martin D18V choices | Each measured bank identifies its source instrument and sensor geometry. Authored construction transforms do not identify additional guitars. |
| Axial effects | Optional one-way slope-squared-driven axial resonators; shipping gain is zero | This is not dynamic longitudinal/transverse coupling. Wound-string effective mass does not determine axial rigidity. |
| Capture | Measured stereo pressure, one pressure observation copied identically to both mono outputs, electrically loaded piezo bridge-force observation | Capture changes should remain observers of the same mechanics. |
| Excitation and release | Continuous Gaussian spatial contact, separate authored technique widths/positions, damping on ordinary note-off; explicit CC68 pull-off behavior | No continuous finger/nail/plectrum frictional contact, finger-loaded equilibrium, or general fret collision solver. |

Source locations: [AcustraEngine.cpp](../Source/DSP/AcustraEngine.cpp),
[AcustraEngine.h](../Source/DSP/AcustraEngine.h), and the architectural and
limitations sections of [README.md](../README.md). The existing isolated SAV
experiment's good energy invariant and failed audio gates are useful evidence:
they justify a corrected experiment, not an automatic solver replacement.

## Relevant recent primary work

| Work and publication status | What it contributes | Consequence for this instrument |
| --- | --- | --- |
| Ducceschi, Russo and Webb, [Measurement-Informed Nonlinear Modal Synthesis of 65 Classical Guitars](https://dafx26.mit.edu/assets/papers/DAFx26_paper_40.pdf), DAFx26, September 2026 | Local geometrically exact transverse potential, measured bridge/radiation, drift and sign regulation, two rank-one solves. Longitudinal displacement is absent. Idle strings are driven one way; real-time C++ and fully coupled simultaneous strings are future work. | Most directly relevant integrated recent formulation. Preserve Acustra's existing shared six-string junction when testing its nonlinear mechanism. |
| Russo, Ducceschi and Bilbao, [Numerical convergence of the scalar auxiliary variable method applied to nonlinear stiff string models](https://link.springer.com/article/10.1007/s11071-026-12708-0), *Nonlinear Dynamics* 114, article 857, published 29 June 2026 | Examines geometrically exact, cubic and Kirchhoff–Carrier models, splitting, spatial convergence and potential shifts. Stable integration can still have poor convergence and incorrect motion. | Require convergence and auxiliary-variable consistency checks alongside energy. A stable five-second render alone is insufficient. |
| Risse, Hélie and Bilbao, [Power-balanced drift regulation for scalar auxiliary variable methods](https://dafx.de/paper-archive/2025/DAFx25_paper_24.pdf), DAFx25, September 2025 | Controls SAV drift through a power-balanced modification of the state coupling; supplies a cubic-string real-time implementation. Its reported laptop polyphony concerns that implementation. | A concrete implementation reference for the next candidate; it does not establish the CPU cost of Acustra plus measured bodies and six shared ports. |
| Bilbao, Russo, Webb and Ducceschi, [Real-Time Guitar Synthesis](https://www.mdphys.org/PDF/DAFx24_Guitar.pdf), DAFx24, September 2024 | Kirchhoff–Carrier transverse string with fretboard, fret and stopping-finger contact using energy quadratisation and small low-rank linear updates. The demonstrated string output is not an identified complete body-radiation pipeline. | Best next reference for physical stopped-note, buzz, tap and slap interactions after the string/body candidate passes. See §§4.6–4.7 and §5. |
| Díaz and Sandler, [Fast Differentiable Modal Simulation of Non-linear Strings, Membranes, and Plates](https://arxiv.org/abs/2505.05940), May 2025 preprint, identified there as accepted to DAFx25 | Differentiable physical modal simulation and parameter inversion using JAX/GPU. | Useful for offline parameter estimation or a high-rate reference. GPU experiments are not evidence of a practical audio-thread replacement. |
| Tablas de Paula et al., [Sound Matching with a Differentiable Karplus-Strong Algorithm](https://dafx26.mit.edu/assets/papers/DAFx26_paper_38.pdf), DAFx26, September 2026 | Studies optimization of physical synthesis controls; separate onset and pitch estimates improve robustness over relying entirely on waveform/spectral gradients. | Improve calibration methodology. Replacing the existing stiff-string/body physics with a simpler KS model is not implied. |
| Pluta et al., [The effect of micro-changes in the pluck trajectory on the sound of an acoustic guitar](https://arxiv.org/abs/2606.24356), journal article dated 2025, arXiv posted 23 June 2026 | Robotic depth changes and different picks produce measurable, mounting-dependent level and harmonic changes. | Keep depth, speed and contact geometry distinct. These measurements do not determine a universal MIDI velocity law. |

For sensor interpretation, Jasiński et al.'s [2025 Sensors
paper](https://www.mdpi.com/1424-8220/25/21/6514), published 22 October 2025,
compares acoustic, electromagnetic and optical measurements of a plucked or
struck string. It is not a measured reciprocal two-axis guitar bridge matrix.
Using a microphone reference to identify a piezo or magnetic observer without
accounting for the different measured quantity would confound the experiment.

## Obtainable models and real measurements

The [65-guitar companion repository](https://github.com/Nemus-Project/65_modelled_guitars)
was inspected at commit `8336bdd87243d322eba7ffa49404fc517a59cc8f`
(31 March 2026). Its complete recursive tree contains 89 files; apart from
README and metadata files, they are audio. **No synthesis source or fitted
coefficient bank is present at that revision**, despite the paper referring
readers there for code. Its WAVs are synthesized examples, not recordings of
played guitars. They must not enter a real-recording validation split.

The companion identifies these nine example instruments:

| Archive ID | Companion identification |
| --- | --- |
| g002 | Benito Campo, 1840 |
| g003 | Bordones y Cuerdas, 1952 |
| g021 | Lester DeVoe, 2018 |
| g025 | HNOS Sandiz Lopez, 2018 |
| g033 | Antonio Lorca, year unknown |
| g049 | Enrique Garcia, 1913 |
| g055 | Manuel Munoa, 1803 |
| g061 | Joseph Martinez, 1804 |
| g062 | Joseph Pages, 1818 |

Actual measured input is obtainable from Robert Mores's
[Zenodo record 4604577](https://zenodo.org/records/4604577), licensed CC BY 4.0:

- [Qualified selected impulses MAT file](https://zenodo.org/api/records/4604577/files/qualified_selected_impulses.mat/content), 32,729,680 bytes.
- [Measurement method](https://zenodo.org/api/records/4604577/files/Method.pdf/content).
- [Instrument descriptions](https://zenodo.org/api/records/4604577/files/List_of_guitars_description.pdf/content).
- [Physical measures](https://zenodo.org/api/records/4604577/files/List_of_guitars_physical_measures.pdf/content).
- [Dataset comments](https://zenodo.org/api/records/4604577/files/Comments.pdf/content).

These are impact measurements for identifying the bridge and radiation, not
matched performances proving pluck realism. Preserve guitar ID, selected
impact, hammer/response channel, sensitivities, time window and fitting
residuals with each derived bank. Use the source descriptions to choose
distinct constructions instead of selecting attractive synthesized examples.

The independent [Rau guitar measurement archive](https://rau.mit.edu/projects/GuitarMeasurements/)
supplies additional measured instruments. Its specific channel semantics and
candidate results are documented separately in
[the source-method review](rau-source-methods-2026-09-08.md). Multiple repeats
of a microphone observation must not be represented as simultaneous stereo.

## Next experiment and acceptance conditions

The recommended solver experiment is bounded: one nonlinear string in an
offline renderer, using current physical dimensions and the existing measured
body interface. Start with a cubic local potential to reproduce the available
2025 implementation, then change only the potential to the exact transverse
form. Evaluate ordinary plucks before adding contacts or longitudinal states.
This isolates whether the new mechanism improves the known pitch/DC failure.

The expected benefit is amplitude-dependent intermodal behavior. It remains a
hypothesis until the following independent checks pass:

1. **Convergence:** compare displacement, modal energy and bridge force against
   a tighter spatial/high-rate reference; sweep gauge, grid and time step.
   First verify the linear limit and independently specified physical units.
2. **State consistency:** measure SAV energy error, signed auxiliary state,
   denominator margins, repeated plucks, post-release decay and DC. An energy
   bound is not an error bound.
3. **Guitar behavior:** check velocity/pitch trajectories, partial frequencies,
   two-stage decay and attack spectra across open and fretted notes, both
   materials and more than one measured body. Fix gains and onset alignment
   before comparisons; retain unsuccessful cases.
4. **Integration:** preserve mechanical work at the six-string junction,
   sympathetic exchange, ordinary dissipative release and capture invariance.
   Check single-string behavior before introducing shared nonlinear strings.
5. **Real-time cost:** measure worst-case six active strings plus release tails,
   automation and measured modal banks at supported sample rates. Record
   allocations and peak block time; an average standalone throughput number
   cannot approve the plugin path.

For immediate audibility, body identification and removal of the five-tap
contact kernel's high-frequency revivals are also concrete candidates. A
continuous Gaussian with the same spatial variance is a mathematically useful
controlled comparison, not a newly published physical contact law. It should
be selected only through the recording and listening gates already used for
the instrument. A measured two-axis bridge and coupled axial motion are later
mechanisms requiring additional identified parameters; neither is supplied by
simply adding more resonators.

## Appendix: bounded SAV implementation references

### Available C++ and provenance

The paper's old GitHub URL redirects to
[Ircam-RnD/SAV-string-simulations](https://github.com/Ircam-RnD/SAV-string-simulations).
The inspected revision is `2c87b7d3de966c3305f2a4d22415d99457fc1a2b`
(17 March 2026), under [GPL-3.0](https://github.com/Ircam-RnD/SAV-string-simulations/blob/2c87b7d3de966c3305f2a4d22415d99457fc1a2b/LICENSE).
The repository describes a JAES extension as **under review**, not published.

| Pinned file | Useful implementation locations |
| --- | --- |
| [StringProcessor.cpp](https://github.com/Ircam-RnD/SAV-string-simulations/blob/2c87b7d3de966c3305f2a4d22415d99457fc1a2b/src/processor/StringProcessor.cpp) | `computeVAndVprime` at line 181; midpoint `computeV` at 238; `process` at 286; drift correction at 322; rank-one solve at 354; auxiliary update at 359. |
| [StringProcessor.h](https://github.com/Ircam-RnD/SAV-string-simulations/blob/2c87b7d3de966c3305f2a4d22415d99457fc1a2b/src/processor/StringProcessor.h) | Actual implementation requires Eigen. The `GE` enum means cubic geometric force, not the square-root exact potential. |
| [Timing CMake](https://github.com/Ircam-RnD/SAV-string-simulations/blob/2c87b7d3de966c3305f2a4d22415d99457fc1a2b/src/timing/CMakeLists.txt) | Standalone C++17 target; Eigen dependency; upstream benchmark enables `-ffast-math`. Recheck numerical behavior with production compiler settings. |

No upstream code is copied into Acustra by this review. The algebra below is
an implementation outline, not a claim of a tested replacement.

### Potential and consistent derivative

DAFx26 §2.1, equations (5)–(7), and §2.3, equations (19)–(20):

```text
G = EA - T0 > 0
R[i,m] = sqrt(2/L) * (m*pi/L) * cos(m*pi*x[i]/L)
zeta = R*q
strain[i] = sqrt(1 + zeta[i]^2) - 1
Phi(q) = (G/2) * sum_i w[i] * strain[i]^2
psi_true = sqrt(2*Phi + gauge)
dPhi/dq = R^T * { w[i]*G*strain[i]*zeta[i]/sqrt(1+zeta[i]^2) }
eta = (dPhi/dq) / psi_true
```

Here `w` is the integration weight in metres. Implementation recommendations:
use `zeta²/(sqrt(1+zeta²)+1)` for small-slope strain to avoid cancellation;
use exactly the same endpoint weights in energy and gradient; test the
gradient by directional finite differences. Apply the same normalization to
modal masses and external-force projection. Retain a zero-safe denominator.
Do not fit `EA` using composite wound-string density as if it were core area.

### Drift and sign regulation

Risse et al. §4, equations (11)–(14), defines the drift and continuous control:

```text
error = psi - sqrt(2*Phi + gauge)
g_modified = g_standard - lambda*error*sign(M^-1*p)/norm1(M^-1*p)
```

Its discrete implementation evaluates the analytic gradient at `q[n]`, the
drift energy at `(q[n]+q[n-1])/2`, and uses the velocity proxy
`(q[n]-q[n-1])/dt`. The same corrected gradient enters the displacement solve
and `psi += 0.5*dot(g, q[n+1]-q[n-1])`.

DAFx26 §4.3, equations (39)–(43), instead prints a slope-space servo with
`norm(delta_zeta)`, followed by a sign constraint:

```text
g_bar = g_analytic - (psi_half - psi_true_half)*delta_zeta/norm(delta_zeta)
theta = eta^T * (A_inverse*b_linear - y_previous)
gamma = (-4*psi_half/theta) if theta != 0 and theta < -4*psi_half else 1
g_final = gamma*g_bar
```

Treat these as distinct controllers. Do not transplant the 2025 gain or replace
the printed norm by its square without deriving and validating that variant.
Skip a zero-velocity correction explicitly. Recompute projected quantities
from the final gradient consistently; post-hoc clamping of `psi` would change
the state update. Initialize the auxiliary state from the actual preload.

### Two rank-one solves

DAFx26 §4.1–4.2, equations (35)–(38), has this matrix structure:

```text
(D + f*v^T + alpha*z*eta^T) * y_next = b
alpha = dt^2/(4*mu)
```

The following is the generic Sherman–Morrison identity applied twice; it can
be implemented without constructing a dense inverse:

```text
u = D_inverse*f
den_bridge = 1 + dot(v,u)
A_inverse(x) = D_inverse*x - u*dot(v,D_inverse*x)/den_bridge

p = A_inverse(b)
t = A_inverse(z)
den_nonlinear = 1 + alpha*dot(eta,t)
y_next = p - alpha*t*dot(eta,p)/den_nonlinear
```

Precompute the time-invariant diagonal and bridge terms. Monitor both
denominators and account for all powers of `dt` and mass in the right-hand
side. Linear-algebra cost is linear in state size; a dense `R*q` and `R^T*g`
still cost the number of grid points times the number of nonlinear modes.

Use the paper's equations (32)–(34) as exact-linear-pole references and
(36) for its right-hand side, not as unverified coefficients pasted into
Acustra. In particular, the text states `u_b=sum(r_j)` while its later
constraint uses a beta-weighted bridge state. Reconcile coordinate and residue
normalization before porting that coupling. Independently test the scalar
linear recurrence against prescribed poles and check bridge work in the
coordinates actually used by Acustra. A generic rank-one identity cannot
resolve an inconsistent physical state definition.
