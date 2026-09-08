# Exact linear poles in the regulated SAV string: isolated experiment

Dated 2026-09-08. **Numerically promising; no engine integration or sound-quality claim.** One algebraic candidate replaces the linear stiffness used by the time-stepping scheme. At 48 kHz its regulated physical endpoint-force error against the unchanged 768 kHz RK4 reference falls from 27.4846% to 0.604447%; modal displacement error falls from 4.38066% to 0.0832520%. Both errors converge approximately quadratically at 96 and 192 kHz.

The original prototype remains unchanged, SHA-256 `227d13ddeccfca9efa1499e30b54301a9542bb065d3eda04e9b3ce0962c3afd5`. A fresh baseline run exactly reproduces all 67 preserved measurements and has zero failures. Applying [the experimental patch](NonlinearStringExactPoleExperiment.patch) produces **two inherited negative-control expectation failures**, retained below. Its additional numerical checks pass. This is not an all-green shipping implementation.

## Candidate and physical model

The frozen [prototype](NonlinearStringPrototype.md) uses 12 unnormalised sine modes, 64 spatial intervals and the declared plain-nylon G3 parameters. The highest retained physical linear frequency is 2372.976 Hz. The primary comparison is a 4 mm, Gaussian-smoothed triangular initial displacement, released from rest, over 100 ms. Initial conditions, spatial quadrature, gauge `C0 = 1e-12 J`, regulator `lambda = 1000 s^-1`, physical nonlinear potential and physical RK4 reference are unchanged. The unregulated `lambda = 0` control is also retained. No coefficient was fitted and no recorded-guitar corpus was used.

Let `h = 1/fs`, `m = mu L/2`, and let the physical modal stiffness and frequency be

```
K_j = (L/2) [T k_j² + EI k_j⁴],   k_j = j*pi/L,
omega_j = sqrt(K_j/m),             theta_j = h*omega_j.
```

For the undamped, unforced linear recurrence,

```
q_j[n+1] - 2 q_j[n] + q_j[n-1] = -(h²/m) Ktilde_j q_j[n],
```

matching its roots to `exp(±i theta_j)` gives

```
Ktilde_j = (4m/h²) sin²(theta_j/2)
         = K_j sinc²(theta_j/2),   sinc(x) = sin(x)/x.
```

The candidate stores `Ktilde` only inside `Sav`. It uses it consistently in the recurrence, rest preload and numerical energy. `Model::stiffness`, `Model::evaluate`, `Model::bridgeForce`, `Rk4` and the physical Duffing prediction remain unchanged. The report authenticates the unchanged physical-model and RK4 source blocks separately.

The local exact nonlinear potential remains

```
Phi(q) = integral_0^L (EA-T)/2 * [sqrt(1+u_x²)-1]² dx.
```

It is evaluated with the original stable expression and the same physical gradient. The fixed-end force observer still evaluates physical tension, bending and nonlinear stress:

```
F_bridge(q) = sum_j (T k_j + EI k_j³) q_j
           + (EA-T) [sqrt(1+s²)-1] s/sqrt(1+s²),
s = sum_j k_j q_j.
```

The corrected stiffness is not substituted into this observer. There is no amplitude normalisation, force gain, time alignment or fitted pitch correction in the error comparison. Modal error is the relative Euclidean RMS over all modal coordinates and common sample times; force error is the relative RMS of this physical observer.

## Preload, energy and bandwidth

For the declared zero initial velocity, the history is

```
q[-1] = q[0] - h²/(2m) [Ktilde q[0] + grad Phi(q[0])],
psi[-1/2] = sqrt(2 Phi((q[0]+q[-1])/2) + C0).
```

Its linear part is exactly `q_j[-1] = q_j[0] cos(theta_j)`. The nonlinear part is the existing second-order rest-history approximation, not an exact backward nonlinear solution. Since `Ktilde = K + O(h²)`, this replacement changes the Taylor history only at `O(h⁴)` for fixed physical frequencies.

The balanced numerical energy is

```
E[n-1/2] = (m/2) ||(q[n]-q[n-1])/h||²
         + (1/2) q[n]' Ktilde q[n-1]
         + (psi[n-1/2]²-C0)/2.
```

With `qbar=(q[n]+q[n-1])/2` and `vminus=(q[n]-q[n-1])/h`, its shifted form is

```
E + C0/2 = (1/2) vminus' [m I-h² Ktilde/4] vminus
         + (1/2) qbar' Ktilde qbar + psi²/2.
```

