# Frozen amendment B: centred-cell integration of the declared damping jump

Date 2026-09-08. Parent protocol: protocol-v1.md, SHA cd826a1cd20322bea306be55f1ae591cf6b93093e3c8b117786de6a13ce383bd. Apply only after separately running and preserving amendment A. Keep the physical fixture, forcing, reference, rates, gauges and all v1 gates unchanged. This is a single derived timing correction, not a damping fit.

## Derivation and exact change

The physical string damping is Cs(t)=2*eta(t)*Ms_phys, with eta=1 before t_j=.2 s and eta=100 after it. Body damping is unchanged. Physical RK4 continues to split exactly at t_j with the appropriate interval-side coefficient.

The half-step momentum update is associated with the centred cell [t_n-h/2,t_n+h/2]. The grid-aligned jump cell contains half of each physical coefficient. Its exact coefficient average is therefore

```
eta_cell[n] = 50.5,  if n=fs/5;
              1,   if n<fs/5;
              100, if n>fs/5.
```

This is the only changed sample in the only driven-release case. The value is fixed by integrating the declared piecewise coefficient over that cell. Do not change jump time, interpolate more samples, alter cb, or substitute numerical mass in Cs.

For a physical solution with continuous z and velocity but different one-sided accelerations at the jump,

```
delta_tt z(t_j) = (a_minus+a_plus)/2 + O(h),
vcenter(t_j) = v(t_j) + O(h).
```

The physical one-sided equations use physical M. Averaging those equations gives the leading jump equation with (C_minus+C_plus)/2. Since Mn=M+O(h^2) for this fixed finite model, numerical mass modification does not change the leading argument. Using right-only C_plus for the whole centred cell leaves an O(1) local acceleration defect and an O(h) accumulated state error. The coefficient average removes that leading defect; the remaining jump contribution can be O(h^2) globally. This is a consistency argument, not a proof of every measured convergence rate.

The average of C(t) is NOT exact integration of C(t)*v(t), and SAV/regulator and numerical-inertia errors remain. Cs_cell remains physical PSD, so the same exact numerical work ledger applies using Cs_cell at that step. Keep the separate branch power signs and independent force reconstruction from A.

## Observer distinctions at the discontinuity

At the single jump instant, centred acceleration/effort tends toward a two-sided value, while the frozen physical RK4 point observation is right-sided. This can create a one-sample floor in pointwise/common-grid force comparisons even when state convergence improves. Do not omit that sample, change the reference, renormalise force, or relax a gate. Retain the existing common sample times and report this limitation if relevant.

For same-state force-gap decomposition, use C_cell for the SAV-versus-direct-gradient and numerical-versus-physical-mass comparisons so each isolates its stated mechanism. Also report the difference from the physical instantaneous right-sided C at the jump as a separate damping-discretisation gap. Do not relabel a coefficient-timing difference as a nonlinear stress or mass error. The actual candidate-versus-physical-RK4 port error remains unchanged in definition.

## Execution and stop conditions

Run a separately hashed B source/executable in a fresh directory after A's output is immutable. All parent cases and gates stay active, including reference self-convergence and independent branch-force tolerance. Preserve every B failure and all A/v1 outcomes. Do not infer success from expected removal of the leading jump defect. No output will qualify a full-band guitar, overturn the earlier coupled acoustic rejection, or authorize engine integration.

Independent reviewer has confirmed the leading averaged one-sided equation and PSD work ledger; their qualification is explicit: this is not exact C*v integration and is not a guarantee that the fixed observed convergence gates pass.
