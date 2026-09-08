# Frozen proposed protocol: 13-coordinate moving-endpoint SAV audit

Date: 2026-09-08. Status: **PROTOCOL ONLY; awaiting root review before implementation.** No candidate trajectory, fit, or gate result has been generated for this protocol. This is a numerical power-port audit of one declared synthetic fixture, not engine integration or sound-quality validation. Every failed gate will be retained; changes require a new protocol version and a reason recorded before a fresh run.

## 1. Sources and scope

Use the physical constants, physical exact local-slope potential, 12 sine modes, 64 quadrature intervals, Gaussian-smoothed triangular initial coefficients, and independent physical RK4 convention from frozen `Tools/NonlinearStringPrototype.cpp`, SHA-256 `227d13ddeccfca9efa1499e30b54301a9542bb065d3eda04e9b3ce0962c3afd5`. The separately retained modified-mass candidate has source SHA-256 `bc319068924c6a4a47339a053abee77dd155fe43091955b79a0b4be1c5ce16ad` and report `Tools/NonlinearStringMassCorrectionExperiment.json`. Neither source nor any current Tools artifact, engine file, CMake file, or corpus is to be edited during this audit's initial /tmp implementation.

There are exactly 13 displacement coordinates: 12 original modal coordinates q and one bridge/end-point displacement y. No extra string modes, fitted resonator, radiation filter, contact law, force gain, state-dependent output correction, or recording score is permitted. A later 24/48-mode spatial convergence study would be a different protocol, not a way to rescue this one's gates.

The 2026-08-30 decision `Docs/decisions.md`, heading “local-slope SAV string/bridge candidate rejected by public nonlinear oracle,” must be cited in the final audit. That earlier candidate already matched linear poles and conserved a numerical energy, yet failed acoustic profile, pitch and finite-window DC gates. Passing this audit does not overturn that rejection, qualify a guitar sound, or authorize integration.

Primary method references, already inspected, are Risse, Hélie and Bilbao (DAFx 2025), equations 14–19, https://dafx.de/paper-archive/2025/DAFx25_paper_24.pdf, and Van Walstijn, Chatziioannou and Bhanuprakash (JSV 569, 2024, 117968), equations 35–39 and 76–83, https://pureadmin.qub.ac.uk/ws/portalfiles/portal/502240620/1_s2.0_S0022460X23004170_main.pdf. The lifting and PSD extension below are independently derived for this experiment; neither paper is claimed to validate this fixture or its acoustic quality.

## 2. Fixed physical fixture and endpoint lifting

Use x=0 at the moving endpoint, x=L at the fixed nut, w(x)=1-x/L, and

```
z=(q_1,...,q_12,y),
u(x)=sum_n q_n sin(k_n x)+w(x)y,   k_n=n*pi/L,
s(x)=u_x=sum_n k_n q_n cos(k_n x)-y/L.
```

The original physical string has L=.650 m, mu=1140*pi*(.001 m)^2/4 kg/m, T=mu*(2*L*195.99771799 Hz)^2 N, EA=2.7e9*pi*(.001 m)^2/4 N, and EI=310e-6 N m^2. Let m=mu L/2, c_n=mu/k_n, d_n=c_n/m=2/(L k_n), and m_yy=mu L/3. Physical string matrices are

```
Ms=[[m I,c],[c',m_yy]],
Ks=diag(K_1,...,K_12,T/L),
K_n=(L/2)*(T k_n^2+EI k_n^4).
```

Tension cross terms vanish because the sine functions vanish at both ends; the linear lift has zero curvature. The physical mass Schur complement is S=m_yy-m*sum(d_n^2)>0 for this finite basis.

One explicitly unfitted mechanical endpoint fixture is fixed now:

```
mb=.05 kg, fb=220 Hz, Qb=30,
kb=mb*(2*pi*fb)^2 N/m,
cb=mb*(2*pi*fb)/Qb N s/m.
```

These round authoring values are not measured guitar parameters. The physical full matrices are M=Ms+mb*ey*ey', K=Ks+kb*ey*ey', C=Cs+cb*ey*ey'. String damping is **Cs=2*eta*Ms**, with physical Ms, never numerical mass. This corresponds to uniform physical distributed velocity loss. Lossless cases set eta=0 AND cb=0; damped cases use eta=1 or 100 s^-1 with the declared cb.

The nonlinear potential is unchanged except for inserting the lifted slope s:

