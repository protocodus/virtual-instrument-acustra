# Moving-endpoint power-port audit v1 — unqualified

Dated 2026-09-08. This standalone numerical experiment couples the original 12-mode nylon string to one unfitted compliant endpoint through a lifted displacement coordinate. **Its frozen audit has three failures and a remaining finite-state guard omission. It is not production or audio qualified.** No engine or recording-corpus change follows from it.

The six retained files preserve the root-approved [protocol](protocol-v1.md), byte-identical [native source](MovingPortAudit.cpp) and its [frozen input](frozen-prototype.cpp), all **182 measurements** in the original [report](report.json), and [provenance and independent checks](provenance.json). The canonical earlier experiment remains [NonlinearStringPrototype.cpp](../NonlinearStringPrototype.cpp). The duplicate frozen source exists only to reconstruct this exact standalone executable; neither file is linked into the plugin.

The fixture is explicitly synthetic: endpoint mass .05 kg, resonance 220 Hz and Q=30. With x=0 at the moving endpoint, displacement is `u=sum q_n sin(n*pi*x/L)+(1-x/L)y`. The full point-load vector at x=.173L includes both modal forces and endpoint component .827. Physical damping is `2 eta Ms_phys`, with its cross terms retained. Numerical mass correction adds positive-semidefinite inertia in the absolute sine projections `q_n+d_n y`. String/body forces are independently reconstructed from their equations and their discrete port work must cancel. Point stress is an observer, not an injected force.

## Retained failures

| Fixed gate | Case | Observed result |
|---|---|---|
| Error must reduce more than 3× per rate doubling | Regulated nonlinear driven release, 48→96 kHz | Endpoint displacement reduces 2.67146× and velocity 2.71417×. |
| Same convergence gate | Same case, 96→192 kHz | Whole displacement reduces 2.84928×, endpoint displacement 2.30987× and endpoint velocity 2.33975×. |
| Independent branch-force peak error / peak force <1e-10 | 220 Hz linear drive, 192 kHz | `1.63431e-12 N / .00578889 N = 2.82319e-10`. |

The driven-release physical weak-force errors are 0.338399%, 0.0841767% and 0.0213021% at 48/96/192 kHz. Its endpoint displacement errors are 0.0669497%, 0.0250611% and 0.0108496%. These satisfy the 48 kHz absolute limits while failing the stricter prescribed convergence reductions. Every free linear/nonlinear trajectory has approximately fourfold reductions. No rate, fixture, amplitude or threshold was changed after observing these results.

The third failure is not a zero-denominator artefact: force peak is .00578889 N. For this same case, joint/string/body work balances are `1.78e-12`, `2.61e-12` and `4.29e-14` relative. Its piconewton branch discrepancy is consistent with cancellation in the second-position acceleration `(next-2*z+previous)/h²`, amplified at the high rate and near a resonant force cancellation. The original threshold still fails; a proposed algebraically equivalent acceleration-state implementation belongs to a separate run, not a retrospective pass for v1.

The abrupt physical damping change from eta=1 to 100 s^-1 at .2 s is a plausible cause of the failed state orders. A right-side coefficient used for the whole centred time cell creates a leading jump-timing defect; all free cases lack this discontinuity. This is an inference, not a demonstrated attribution from v1. The unregulated driven control reduces its substantially larger errors about fourfold, so it does not independently prove the cause. A later cell-average treatment must be derived and tested separately with unchanged physical coefficients and gates.

## Coverage and independent evidence

Physical RK4 reference gates pass before candidate qualification. Across the trajectory references, worst 384/768 kHz relative displacement difference is `3.535e-7`, and worst weak-port force difference is `2.476e-6`. The numerical audit never substitutes corrected mass into the physical reference.

The independent NumPy algebra oracle reproduces numerical mass entries within `5.4e-20`, all complex transfer fields within `7.4e-15` relative, and coupled pole biases within `4.7e-12` cents. It verifies the full point-load vector, physical cross damping, PSD mass additions, separate string and joint coercivity, and fixed-end identities. The naive q-only mass correction has the predicted top-mode force bias of −0.801468% at 48 kHz while conserving its own numerical ledger. Energy conservation alone therefore does not establish the correct boundary force.

The auxiliary `LedgerMonitor::add` used in zero/symmetry/static/sinusoidal loops lacks an explicit rejection of nonfinite state and energy values. `std::max` can preserve an older value when its other argument is NaN. Saved measurement outputs are finite (the seven nulls are intentional no-reference fields), but this does not establish complete finite-state coverage for every intermediate auxiliary trajectory. The main trajectory path rejects nonfinite energy plus psi. This omission is disclosed and retained in v1; future explicit guards are an audit repair, not evidence that v1 passed.

Complete failed fields, thresholds, native/compiler hashes and the independent cross-check report are retained in `provenance.json`. The original native result is exit 1 with exactly three failures. No false expectation was added to make the unregulated control fail a release gate.

## Force interpretation and remaining scope

The physical Galerkin endpoint reaction and instantaneous point stress are distinct in a finite nonlinear basis. This audit separately reports solved-port error against physical RK4, same-state SAV-force error, same-state numerical-inertia error, and physical weak-versus-point closure gaps. During driving, the projected forcing remainder also belongs in the weak reaction. For a fixed-end linear 1 N point load, the independent oracle obtains .827 N weak reaction and .741156 N point-stress force; the required load-projection remainder is .0858436 N.

Neither a small within-model weak/point gap nor accurate time integration establishes that 12 modes resolve guitar force. Root's separately frozen spatial audit at `/tmp/acustra-nonlinear-spatial-audit-20260908` finds the initial 4 mm Gaussian 12-mode point force 9.915% below its 192-mode spatial reference, despite a much smaller weak/point difference within the 12-mode model. That separate result does not alter this protocol's fixed 13-coordinate scope.

The [2026-08-30 coupled SAV rejection](../../Docs/decisions.md#2026-08-30--local-slope-sav-stringbridge-candidate-rejected-by-public-nonlinear-oracle) remains relevant: an earlier coupled solver already conserved a numerical energy and reproduced poles while failing acoustic profile, pitch and finite-window DC gates. This experiment has no recording score, full-band string qualification, production callback benchmark or acoustic-superiority claim.

## Reproduction

From this directory, compile the two retained source files as one standalone translation unit:

```sh
clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic MovingPortAudit.cpp -o /tmp/acustra-moving-port-v1
/tmp/acustra-moving-port-v1 /tmp/acustra-moving-port-v1-new-output
```

The output directory must not exist. Expected status is exit 1, `Moving-port audit failures: 3`. Original raw logs, executable and run manifest remain at `/tmp/acustra-moving-port-audit-20260908`. Later corrections must preserve these original results and receive their own protocol, source hashes and output directory.

An independent rebuild from these retained repository files on 2026-09-09
reproduced all 182 measurement rows and three failures exactly as parsed JSON.
Its result and log hashes are recorded in `provenance.json`. Reproduction of
the failed audit does not qualify this original implementation.
