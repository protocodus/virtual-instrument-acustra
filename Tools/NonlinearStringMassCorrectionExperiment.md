# Exact linear poles with physical stiffness and modified numerical mass

Dated 2026-09-08. **A separate isolated numerical candidate; no engine integration or acoustic qualification.** This follow-up preserves physical stiffness, nonlinear potential and unscaled force injection, placing the exact-pole correction in modal numerical inertia. Against the unchanged physical 768 kHz RK4 reference, regulated 48 kHz endpoint-force error is 0.254797%, compared with 0.604447% for the separate [stiffness correction](NonlinearStringExactPoleExperiment.md) and 27.4846% for the original prototype. Physical linear static compliance is recovered.

The [patch](NonlinearStringMassCorrectionExperiment.patch) applies directly to the original `NonlinearStringPrototype.cpp`, SHA-256 `227d13ddeccfca9efa1499e30b54301a9542bb065d3eda04e9b3ce0962c3afd5`. It is not a patch on top of the stiffness candidate. Neither that candidate nor the original was edited. The original's fresh baseline has zero failures and exactly matches its 67 preserved measurements. The mass candidate retains **two inherited negative-control expectation failures**, described below; its other checks pass. It is not an all-green production implementation.

## One unfitted candidate

Use the same physical modal mass `m=mu L/2`, physical modal stiffness `K_j`, angular frequency `omega_j=sqrt(K_j/m)`, time step `h`, and `theta_j=h*omega_j` as the frozen prototype. Derive the numerical modal mass from its undamped homogeneous recurrence:

```
Mtilde_j [q_j[n+1]-2q_j[n]+q_j[n-1]]/h² + K_j q_j[n] = 0,
Mtilde_j = K_j h²/[4 sin²(theta_j/2)]
         = m/sinc²(theta_j/2),   sinc(x)=sin(x)/x.
```

This yields roots `exp(±i theta_j)` while retaining physical `K`. `Mtilde` is numerical inertia, not a remeasurement of string density. At mode 12 and 48 kHz it exceeds physical `m` by 0.807943%. The constructor checks the physical condition `0 < omega_j < pi*fs` before evaluating the correction. Tests reject frequencies equal to, 1.01 times and 2.01 times Nyquist at all three rates, while accepting 0.99 times Nyquist. This is necessary bandwidth control, not proof of nonlinear anti-aliasing or good conditioning near Nyquist.

The same 12-mode plain-nylon G3 model, 64 spatial intervals, physical local potential, gradient, fixed-end force observer, gauge and drift regulator are used. `Model` and the independent physical `Rk4` source blocks are byte-identical to the baseline; hashes are recorded in [the report](NonlinearStringMassCorrectionExperiment.json). There is no coefficient fitting, force gain, output normalisation, time alignment, new sound control or recording-corpus selection.

## Weighted rank-one update and rest history

The physical force distribution is `b_j=sin(j*pi*0.173)`. Crucially, physical viscous damping stays `C_j=2*m*eta`; substituting `Mtilde` into this coefficient would change the declared loss model. Let `g` be the original regulated SAV coupling vector, `psi` the original auxiliary state at `n-1/2`, and define

```
beta = h²/4,
D_j = Mtilde_j + C_j h/2,
z_j = g_j/D_j,
r_j = [2 Mtilde_j q_j[n] - (Mtilde_j-C_j h/2) q_j[n-1]
       - h² K_j q_j[n] + beta g_j (g' q[n-1]-4 psi)
       + h² b_j F[n]] / D_j,

q[n+1] = r - beta z (g' r)/(1+beta g' z),
psi[n+1/2] = psi[n-1/2] + g'[q[n+1]-q[n-1]]/2.
```

This is the Sherman–Morrison inverse of `D + beta g g'`. For nonnegative physical damping and positive numerical masses, `D` is positive and the rank denominator is at least one. The force, physical linear restoring force and physical nonlinear coupling all pass through the same `D_j`. No independent input correction is applied.

For a release from rest, consistent history is

```
q_j[-1] = q_j[0] - h²/(2 Mtilde_j)
                    [K_j q_j[0] + (grad Phi(q[0]))_j],
psi[-1/2] = sqrt(2 Phi((q[0]+q[-1])/2) + C0).
```

The linear part is exactly `q_j[-1]=q_j[0] cos(theta_j)`. The nonlinear part retains second-order history accuracy; its acceleration uses modified numerical inertia. The continuous physical initial displacement and zero velocity are unchanged, but this is not an exact backward nonlinear solution.

