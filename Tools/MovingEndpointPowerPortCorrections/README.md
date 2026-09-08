# Moving-endpoint power-port corrections A and B

These are two sequential numerical corrections to the retained [13-coordinate v1 audit](../MovingEndpointPowerPortAuditV1/README.md). They are isolated experiments and are never linked into Acustra. The original v1 source and its three failures remain unchanged.

**A retains two failed gates; B passes the frozen gates.** Both reports contain all 204 measurement rows, including 22 controlled NaN/Inf guard checks. There are no deleted cases, changed thresholds, omitted event samples, reference substitutions, fitted coefficients, or audio qualification claims.

## What changed

[Amendment A](amendment-A.patch) rewrites the identical centered recurrence in acceleration and stored half-step velocity form. This avoids recovering acceleration by subtracting nearly equal positions and dividing by the squared time step. It also explicitly rejects nonfinite state, auxiliary, force and accumulated-work observations before maxima or JSON formatting can hide them. All 22 deliberately injected nonfinite observations are detected. The original 220 Hz / 192 kHz independent branch-force closure failure clears: relative peak mismatch is 1.4234058e-14 under the unchanged 1e-10 gate. The two regulated driven-release convergence gates still fail, exactly as retained in [report A](report-A.json).

[Amendment B](amendment-B.patch) changes just the candidate damping cell centered at 0.2 seconds to eta=50.5. The physical coefficient is eta=1 before that instant and eta=100 after it; each occupies half the centered cell. Physical damping remains proportional to the original physical mass, including its off-diagonal terms. This is an average of the coefficient, not exact integration of the coefficient times velocity. Independent RK4 still splits at the physical jump, and its event observation remains right-sided. Additional output separates this timing difference from SAV/nonlinear and numerical-inertia force gaps. The derivation, physical fixture, samples, controls and gates were frozen before the run.

The protocol files are byte-identical copies: [protocol A](protocol-A.md) is the original `protocol-A-v2.md` (revision 2 including controlled guard checks), and [protocol B](protocol-B.md) retains its original name. The reconstructed source comments retain their original protocol filenames. No source/header include paths were changed for packaging. Both corrections continue to include the **same** `frozen-prototype.cpp` retained by v1, itself a byte-identical frozen copy of the [original nonlinear string prototype](../NonlinearStringPrototype.cpp).

## B results

The regulated driven-release relative errors in [report B](report-B.json) are:

| Sample rate | Whole q | Whole v | Endpoint y | Endpoint vy | Port force |
| --- | ---: | ---: | ---: | ---: | ---: |
| 48 kHz | 0.0621533% | 0.339569% | 0.0435350% | 0.0497687% | 0.339263% |
| 96 kHz | 0.0154611% | 0.0847914% | 0.0108710% | 0.0124333% | 0.0843040% |
| 192 kHz | 0.00386328% | 0.0212134% | 0.00271649% | 0.00310751% | 0.0210648% |

Every error decreases by 3.997–4.024 times at each rate doubling, clearing the fixed minimum of three without invoking a reference-limited exception. All physical-reference measurements and all original non-driven measurement fields are exactly equal to A as JSON values. Maximum relative joint/string/body work residual over every stepped case is below 1.78e-13; maximum relative independent branch-force mismatch is 1.42341e-14.

The single-sample force distinction survives: at the same candidate state, centered-cell versus instantaneous right-sided damping changes the physical port effort by about **2.263e-5 N** at the event. The event remains in every common-grid error. It accounts for 0.0577%, 0.0553% and 0.0492% of total squared force error at 48/96/192 kHz. The existing whole-trajectory force and refinement gates pass; there was no separate pointwise equality gate. The mismatch is not zero, and further asymptotic refinement could expose it. `damping_transition_observation` and `damping_cell_vs_instantaneous_force_gap` retain the separate force values.

The unregulated diagnostic trajectories remain in both reports, with their larger errors and auxiliary drift. Zero failed B gates does not mean the unregulated method meets the regulated accuracy bounds; the frozen protocol applies those accuracy bounds to lambda=1000. V1 still has three failures and its documented finite-monitor coverage hole. A has two failures and closes that hole. B has zero failures under the same declared candidate gates.

## Reconstruct and run

From the repository root, choose a new temporary directory. The explicit patch target prevents patch filename inference from selecting the wrong file:

```sh
audit_dir=$(mktemp -d /tmp/acustra-port-reconstruction.XXXXXX)
cp Tools/MovingEndpointPowerPortAuditV1/frozen-prototype.cpp "$audit_dir/frozen-prototype.cpp"
cp Tools/MovingEndpointPowerPortAuditV1/MovingPortAudit.cpp "$audit_dir/MovingPortAudit-A.cpp"
cp Tools/MovingEndpointPowerPortAuditV1/protocol-v1.md "$audit_dir/protocol-v1.md"
cp Tools/MovingEndpointPowerPortCorrections/protocol-A.md "$audit_dir/protocol-A-v2.md"
cp Tools/MovingEndpointPowerPortCorrections/protocol-B.md "$audit_dir/protocol-B.md"
patch "$audit_dir/MovingPortAudit-A.cpp" < Tools/MovingEndpointPowerPortCorrections/amendment-A.patch
cp "$audit_dir/MovingPortAudit-A.cpp" "$audit_dir/MovingPortAudit-B.cpp"
patch "$audit_dir/MovingPortAudit-B.cpp" < Tools/MovingEndpointPowerPortCorrections/amendment-B.patch
shasum -a 256 "$audit_dir/MovingPortAudit-A.cpp" "$audit_dir/MovingPortAudit-B.cpp"
clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic "$audit_dir/MovingPortAudit-B.cpp" -o "$audit_dir/MovingPortAudit-B"
"$audit_dir/MovingPortAudit-B" "$audit_dir/result-B"
```

Expected reconstructed source SHA-256:

- A: `93c856d625f2135de1fc895ebe51953178b5414d7f057e8e17d4d2b3f57911ac`
- B: `725b51ad7fea7ecdc50a7b06d1b5275f3cc49d152abaf0fdb4e9218c230552e6`

[Provenance](provenance.json) records the frozen input, patch, protocol, reconstructed-source, original executable, output and package-file hashes, compiler/flags and preservation checks. Original executable hashes authenticate the retained runs; rebuilding at another path or with a different compiler need not produce an identical executable. The reconstructed source hashes must match. A can be compiled and run analogously; its expected exit code is 1 with two convergence failures. B's retained exit code is 0.

An independent reconstruction from these packaged patches on 2026-09-09
matches B's source hash and reproduces every value in all 204 report rows,
with zero failed gates. Its directory and report/log hashes are retained in
the provenance file.

## Scope

This audit contains twelve original nylon modes plus one unfitted compliant endpoint. It establishes the declared finite-model numerical checks only. It does not establish full-band spatial/temporal convergence, realistic guitar sound, production suitability, or an improvement in recording scores. It does not overturn the earlier coupled-acoustic rejection. No production engine change, full-band expansion, audio render or recording fit accompanied these runs.