```
Phi(z)=sum_i W_i*(EA-T)/2*(sqrt(1+s_i^2)-1)^2,
tau_i=(EA-T)*s_i^3/[sqrt(1+s_i^2)*(sqrt(1+s_i^2)+1)],
grad_q Phi=R' W tau,   grad_y Phi=-(sum_i W_i*tau_i)/L.
```

W are the same metre-valued trapezoidal weights; use the stable non-cancelling strain expression. The linear control sets Phi=0 without altering physical M or K.

Physical external force is the original point force at a=.173L, with the complete load vector

```
b=(sin(k_1*a),...,sin(k_12*a),1-a/L).
```

Its endpoint component is .827 and must not be omitted. Gaussian width .012L belongs to the original initial displacement, not this point-force vector. Input power is F*b'v. Loaded-equilibrium tests below are static point-force preloads, not a plectrum-contact or finger-release model.

## 3. One numerical candidate and one targeted negative control

At each sample rate h=1/fs, compute physical fixed-end omega_n=sqrt(K_n/m) and

```
D_n=Mtilde_n=K_n*h^2/[4*sin^2(h*omega_n/2)],
Delta_m_n=Mtilde_n-m >= 0,
v_n=(e_n,d_n),
Ms_num=Ms+sum_n Delta_m_n*v_n*v_n'.
```

Thus Ms_num_qq=diag(Mtilde), Ms_num_qy=diag(Mtilde)*d, and Ms_num_yy=m_yy+sum Delta_m_n*d_n^2. The PSD additions correct inertia of the absolute sine projections q_n+d_n*y. They preserve physical static K and force distribution. The full numerical mass is Mn=Ms_num+mb*ey*ey'.

The targeted naive control is Ms_naive=Ms+diag(Delta_m_1,...,Delta_m_12,0), retaining the physical cross mass c. This control can conserve its own numerical energy. It is not expected or required to fail an energy gate; its declared failure is the fixed-end physical reaction identity in section 5. Run its identity checks and one 48 kHz lossless 4 mm coupled trajectory to demonstrate why energy conservation alone is insufficient. Do not select between several mass extensions.

Check BOTH physical coupled generalized frequencies omega(M,K)<pi*fs and full numerical ledger coercivity Mn-h^2*K/4 positive definite before stepping. Log eigenvalue margins and condition estimates for Ms, M, Ms_num, Mn, and full ledger coercivity. The fixed-end correction does not imply exact physical poles for all 13 coupled coordinates. Report every coupled linear pole bias. Also check the separate string/body ledger coercivity; if only the joint ledger is coercive, state this explicitly rather than claiming each branch energy is positive.

Use the inherited regulator in these explicit metre-valued (q,y) coordinates: gauge C0=1e-12 J, lambda=1000 s^-1, original L1 backward-velocity norm/sign correction, same 1e-16 velocity floor. The L1 regulator is basis-dependent; changing to absolute-projection or mass-normalised coordinates would be another candidate. Retain lambda=0 as an observational control, not an expectation that the old release threshold must fail.

The complete dense positive-definite matrix A=Mn+h*C/2 replaces the diagonal denominator of the fixed-end experiment. Use its independently checked solve and the same Sherman–Morrison rank-one correction with beta=h^2/4. Physical nonlinear gradient and force pass through this same A; no port gain is allowed. Initial released rest history is z[-1]=z[0]-h^2/2*Mn^-1*(Kz[0]+grad Phi(z[0])) and psi[-1/2]=sqrt(2*Phi((z[0]+z[-1])/2)+C0). Constant-force equilibrium uses constant history instead. No state reset occurs after forcing stops or damping changes.

## 4. Physical reference and power-port definition

Independent RK4 integrates physical M*zddot+C*zdot+K*z+grad Phi=b*F(t). Its acceleration solve uses physical M, not Mn, and no SAV state or candidate recurrence. Reference steps are 1/384000 and 1/768000 s. Use common 48 kHz sample instants. Retain physical velocity in the reference; compare candidate centred velocity v=(z[n+1]-z[n-1])/(2h) without sinusoidal renormalisation.

Define F_b positive for force exerted by the string on the endpoint oscillator. Numerical port force is the solved reaction, computed independently from each subsystem:

```
F_b,body=mb*delta_tt y+cb*v_y+kb*y[n],
F_b,string=b_y*F - ey'*(Ms_num*delta_tt z+Cs*v+Ks*z[n]+g*psi_bar),
psi_bar=(psi[n+1/2]+psi[n-1/2])/2.
```

