# Frozen amendment A, revision 2: equivalent acceleration/half-velocity implementation

Date 2026-09-08. Parent protocol: protocol-v1.md, SHA cd826a1cd20322bea306be55f1ae591cf6b93093e3c8b117786de6a13ce383bd. This is one numerical implementation correction, with the same 13-coordinate physical fixture, PSD numerical mass, potential, regulator, load, damping schedule, test cases, physical RK4 references, sample rates and numerical gates. Preserve all three original v1 failures and its source/output hashes. Do not fit coefficients, relax thresholds or change audio/engine code.

## Exact-arithmetic derivation

Write the same centred recurrence as

```
Mn*a + C*vcenter + K*z + g*psiBar = b*F,
vminus=(z_n-z_(n-1))/h,
vplus=(z_(n+1)-z_n)/h,
a=(vplus-vminus)/h,
vcenter=(vplus+vminus)/2.
```

The existing SAV update gives

```
psiBar = psi + (h/2) g'*vminus + (h^2/4) g'*a.
```

Consequently, with A=Mn+h*C/2 and beta=h^2/4,

```
(A+beta*g*g')a = b*F-K*z-g*psi-C*vminus-(h/2)*g*(g'*vminus).
```

Solve this SPD diagonal-plus-rank-one system using the same dense A inverse and Sherman–Morrison identity. Then update

```
vplus=vminus+h*a,
znext=z+h*vplus,
psiNext=psi+(h/2)*g'*(vplus+vminus).
```

Store vminus independently across steps. Compute acceleration a and centred velocity directly; do not reconstruct acceleration by subtracting nearly equal positions. Reconstruct body and string force independently from their own physical coefficients and the same solved a,vcenter. Do not copy one branch's force into the other.

The regulator uses the same lifted coordinates, qbar=z-h*vminus/2, L1 norm of vminus, sign(vminus), gauge and velocity floor. For released rest, set vminus=+(h/2)*Mn^-1*(K*z+grad Phi); this positive sign follows from the existing backward rest history. Constant equilibrium uses vminus=0. Steady sinusoidal tests initialise the same analytic half-step history, preserving the physical phase and forcing. No frequency, velocity or force gain is introduced.

The exactly equivalent cross-time ledger is evaluated without a subtractive history reconstruction:

```
E = .5*vminus'*(Mn-h^2*K/4)*vminus
  + .5*qbar'*K*qbar + .5*(psi^2-C0).
```

Use Ms_num,Ks for the string branch and mb,kb for the body branch. They sum to the joint expression. Work remains h*F*b'*vcenter and physical dissipation h*vcenter'*C*vcenter.

## Audit repair and frozen outcomes

Add explicit finite guards to every stepped monitor: all z,v,a components, independent forces, energies and psi must be finite before maxima are accumulated. This repairs the documented v1 coverage omission and does not change a physical equation or tolerance. Keep no-reference fields null.

A changes floating-point evaluation, not the exact-arithmetic recurrence. The expected outcome is that the 192 kHz/220 Hz branch-force closure failure disappears while the two discontinuous-damping convergence failures remain. This is an expectation, not a replacement gate: record every actual failure, including unexpected ones, without modifying the fixture or thresholds. Do not claim the candidate qualified merely because two failures were expected.

Run A in a fresh output directory with a separately hashed source and executable. Re-run the same algebra/reference gates first; stop qualification if reference gates fail. Independent equation review has confirmed the rank solve, positive released-rest sign, full physical cross damping, regulator coordinates and separate ledgers. No production or audio claim is authorized.

## Controlled finite-guard sensitivity checks

Before any physical reference qualification, inject quiet NaN and positive infinity into otherwise valid observations, one field at a time: position, velocity, acceleration, stored half-velocity, psi, each reconstructed force class, and accumulated work. Every injected nonfinite value must be explicitly rejected before maxima or JSON formatting can hide it. Catching the deliberate error is a successful negative-control sensitivity result, not a candidate failure. A missed injection is a genuine audit failure. Keep and report all checks; do not change physical trajectories or parameters. This revision adds the root-requested guard proof before any A run.
