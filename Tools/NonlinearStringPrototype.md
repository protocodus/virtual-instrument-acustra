# Offline regulated-SAV string experiment — 8 September 2026

`NonlinearStringPrototype.cpp` is a standalone native numerical experiment.
It implements local-slope nonlinear dynamics, a power-balanced SAV drift
controller, a physical Kirchhoff comparison and an independent RK4 reference.
It does **not** replace any shipping DSP, add controls, identify a guitar, or
establish an audible improvement. The current 12-mode G3 model stops near
2.37 kHz and has fixed, simply supported ends; there is no moving bridge,
body radiation, second transverse polarization, fret or longitudinal state.

The useful result is that regulated integration removes a reproducible
post-excitation auxiliary-energy residue while preserving the discrete work
balance. Local slope coupling also produces intermodal exchange absent from
the global-tension comparison. The force convergence results below prevent
treating energy conservation alone as qualification for audio-rate use.

## Reproduce

From the repository root:

```sh
c++ -std=c++20 -O3 -Wall -Wextra -Wpedantic Tools/NonlinearStringPrototype.cpp -o /tmp/AcustraNonlinearStringPrototype
/tmp/AcustraNonlinearStringPrototype --self-test /tmp/new-nonlinear-string-audit
```

The output directory must not already exist. The executable writes every
measurement to `report.json` and ten full-precision trajectory CSVs. A failing
candidate assertion returns exit code 1. Unregulated cases remain in the
report, including their failed release criterion, and explicitly test that
the negative control is sensitive. No engine, plugin, third-party numerical
library, fast-math flag or CMake target is involved.

The checked run is retained in [NonlinearStringPrototype.json](NonlinearStringPrototype.json),
including source/compiler/binary provenance and CSV hashes. Temporary CSVs
are reproducible from the native source; the committed JSON contains their
summary metrics. Numbers below refer to that run, not a production render.

## Primary basis and independent implementation