## Correct energy, physical work, and negative control

The numerical ledger is

```
E[n-1/2] = sum_j Mtilde_j [(q_j[n]-q_j[n-1])/h]²/2
         + q[n]' K q[n-1]/2 + (psi[n-1/2]²-C0)/2.
```

Writing `qbar=(q[n]+q[n-1])/2` and `vminus=(q[n]-q[n-1])/h` gives

```
E+C0/2 = vminus' [Mtilde-h² K/4] vminus/2
       + qbar' K qbar/2 + psi²/2.
```

The kinetic coefficient is `Mtilde_j cos²(theta_j/2)`, positive strictly below Nyquist. With the same centred velocity `vcenter=(q[n+1]-q[n-1])/(2h)`, the physical force and physical damping ledger remains

```
E[n+1/2]-E[n-1/2] = h F[n] b'vcenter - h sum_j C_j vcenter_j².
```

The numerical energy still differs from the physical continuous Hamiltonian. For one linear mode released from rest at amplitude `A`, `E_initial/H_physical_initial = cos²(theta_j/2)`, where `H_physical_initial=K_j A²/2`. At mode 12/48 kHz the ratio is 0.9760718531. Keeping physical stiffness does not turn a conserved discrete energy into the exact physical energy.

The added negative control deliberately computes the same stencil ledger with **physical mass in place of numerical mass**, leaving the corrected recurrence untouched. Its relative drift is 0.289318%, 0.0727645% and 0.0182490% at 48/96/192 kHz, exceeding the fixed `1e-5` sensitivity gate at every rate. The correct ledger in the same linear run balances within `4.08e-13`. The wrong-mass control is not labelled an estimator of the continuous physical Hamiltonian.

## Static and dynamic force response

For linear static input the recurrence reduces to `Kq=bF`, exactly the physical equilibrium. The explicit test initialises all 12 coordinates to `q_j=b_j*0.01 N/K_j`, with constant force already present before time zero, so the correct prehistory is `q[-1]=q[0]`. It then applies that unscaled physical force for 200 ms at each rate. Worst relative displacement error is `4.09e-14`. This is a physical equilibrium check, not a rescaling of output to match a target.

Dynamic compliance is still approximate. A direct algebraic frequency-response calculation for a linear modal input at angular frequency `Omega` gives

```
H_j,num(Omega) = 1 / [K_j - (4 Mtilde_j/h²) sin²(Omega h/2)
                         + i (C_j/h) sin(Omega h)],
H_j,physical(Omega) = 1 / [K_j-m Omega²+i C_j Omega].
```

Collocated displacement compliance is `b' H b`; work-conjugate numerical port mobility uses `i sin(Omega h)/h`, whereas the physical continuous mobility uses `i Omega`. They must not be interchanged silently. At the **physical undamped modal frequency** `Omega=omega_j`, for positive damping, modal displacement compliance has ratio `H_num/H_physical=theta_j/sin(theta_j)`. This is not a claim about the precise frequency of a damped resonance maximum.

For mode 12, that displacement-response excess is +1.626386%, +0.403159% and +0.100577% at 48/96/192 kHz. At that same frequency the centred modal velocity response is `1/C_j`, matching physical velocity response, despite the displacement mismatch. This illustrates why displacement/force observation and discrete power-port velocity need separate qualification.

Preserving physical `C` also does not give exact damped poles. In the tested underdamped linear domain,

```
rho_j = sqrt[(Mtilde_j-C_j h/2)/(Mtilde_j+C_j h/2)],
sigma_j,num = atanh[(m/Mtilde_j)*eta*h]/h.
```

At mode 12/48 kHz this numerical amplitude-decay rate is 0.801468% below physical `eta=1 s^-1`, and 0.801326% below `eta=100 s^-1`. These analytic biases are recorded separately from time-domain measurements. No damping correction was fitted or added. A driven physical-ODE comparison and a reciprocal moving-bridge port test remain necessary before any coupling or audio qualification.

## Fixed comparison results

The lossless comparison uses the same 4 mm smoothed-triangular rest pluck, 100 ms duration and 4800 common sample times as the baseline. RK4 still integrates physical `m`, `K` and `grad Phi`. Its 384/768 kHz self-convergence errors remain `2.14584e-7` for modal displacement and `1.71059e-6` for physical endpoint force.

