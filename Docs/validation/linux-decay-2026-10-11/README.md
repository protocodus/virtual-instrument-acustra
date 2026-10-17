# Decay-test diagnostic evidence

See [the assessment](../linux-decay-test-2026-10-12.md) for the failure, repair
and limits of the conclusion. These are compact read-only probe results from
`build-audit/linux-decay-ci-20261011`; raw float render buffers and binaries
remain outside Git.

- `linux/baseline.log`: exact reproduction of the Linux CI failure, including
  original frequency/amplitude/rate diagnostics at all nine notes.
- `linux/early.log`: candidate 0.20–0.40 / 0.60–0.80 s decay measurements.
  **Its `DIAG hz` and `amp` fields still describe the original windows** because
  the diagnostic print retained its original calls; only `decay` uses the new
  windows. Do not derive the candidate rate from those printed amplitudes.
- `linux/window.log`: MIDI 84 frequency, amplitude and raw-force observations
  across shorter windows and the original window pair.
- `windows-ci-failure-excerpt.log`: relevant original Windows CI failure lines;
  the complete downloaded job log remains in the scratch evidence directory.
- `native/measurements.jsonl`: corresponding macOS arm64 MIDI 84 observations.
  Amplitudes are normalized by the Hann-window sample length; `relative_db`
  is relative to the first window for that rate and observer.
- `native/singleplane.jsonl`: the same diagnostic after clearing the parallel
  polarization, with the production normal-plane loop unchanged.
- `native/early_tests.log`: candidate-window pass over MIDI 76–84, H1/H8 and
  44.1/48/96 kHz. This scratch probe used earlier windows for both partials;
  the production repair deliberately retains the original H1 windows.
- `native/synthetic.jsonl`: independently specified exponential decay rates
  in float mixtures; it contains a wider diagnostic matrix than the two
  representative cases retained in the production regression test.
- Native `.cpp` files: small driver sources for the retained diagnostics.
  They included the current test source to reuse its exact estimators. The
  synthetic driver included a scratch copy named `early_tests.cpp`, made by
  changing only the original estimator's windows to 0.20–0.40 / 0.60–0.80 s
  and replacing `main` with the fractional-delay test. Use the recorded
  pre-repair source revision when reproducing those original observations.

The final repaired full-engine suite logs, source/build hashes and commands are
recorded in `final-local-validation.json`; `evidence-index.json` hashes all
retained evidence and the assessment. Linux emulation is functional validation
only, not a CPU measurement.