The effective kinetic coefficient is `m cos²(theta_j/2)`. It is positive below Nyquist, tends to zero at Nyquist and provides poor conditioning near that boundary. The constructor therefore checks the **physical** condition `0 < omega_j < pi*fs` before applying the bounded sine map. Manufactured frequencies at 0.99, 1.00, 1.01 and 2.01 times Nyquist are checked at all three rates; only 0.99 is accepted. The bound on `Ktilde` must not conceal aliased physical modes. Keeping linear modes below Nyquist also does not prove absence of nonlinear temporal aliasing.

This is a **modified numerical Hamiltonian**, not the continuous physical Hamiltonian `m||qdot||²/2 + q'Kq/2 + Phi(q)`. For a single linear mode released from rest with amplitude `A`,

```
E_initial = (m A²/2h²) sin²(theta),
H_physical_initial = m omega² A²/2,
E_initial/H_physical_initial = sinc²(theta).
```

For mode 12 at 48 kHz, `Ktilde/K = 0.9919853244` and this energy ratio is `0.9682489538`. Exact linear pole frequency is compatible with a different numerical energy value. A deliberately inconsistent control, keeping the corrected recurrence but inserting physical `K` in this stencil ledger, drifts by 0.2893%, 0.07276% and 0.01825% at 48/96/192 kHz. It is explicitly labelled an incorrect stencil ledger, not an estimator of the continuous physical Hamiltonian. The correct linear ledger stays within `1.04e-13` relative error in that sweep.

## Force injection and damping limitations

The physical input remains `b F[n]`, with `b_j = sin(j*pi*0.173)` and force in newtons. No mode-dependent input scaling was introduced. The existing centred viscous force remains `2m eta vcenter`, where `vcenter=(q[n+1]-q[n-1])/(2h)`. Multiplying the corrected recurrence by this same velocity yields the unchanged numerical work identity

```
E[n+1/2]-E[n-1/2] = h F[n] b'vcenter - h*2m*eta ||vcenter||².
```

However, exact homogeneous linear poles do not make a forced or damped solution exact. In particular, unscaled static input gives `q_j = b_j F/Ktilde_j`, rather than `b_j F/K_j`. Its relative static compliance error is `K_j/Ktilde_j - 1`. Across these 12 modes its maximum is +0.807943%, +0.201255% and +0.0502682% at 48/96/192 kHz. For the fundamental at 48 kHz it is +0.00548613%.

Multiplying the input by `Ktilde/K` could recover linear DC compliance, but would also change the input port and its work accounting; scaling the nonlinear force introduces further consistency questions. Such a change would be a second candidate. It was not made. Physical-potential versus numerical-inertia formulations require their own complete derivation and validation. The present endpoint-force comparison is an observation of a fixed-end string, not a power-transferring connection to a moving bridge.

## Results against the same physical RK4 reference

Reference self-convergence, 384 versus 768 kHz, is unchanged: modal relative error `2.14584e-7`, physical force relative error `1.71059e-6`. All time comparisons sample the same 4800 instants at 48 kHz. These errors compare time integration of the same finite-dimensional model, not convergence to a complete guitar or an untruncated string PDE.

| Rate | Baseline regulated modal error | Corrected modal error | Baseline regulated force error | Corrected force error |
|---|---:|---:|---:|---:|
| 48 kHz | 4.38066% | 0.0832520% | 27.4846% | 0.604447% |
| 96 kHz | 1.54006% | 0.0206898% | 11.0574% | 0.150006% |
| 192 kHz | 0.396839% | 0.00516615% | 2.86367% | 0.0374451% |

Corrected regulated force orders are 2.01060 and 2.00217; modal orders are 2.00856 and 2.00176. Corrected **unregulated** force errors are 1.08534%, 0.271315% and 0.0678307%, with modal errors 0.260828%, 0.0657745% and 0.0164794%. This control separates the effect of pole correction from the additional benefit of the regulator.

All 12 undamped linear modes reproduce physical cosines over 200 ms at all three rates: worst absolute displacement error `8.57557e-16 m`. Potential gradients, spatial quadrature, signed symmetry, local intermodal transfer, zero state and the quadratic small-amplitude limit retain their original gates. Worst lossless numerical energy-balance error is `4.70e-13` relative.

