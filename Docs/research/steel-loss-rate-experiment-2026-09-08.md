# Steel residual-loss experiment — 8 September 2026

**Rejected; the released sound is unchanged.** Converting the extra steel
normal-plane attenuation from a constant per period to a constant rate per
second improves the dry training corpus but worsens the Walden training corpus.
Development and flat-top scores were not opened for this candidate.

The baseline is `dfa1002`. The isolated change was
`0.9995 -> pow(0.9995, midiFrequency(40)/frequency)` on the steel normal loop
only. Nylon, the horizontal loop, filter phase, all 29 calibration values,
bridge/body/capture and excitation remain fixed. This preserves the low-E
residual rate (about 0.358 dB/s) while removing its additional frequency slope.
It is an authored loss-law experiment, not an identified material coefficient.

| Training metric, lower is better | Baseline | Candidate |
| --- | ---: | ---: |
| Dry steel total | 6.091584 | 6.047128 |
| Dry steel decay | 4.095839 | 3.961981 |
| Walden total | 9.132877 | 9.191158 |
| Walden decay | 5.209248 | 5.334756 |

The dry benefit does not transfer to the second instrument. Register-specific
signed low-partial errors are insufficient to justify a change that also
lengthens other partials and lower notes. No development-selected replacement
was tried. The unchanged loop-gain clamp also limits the realized change.

The [full report](steel-loss-rate-experiment-2026-09-08.json) records the
predeclared protocol, source/binary hashes, all term scores and independent
baseline verification: **79 dry model renders and nine Walden training renders
are byte-identical** between current frozen executables and the retained
baseline corpus. The [unapplied patch](../Tools/SteelLossRateExperiment.patch)
retains the exact experiment against `dfa1002`. Frozen binaries, before/after
source, render/score scripts and raw audio remain locally under
`/tmp/acustra-loop-loss-20260908`; no raw recording is committed.

This trial motivates a construction-specific loss experiment, not a claim that
more sustain always improves realism. See the
[recording diagnosis](sound-quality-diagnosis-2026-09-08.md) and
[primary-source review](sound-quality-research-2026-09-08.md).