Do not inject the instantaneous point-stress observer as this force. The physical RK4 port has the same two branch definitions using physical Ms, physical acceleration and grad Phi.

Track joint and separate string/body cross-time numerical energies, including Ms_num, Ks and the SAV energy in the string ledger and physical mb,kb in the body ledger. The required balances are

```
Delta Es=h*F*b'v-h*F_b*v_y-h*v'*Cs*v,
Delta Eb=h*F_b*v_y-h*cb*v_y^2,
Delta (Es+Eb)=h*F*b'v-h*v'*C*v.
```

Log cumulative signed input work, both independently reconstructed port works and each physical dissipation. Power cancellation must be checked from the two branch force reconstructions; do not make it tautological by copying a single stored work value. Normalise joint errors by max(abs(E0)+integral(abs(input power))dt,1e-18 J). Also report branch errors relative to their own initial energy plus cumulative absolute branch work, with the same 1e-18 J floor. No inference of physical continuous-energy conservation from a balanced numerical ledger is allowed.

## 5. Force identities and honest decomposition of gaps

With y fixed to zero, force removed, eta=0 and Phi=0, physical point force is

```
F_point=sum_n (T*k_n+EI*k_n^3)*q_n = d'*Kq.
```

The corrected cross mass gives exactly this reaction. Naive q-only correction gives d'*diag(m/Mtilde_n)*Kq. Test all 12 single-mode states with modal amplitude 1e-3 m at 48/96/192 kHz, normalising by each state's nonzero analytic force. Corrected relative error must be <1e-12; the naive top-mode error at 48 kHz must exceed 1e-3. Verify the predicted error, rather than simply seeing that the two outputs differ.

For the same fixed-boundary conservative nonlinear state, physical Galerkin reaction is

```
F_weak=d'*Kq+d'*grad_q Phi-grad_y Phi,
F_SAV=d'*Kq+(d'*g_q-g_y)*psi_bar.
```

The physical point-stress observer is F_point=T*s(0)-EI*u_xxx(0)+tau(s(0)). The weak nonlinear force is a finite endpoint cosine projection of tau, generally NOT tau(0) for a 12-mode nonlinear string. In moving, driven or damped cases, weak/point gaps also include finite-basis dynamics and force/damping representation. They must not all be labelled SAV error.

Report separately:

1. Actual numerical solved reaction versus physical RK4 weak reaction at common times: total numerical trajectory/port error.
2. On each numerical state (z,v), recompute direct physical-gradient reaction using Mn and physical K,C,grad Phi. Its difference from the SAV reaction isolates the SAV force approximation at that state.
3. On the same state, replace Mn by physical M in that direct-gradient acceleration solve. The reaction difference isolates numerical inertia at that state.
4. On physical RK4 states, compare physical weak reaction with instantaneous physical point stress. Report this finite-model closure gap, especially in unforced, undamped cases. No threshold assumes it vanishes with time-step refinement.

Record absolute RMS, signed mean and peak differences plus relative RMS against the corresponding whole-reference signal; do not divide by individual near-zero samples. In the fixed-boundary nonlinear check additionally retain the weak/SAV/point conservative formulas above so no moving-boundary inertia can hide their distinction.

## 6. Frozen test matrix and gates

All candidate cases use 48/96/192 kHz. Main candidate lambda=1000; lambda=0 is retained in nonlinear trajectory/energy reports. Linear control has Phi=0. No changes to amplitudes, thresholds or body fixture after results are seen.

A. Algebra and domains: finite-difference full 13-coordinate potential-gradient checks at displacement scales 1e-7,1e-4,.004 m (q uses frozen pluck coefficients; y=scale/10; deterministic direction cos(.73*n)/(n+1), including coordinate 13); relative directional error <1e-6. Matrix symmetry, positive mass and full coercivity checks; sign/denominator/finite-state checks; exact zero state for .1 s; odd symmetry under simultaneous sign reversal of initial displacement/forcing. Identity gates are in section 5.

B. Free coupled trajectories, 100 ms, eta=cb=0: (i) frozen 4 mm Gaussian-smoothed triangular q, y=0, velocity zero; (ii–iii) loaded equilibria solving Kz+grad Phi=b*Fload for Fload=.25 and 1 N, then removing force at time zero. Solve by damped Newton from the linear K^-1 b Fload state, stopping only at relative force residual <1e-12, verify positive physical Hessian and report residual/iterations. Failure to reach that equilibrium is a failed fixture check, not permission to select a different force. Run linear and exact-potential controls for each initial-state family. Both physical RK4 references start from the identical physical displacement and zero velocity; numerical histories follow section 3.