| Rate | Original regulated force error | Stiffness candidate force error | Mass candidate force error | Mass candidate modal error |
|---|---:|---:|---:|---:|
| 48 kHz | 27.4846% | 0.604447% | 0.254797% | 0.0481111% |
| 96 kHz | 11.0574% | 0.150006% | 0.0632809% | 0.0119914% |
| 192 kHz | 2.86367% | 0.0374451% | 0.0158076% | 0.00299649% |

Mass-candidate regulated force orders are 2.00951 and 2.00115; modal orders are 2.00437 and 2.00066. Its unregulated force errors are 0.781711%, 0.196475% and 0.0491904%. These are errors for the same finite-dimensional string, not evidence of better recordings or guitar realism.

All 12 linear modes match their physical cosine trajectories over 200 ms at all rates: worst displacement error `1.91e-13 m`. The original gradient, quadrature, symmetry, mode-transfer and small-amplitude gates pass. All 27 one-mode weak-Duffing pitch cases at 48/96/192 kHz and 1/2/4 mm pass the unchanged 3% gate, using physical stiffness; maximum relative fractional-shift error is about 0.140%. This check does not establish multimode or coupled pitch behaviour.

The original repeated-force/release test remains unchanged: 2 mm rest pluck; three 1 N, 1 ms raised-cosine pulses at 50/100/150 ms; physical `eta=1 s^-1` until 200 ms then `100 s^-1`; 400 ms total. Its all-rate extension uses both regulators and both original gauges. Maximum numerical power-balance error is `1.97e-12`; regulated final energy fractions are approximately `9.03e-18`, `8.81e-18` and `8.76e-18` at 48/96/192 kHz. Tested auxiliary states remain nonnegative and rank denominators at least one.

**Exactly two inherited control expectations fail.** The original unregulated exact-potential release residues exceeded `1e-4`; the mass candidate's residues are `8.79866e-5` and `9.17850e-5`, so they now satisfy that fixed release criterion. The harness still demands this unregulated control fail and therefore reports two failures, exit 1. No threshold or pulse was adjusted to restore that expectation. All other assertions pass, including the new wrong-mass energy control. A future combined harness must explicitly represent these scheme-dependent control outcomes rather than conceal them by weakening a threshold.

## Provenance, scope and reproduction

The weighted inverse and ledger above are independently derived for the frozen recurrence. [Risse, Hélie and Bilbao, DAFx 2025, equations 14–19](https://dafx.de/paper-archive/2025/DAFx25_paper_24.pdf) are the primary source for the inherited drift-regulated, power-balanced SAV method. [Van Walstijn, Chatziioannou and Bhanuprakash, JSV 569 (2024), equations 35–39 and 76–78](https://pureadmin.qub.ac.uk/ws/portalfiles/portal/502240620/1_s2.0_S0022460X23004170_main.pdf) provide exact-pole and physical-bandwidth precedent for a different averaged-stiffness scheme. Neither publication is cited as validating this numerical-inertia candidate or its acoustic quality.

The prior [coupled SAV rejection](../Docs/decisions.md#2026-08-30--local-slope-sav-stringbridge-candidate-rejected-by-public-nonlinear-oracle) remains applicable: correct linear poles and a balanced ledger previously coexisted with failed acoustic gates. This candidate has no moving-boundary solve, string/body port validation, measured-recording comparison or realtime callback qualification.

The JSON includes all baseline and candidate measurements, analytic linear-response biases, exact failure text, compiler and source/executable hashes. The independent source and raw results are retained under `/tmp/acustra-exact-pole-experiment-20260908/mass-*`. Reconstruct in a fresh directory from the original, without applying the stiffness patch:

```sh
experiment_dir=$(mktemp -d /tmp/acustra-mass-correction-repro.XXXXXX)
mkdir "$experiment_dir/Tools"
cp Tools/NonlinearStringPrototype.cpp "$experiment_dir/Tools/"
patch -d "$experiment_dir" -p1 < Tools/NonlinearStringMassCorrectionExperiment.patch
clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic \
  "$experiment_dir/Tools/NonlinearStringPrototype.cpp" -o "$experiment_dir/candidate"
"$experiment_dir/candidate" --self-test "$experiment_dir/result"
```

Expected result is `Prototype audit failures: 2`, exit 1, for the two documented inherited release-control expectations. No production or existing prototype file was changed by this experiment.
