# Axial source-rate correction — 2026-11-08

Local 1.9.30 review candidate; no listening verdict or publication.

The temporal source clock now runs at the host rate. The existing spatial
cell spacing, retardation, inferred excursion, ring tuning/loss and output
band remain. The source increment is F[n] - F[n-1], replacing the difference
between strided samples divided across a hold. This removes folds at the
former fs/stride clock; it does not remove host-Nyquist quadratic aliasing.

## Independent frequency oracle

`StringAxialForceTests::sourceDoesNotFoldAtSpatialStride` writes a pure 6 kHz
wave. Squared slope has only DC and 12 kHz, regardless of quadrature gain or
phase. The source is inspected before the ring/output band. The previous
source produces the unintended line at 10.05 kHz at a 44.1 kHz host and
4 kHz at 48/96/192 kHz: respectively +1.21, +8.73, +9.34 and +9.49 dB over
its 12 kHz component. The candidate's unintended line is below -75 dB
(the actual deterministic measurement is at numerical floor). This same
test fails against the frozen pre-correction library.

## Finite high-rate reference

Jumbo Steel Pick, velocity 1, top E frets 0/5/12/16 and low E frets 0/12;
fresh deterministic engine, no room/piezo mix, 0.5 s per note. Render the
full source at 44.1/48/96/192 kHz. To isolate folding in one common source,
divide the 192 kHz lastStretch by rate, then compare raw 4:1 decimation
against `scipy.signal.resample_poly` (Kaiser beta 12, line padding). Drive
identical 48 kHz source-difference, longitudinal-ring and output-band
coefficients with each. Measure 50-450 ms to exclude boundary padding.

Predicted folded-source RMS relative to the bandlimited axial reference:
top E frets 0/5/12/16: -37.2/-23.9/-34.2/-41.3 dB;
low E frets 0/12: -57.1/-63.7 dB. These are relative to the axial force,
not the complete note. The 192 kHz reference is finite-band and uses its
own spatial/string discretization, so these figures are neither a proof
of alias-free sound nor an exact estimate of native 48 kHz alias.

Separately, native 48 kHz axial-force band energies relative to 192 kHz
are within 0.23 dB at 100-2500 Hz and 2.54 dB at 2500-10000 Hz over these
six notes. Above 10 kHz the output-band mapping and quadrature differ
more. These native-rate residuals contain physical-state, quadrature and
filter differences and must not be labelled alias measurements.

## Verification and review evidence

Release GCC build; existing StringAxialForce, PhysicalHandRealism and full
Engine contracts pass; C++17 DSP build passes. The unit conversion and source
rate are separate local changes with separate A/B sets. Evidence, frozen
renderers, raw float traces, coefficient JSON and analysis script are under
the task's external `review/acustra/` folder. A/B uses the production
Performer, equal events/seed/parameters/rate, with whole-file RMS matching
and native float renders retained. Performance and broad-suite results are
recorded in that external evidence; no DAW performance or listener preference
is inferred here.