The one-mode pitch test adds 96 kHz to the original 48/192 kHz sweep without changing the 3% gate. It uses 1, 2 and 4 mm modal amplitudes, the original physical `K`, and the physical weak-Duffing prediction `delta f/f = 3 beta A²/(8K)`. All 27 cases pass; maximum relative fractional-shift error is 0.162744%. For the exact potential at 48 kHz, measured shifts are 0.201703, 0.806401 and 3.220720 cents, versus weak predictions 0.201749, 0.806855 and 3.225166 cents. The report includes both shifts over the measured linear baseline and shifts over the physical linear frequency. Linear zero-crossing estimator bias is at most `3.37e-7` cents, so normalisation does not conceal a tuning correction. This is a weak, one-mode nonlinear check; individual pitch errors are not asserted to decrease monotonically with sample rate, and it does not establish nonlinear pitch behaviour near Nyquist or in a coupled guitar.

The repeated-force test retains its 2 mm initial pluck, three 1 N raised-cosine pulses of 1 ms at 50/100/150 ms, `eta=1 s^-1` until 200 ms then `100 s^-1`, and a 400 ms duration. The added exact-potential rate sweep retains both regulators and gauges `1e-14/1e-10 J`. Regulated final numerical energy fractions remain about `8.74e-18` at every rate and gauge; worst power-balance error is `5.57e-12`, auxiliary variables stay nonnegative and rank denominators remain at least one.

**Changed negative-control behaviour:** the original harness demands the unregulated exact-potential release fail the fixed `abs(E_final/E_initial) < 1e-4` criterion. Baseline residues are `1.28554e-4` and `1.33221e-4`; corrected residues are `8.87182e-5` and `9.25334e-5`. They now satisfy that release criterion, so both original negative-control expectations fail. Neither threshold nor forcing was changed to restore a desired failure. This sensitivity belonged to the original scheme and case. The nonzero unregulated residue remains many orders above the regulated result, but the old binary control cannot certify that distinction for this candidate. The patch's self-test consequently exits 1 with exactly two failures; further harness work must represent the two schemes' controls explicitly before calling the combined suite passing.

## Primary algorithm provenance and scope

[Risse, Hélie and Bilbao, DAFx 2025, equations 14–19](https://dafx.de/paper-archive/2025/DAFx25_paper_24.pdf) supply the drift-regulation and interleaved power-balance method used by the frozen prototype. They do not establish this experiment's sound quality. Its linear correction is derived explicitly above for the prototype's particular recurrence.

[Van Walstijn, Chatziioannou and Bhanuprakash, JSV 569 (2024), 117968, equations 35–39 and 76–78](https://pureadmin.qub.ac.uk/ws/portalfiles/portal/502240620/1_s2.0_S0022460X23004170_main.pdf) provide primary precedent for matching linear poles, consistent initial histories and excluding physical modes at/above Nyquist. Their temporally averaged stiffness has a different correction: in the undamped case their `Khat = 4m/h² tan²(theta/2)`. Substituting that expression into this prototype's **unaveraged** stiffness term would be wrong. The present `sin²` formula is not claimed to reproduce their whole forcing or damping scheme.

The [earlier coupled SAV experiment](../Docs/decisions.md#2026-08-30--local-slope-sav-stringbridge-candidate-rejected-by-public-nonlinear-oracle) already reproduced linear poles and conserved a numerical energy while failing acoustic profile, pitch and finite-render DC gates. This result does not overturn that rejection. It supports further isolated numerical work on the newly regulated solver. Real recordings, moving-boundary force/power consistency, both transverse planes, longitudinal dynamics, model bandwidth and callback cost remain separate requirements.

## Reproduction and artifacts

[The JSON report](NonlinearStringExactPoleExperiment.json) preserves the complete fresh baseline and candidate measurements, exact failure text, source and executable hashes, compiler identity and unchanged-block checks. The candidate source and raw logs are retained in `/tmp/acustra-exact-pole-experiment-20260908`; the patch is sufficient to reconstruct that source from the frozen original. No engine, existing prototype, CMake or corpus file was changed.

From the repository root, create an isolated copy, leaving the original untouched:

```sh
experiment_dir=$(mktemp -d /tmp/acustra-exact-pole-repro.XXXXXX)
mkdir "$experiment_dir/Tools"
cp Tools/NonlinearStringPrototype.cpp "$experiment_dir/Tools/"
patch -d "$experiment_dir" -p1 < Tools/NonlinearStringExactPoleExperiment.patch
clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic \
  "$experiment_dir/Tools/NonlinearStringPrototype.cpp" -o "$experiment_dir/candidate"
"$experiment_dir/candidate" --self-test "$experiment_dir/result"
```

Expected final line: `Prototype audit failures: 2`, exit status 1, with the two retained unregulated release-control expectations. Do not treat that status as an unexpected build failure or a passing production gate.
