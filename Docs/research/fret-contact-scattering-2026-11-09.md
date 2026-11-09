# Fret-contact scattering audit — 2026-11-09

This is deferred modelling research, not a shipped fret-buzz implementation.
The shipping bridge-wave clip is removed because it responds to a travelling
potential's arbitrary common reference rather than physical displacement.

For folded travelling potentials `a,b`, physical displacement before contact
is `z=b-a`. A causal local scatter must remember the unmodified incoming
histories. Let the persistent outgoing potential offset be `O`, with
`a_out=a-O`, `b_out=b+O`. The physical junction pairs one incident wave and
one outgoing wave: `y=z+O`, not `z+2O`. For `p=delta(z)` and
`delta(O)=-alpha*p`, `0 <= alpha <= 1`, the outgoing velocity-power change is
`-2 alpha (1-alpha) p^2`, so an arbitrary bounded, time-varying resistive
strength is passive at fixed integer geometry.

The construction also extends to coalesced fractional supports. Let the
signed interpolation basis be `q`, `S=q.q`, and keep each support's previous
unmodified incoming sample. With `p=q.(incoming-previousIncoming)`, output
samples are `incoming+q*O`, physical midpoint displacement is
`q.incoming+(S/2)*O`, and `delta(O)=-beta*p`. The power change is
`-(2 beta-beta^2 S) p^2`, passive for `0 <= beta <= 2/S`. A physical resistance
ratio `r=R/Z` gives `beta=2r/(4+rS)`. Duplicate tap indices must be coalesced
before calculating this norm, especially on short upper strings.

Independent displacement-ring and velocity-ring experiments agree to about
2.5e-15 over 72 fixed fractional geometry/mode/phase cases, including overlap;
the exact work ledger agrees to about 5.6e-16. A full-ring spatial-difference
norm is not an energy oracle at a scatter interface: it incorrectly includes
the jump from incident to reflected potential. Saved incident histories (or
an independently propagated velocity ring) are necessary at those boundaries.
A memoryless displacement projection also cannot instantaneously impose a
bound on an already penetrating initial state without accounting for work.

These fixed-geometry results do not solve a moving fret contact. Bends,
slides, replucks and construction changes need a consistent geometry/state
migration, and a physical crown clearance/stiffness needs units and evidence.
The former authored 0.072/0.050 potential thresholds provide neither. A
quantized or frozen next-fret approximation would introduce another model
limitation. No replacement is shipped until those questions are resolved.