The cubic potential and drift-control choice follow Risse, Hélie and Bilbao,
[Power-balanced drift regulation for scalar auxiliary variable methods](https://dafx.de/paper-archive/2025/DAFx25_paper_24.pdf),
DAFx25, especially equations (1), (5), (7), (14)–(19). The gain used here,
1,000 s⁻¹, is a published numerical example, not a fitted guitar parameter.
The modal coordinates, units, rank-one elimination, damping ledger and native
tests below were derived and written independently. No companion GPL source
was opened or copied for this implementation.

The local square-root potential follows Ducceschi, Russo and Webb,
[Measurement-Informed Nonlinear Modal Synthesis of 65 Classical Guitars](https://dafx26.mit.edu/assets/papers/DAFx26_paper_40.pdf),
DAFx26, equations (5)–(7). “Exact” in the executable selects that transverse
potential; it does not mean a complete geometrically exact guitar. The
2026 paper's different slope-space servo and sign controller are not mixed
into the 2025 modal-velocity controller used here.

The need to separate stability from convergence is examined directly in
Russo, Ducceschi and Bilbao,
[Numerical convergence of the scalar auxiliary variable method applied to nonlinear stiff string models](https://link.springer.com/article/10.1007/s11071-026-12708-0),
*Nonlinear Dynamics* 114, article 857 (29 June 2026). This experiment therefore
measures physical-force error and gauge sensitivity in addition to energy.
The project-level source review is
[physical-modelling-review-2026-09-08.md](../Docs/physical-modelling-review-2026-09-08.md).

## Coordinates, units and spatial discretization

Use unnormalized sine modes on `[0,L]`:

```text
u(x,t) = Σ q_n(t) sin(k_n x),       k_n = nπ/L
m = μL/2
K_n = (L/2)(T k_n² + EI k_n⁴)
R_in = k_n cos(k_n x_i)
s_i = Σ R_in q_n
```

`q` and `u` are metres, `k` and `R` are inverse metres, slope `s` is
dimensionless, modal mass `m` is kilograms, and diagonal stiffness `K` is
newtons/metre. The same normalization projects a point force `F` in newtons
to modal force `sin(k_n x_in) F`. The observed displacement is at `0.371L`;
the input location is `0.173L`.

For `N` equal intervals, the endpoint-inclusive trapezoid weights are
`w_i=L/N`, with half weights at both endpoints. They have units of metres.
Every local energy and gradient uses the same weights and slope matrix.
`N>2M` exactly integrates a quartic slope potential for `M` sine modes up to
floating-point roundoff; the square-root potential needs a convergence check.

Let `G=EA−T>0`, `I=Σ (L/2) k_n² q_n²` (metres). The implemented potentials
and their **derived** gradients are:

| Model | Potential Φ, joules | Gradient ∂Φ/∂qₙ, newtons |
| --- | --- | --- |
| Linear | `0` | `0` |
| Cubic local | `Σ w G s⁴/8` | `Σ R_in w G s³/2` |
| Square-root local | `Σ w G (√(1+s²)−1)²/2` | `Σ R_in w G (√(1+s²)−1)s/√(1+s²)` |
| Kirchhoff | `G I²/(8L)` | `G I k_n² q_n/4` |

The square-root strain is evaluated as `s²/(sqrt(1+s²)+1)`, avoiding small
slope subtraction cancellation. The physical Hamiltonian is
`H=m||v||²/2 + qᵀKq/2 + Φ(q)`. This common potential/gradient implementation
also supplies the independent RK4 physical ODE.

The plain nylon G3 dimensions follow the current engine's construction:
`L=0.65 m`, diameter `1 mm`, bulk density `1,140 kg/m³`, `E=2.7 GPa`,
and measured effective `EI=310e−6 N m²`. They give
`μ=8.9535390627e−4 kg/m`, `m=2.9099001954e−4 kg`,
`T=58.1277438955 N`, `EA=2,120.5750411731 N`.
Effective bending rigidity is deliberately independent of static axial
rigidity; composite wound-string density is not used to infer a core area.

The physical Kirchhoff comparator is not a byte-for-byte copy of the
shipping steel pitch cue. That cue estimates a scalar tension increase from
total slope energy, alters delay length and bounds the pitch excursion.
It cannot exchange modal energy. The comparator here integrates the
corresponding global stretching potential without that clamp, in the same
coordinates as the local models, so the mechanism comparison is controlled.

## Time step and power ledger

At sample `n`, the stored states are `q=q[n]`, `q₋=q[n−1]` and
`ψ=ψ[n−1/2]`. Time step `h` is seconds and gauge `C₀>0` is joules.
`ψ` has units √J, while `g` has units √J/metre.

```text
q_mid = (q + q₋)/2
v_back = (q − q₋)/h
e = ψ − sqrt(2 Φ(q_mid) + C₀)
g_std = ∇Φ(q) / sqrt(2 Φ(q) + C₀)
g = g_std − λ e sign(v_back) / ||v_back||₁
```

When `||v_back||₁≤1e−16 m/s`, the correction is skipped explicitly. `λ` is
in inverse seconds. Both the solve and auxiliary update use the **same**
corrected `g`. There is no post-hoc energy, displacement or auxiliary clamp.

For modal viscous damping `C=2mη`, scalar `η≥0` in s⁻¹, define
`D=1+ηh` and `α=h²/(4m)`. Directly eliminating the centered auxiliary state
from the momentum and auxiliary equations gives:

```text
b = 2q − (1−ηh)q₋ − (h²/m)Kq
    + α g(gᵀq₋ − 4ψ) + (h²/m) f_n
r = b/D,       z = g/D
q₊ = r − α z(gᵀr)/(1 + α gᵀz)
ψ₊ = ψ + 0.5 gᵀ(q₊−q₋)
v_center = (q₊−q₋)/(2h)
```

The native code performs this rank-one update without constructing or
inverting a dense matrix. Its denominator is at least one for nonnegative
damping. All vectors and the slope matrix are allocated during construction;
`step`, potential/gradient evaluation and the force observer do not allocate.
Offline CSV writing and reference-sample storage are outside that claim.

The independently accumulated external and damping work obey this identity:

```text
E[n−1/2] = (m/2)|| (q−q₋)/h ||² + 0.5 qᵀKq₋ + 0.5(ψ²−C₀)
E[n+1/2] − E[n−1/2]
    = h f_nᵀv_center − h(2mη)||v_center||²
```

Writing `q_mid=(q+q₋)/2` shows that the kinetic coefficient is
`mI−h²K/4`. Thus the unshifted energy `E+C₀/2` is nonnegative under the
linear CFL condition `h²K_n/m<4`; the constructor rejects its violation.
Subtracting the constant gauge can yield tiny negative reported energies
without violating that bound. This invariant is not proof that `ψ` follows
the physical nonlinear energy or that the displacement is accurate.

For a resting preload, the initialization is
`q₋=q−h²(Kq+∇Φ(q))/(2m)` and `ψ=sqrt(2Φ((q+q₋)/2)+C₀)`.
This is a second-order Taylor approximation to the negative-time resting
state. Setting `q₋=q` would instead create a half-step velocity bias. In the
linear case this initialization reproduces the exact discrete cosine pole.

The left-end force delivered to a fixed bridge is observed as
`T u_x − EI u_xxx + τ_nl`, using the same local stress as the potential
derivative (or global `ΔT u_x` for Kirchhoff). It is a reaction force in
newtons. Since bridge velocity is zero here, this observation extracts no
energy and cannot substitute for a moving reciprocal bridge port.

## Native evidence

Unless stated otherwise: 12 modes, 64 spatial intervals, 48 kHz,
`C₀=1e−12 J`, `λ=1,000 s⁻¹`. The rounded values below remain fully precise in
the accompanying JSON.

| Check | Measured result |
| --- | --- |
| Independent directional finite differences, three nonlinear potentials, heights `1e−7`, `1e−4`, `0.01 m` | Worst relative gradient error `7.75e−10` |
| Cubic spatial quadrature, 25–192 intervals against 768 | Energy/gradient errors below `5e−15` |
| Exact potential, dense 0.10 m numerical stress case | Gradient error falls from `1.05e−2` at 25 intervals to `1.20e−15` at 192; this extreme state is not a guitar calibration |
| Linear pole at 48/96/192 kHz | Maximum displacement error below `9e−16 m` against its exact discrete cosine |
| Small-amplitude exact-versus-linear error, 0.1/0.05/0.025 mm | `2.307e−4`, `5.775e−5`, `1.445e−5`: approximately quadratic disappearance with amplitude |
| Exact local lossless power balance, pure 4 mm mode 1, 0.3 s | Relative cumulative residual below `5.2e−13` |
| Same local experiment, maximum mode-3 linear-energy diagnostic | `3.92%` of initial energy; global Kirchhoff remains exactly zero |
| One spatial mode at 192 kHz, exact model, 1/2/4 mm | Pitch rises `0.2017 / 0.8066 / 3.2213` cents above its same-rate linear pole |
| Same one-mode Kirchhoff experiment | `0.1345 / 0.5378 / 2.1492` cents; both agree with weak Duffing theory within `0.2%` |
| Sign-reversed plucks and force pulses, with release damping | Displacement/force sums and auxiliary difference are exactly zero in the checked binary |

For the one-mode analytic check,
`m q̈+K₁q+βq³=0`, with
`β_local=3GLk₁⁴/16`, `β_Kirchhoff=GLk₁⁴/8`.
The leading fractional frequency increase is `3βA²/(8K₁)`.
Its monotonic amplitude dependence is a controlled single-mode check;
it does not prove that a multimode attack pitch estimator must be monotonic.

Time convergence uses a 4 mm smoothed triangular preload over 0.1 s, comparing
all modal displacements and the endpoint force at common 48 kHz observation
times. Independent RK4 at 384 versus 768 kHz differs by only `2.15e−7`
in modal displacement and `1.71e−6` in force. SAV errors against the finer
reference are much larger:

| SAV rate | Regulated modal displacement error | Regulated bridge-force error |
| --- | ---: | ---: |
| 48 kHz | 4.381% | 27.485% |
| 96 kHz | 1.540% | 11.057% |
| 192 kHz | 0.397% | 2.864% |

The final doubling gives observed orders `1.956` and `1.949`, respectively.
Unregulated results are similar. Regulation controls drift but does not
remove linear numerical dispersion. This basic centered scheme does not
use the exact-linear-pole correction of the earlier integrated prototype.
Modal truncation and force convergence across higher spatial bandwidths have
not yet been qualified.

The repeated-excitation test starts from a 2 mm pluck, applies three fixed
1 N raised-cosine pulses lasting 1 ms at 50, 100 and 150 ms, then changes
positive damping from `η=1` to `100 s⁻¹` at 200 ms. It runs to 400 ms without
replacing modal state or resetting the auxiliary variable. These prescribed
forces represent repeated excitation, not a solved plectrum contact.

| Exact local model | Worst drift energy / initial energy | Final energy / initial energy | Fixed `abs(final fraction)<1e−4` release criterion |
| --- | ---: | ---: | --- |
| Unregulated, gauge `1e−14 J` | `1.743e−3` | `1.286e−4` | Fail, retained negative control |
| Unregulated, gauge `1e−10 J` | `1.743e−3` | `1.332e−4` | Fail, retained negative control |
| Regulated, gauge `1e−14 J` | `5.204e−5` | `8.812e−18` | Pass |
| Regulated, gauge `1e−10 J` | `5.204e−5` | `8.812e−18` | Pass |

All repeated-force/damping power residuals are below `9e−13` relative.
The regulator reduces this example's worst local drift energy by about 33×
and makes the final decayed state essentially gauge-independent. This does
not establish a universal optimal gain: in the pure-mode Kirchhoff control,
regulation actually increases the small maximum drift diagnostic. All tested
auxiliary states remain nonnegative, but no sign limiter or general guarantee
for arbitrary extreme states is supplied by this prototype.

## What this can and cannot repair

The [earlier rejected SAV transplant](../Docs/decisions.md#2026-08-30--local-slope-sav-stringbridge-candidate-rejected-by-public-nonlinear-oracle)
already conserved energy and used exact linear poles. It failed harmonic
redistribution, force-dependent pitch and finite-render DC criteria. Its
reported harmonic-profile error was 7.3% worse than production, rather than
the required 25% improvement. Its comparison examples were public synthetic
guitar renders with changing pluck positions, not controlled real recordings.

This experiment supplies a verified drift-control mechanism and a way to
diagnose physical state and force before radiation. It does **not** identify
the cause of that earlier failure: its retained temporary folders no longer
contain the candidate source, and this fixed-end model does not reproduce
its coupled bridge, attack handoff, output observer or test conditions.
The finite-window displacement means reported by this tool depend on the cut
phase (roughly −54 dB for the 0.3 s local pure-mode render). They are not a
claim of true DC, nor a substitute for the earlier −80 dB output gate.
Signed symmetry checks for intrinsic bias; the original audio gate remains
unresolved.

A defensible next integration is still an isolated candidate: increase modal
bandwidth and converge force; qualify exact-linear-pole treatment or the
required oversampling; derive the existing reciprocal bridge and shared
six-string work ports in these units; preserve physical state across plucks,
release damping and idle-string exchange. Then run the unchanged acoustic
checks with a matched bridge/radiation pair and excitation, followed by the
frozen real-recording TRAIN/development splits. Only a passing candidate
should proceed to the blind corpus, listening comparison and full callback
cost measurement. This prototype is useful groundwork for that experiment,
not a reason to ship a new solver now.