C. Driven release, 400 ms, exact potential: frozen 2 mm Gaussian q, y=0, released rest; three 1 N raised-cosine point-force pulses of 1 ms beginning at .05,.10,.15 s. Use eta=1 until .2 s then 100 s^-1, with fixed physical cb from the declared Qb. At a damping discontinuity RK4 stages use the appropriate one-sided coefficient for their interval; split exactly at .2 s. Gate all numerical power ledgers and state convergence; report late physical-state energy and both branch residues. Do NOT transfer the old isolated string's final-energy <1e-4 release threshold to a body with a different physical decay. Record final continuous physical energy evaluated on numerical and reference states and the SAV drift separately.

D. Driven linear compliance and mobility: F0=.01 N, physical eta=1 and declared cb. Static full-coordinate equilibrium z=K^-1 b F0 with constant history must remain within 1e-8 relative displacement over .1 s. For frequencies [80,196.0099263900649,220,440,1000,2000,3000] Hz, compute physical and numerical matrix resolvents

```
H_phys=[K-Omega^2 M+i Omega C]^-1,
H_num=[K-4*sin^2(Omega*h/2)*Mn/h^2+i*sin(Omega*h)*C/h]^-1.
```

The numerical driven recurrence is initialised from its exact complex steady-state displacement/history and checked over .1 s against that resolvent (<1e-8 relative RMS state error). This verifies implementation independently of long settling transients. The physical linear response is also known analytically; verify the physical RK4 reference at 220 and 2000 Hz over .1 s from its own exact steady state, with physical complex velocity. Compare full complex collocated compliance b'*H*b and mobility: i*Omega for the physical port, i*sin(Omega*h)/h for the numerical work-conjugate port. Also report endpoint displacement/velocity and endpoint-oscillator force transfer. The 3000 Hz drive is a discretisation stress test above the highest retained string mode, not a claim of modeled guitar bandwidth.

Complex transfer error uses one aggregate L2 norm over all declared frequencies divided by the aggregate physical L2 norm; also report maximum absolute error divided by maximum physical magnitude. Do not average per-bin relative errors near antiresonance nulls or tune/align the frequency axis. At 48 kHz both aggregate compliance and mobility errors must be <2%, and reduce at least threefold per doubling. Report per-frequency values and coupled pole-frequency biases even if the aggregate gate passes.

Trajectory accuracy gates for B and C: physical RK4 384/768 kHz self-convergence <1e-6 relative displacement and <1e-5 relative velocity/weak reaction; both whole-vector and endpoint-only state errors are reported. If reference gates fail, retain the failure and stop qualification rather than silently increasing reference rate. Candidate 48 kHz whole-vector displacement and velocity errors <1%, endpoint-only displacement and velocity errors <1%, and solved weak-port reaction error <2%. Each error must decrease at least threefold with each sample-rate doubling unless already below ten times the corresponding reference self-convergence error; in that case mark reference-limited rather than claiming an order. All three rates' measured values and reference floors remain visible. Initial/time alignment and RMS normalisers are fixed; no output gain fitting.

Numerical ledger gates for all stepped cases: maximum joint and both branch relative work residuals <1e-8; independent force-branch mismatch <1e-10 relative to maximum RMS/peak force scale of that run (with explicit absolute values logged); no nonfinite values, negative rank denominator or negative auxiliary state. Report smallest denominator, minimum psi, maximum SAV-vs-Phi drift, full coercivity margin, and physical versus numerical initial energy. No gate requires the naive control or lambda=0 control to lose energy stability. Their intended sensitivities are labelled explicitly.

## 7. Implementation, review and stop conditions

First send this written protocol and its SHA-256 to root and the independent reviewer. Do not implement until root reviews it. Implementation is initially a standalone /tmp source plus independent analysis script, not a change to the retained mass candidate. Record source, protocol, compiler, executable and raw-output hashes. Save every diagnostic and failed assertion; use a fresh output directory for each run. Never overwrite results to make the same protocol appear to have passed.

Root owns any later decision to retain a new Tools artifact or change scope. A pass establishes numerical consistency and convergence for this finite synthetic fixture only. A failure produces a report and a scoped explanation, not an unfrozen coefficient search. No recording corpus, development set, blind data, engine integration or acoustic-superiority claim belongs to this protocol.
