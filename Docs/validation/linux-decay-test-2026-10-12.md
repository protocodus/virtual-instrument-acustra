# Cross-platform upper-partial decay test — 2026-10-12

The public [CI run 38177217157](https://github.com/protocodus/virtual-instrument-acustra/actions/runs/38177217157)
tested `da098abb50f98babd44ee6ec20a728561420d510`. Its [Linux job](https://github.com/protocodus/virtual-instrument-acustra/actions/runs/38177217157/job/114582032463) passed 86 of 87
tests and failed `Acustra.Engine`: isolated steel MIDI 84 H8 had **6.257539%**
decay-rate spread across 44.1, 48 and 96 kHz, exceeding the existing 6% bound.
The [Windows job](https://github.com/protocodus/virtual-instrument-acustra/actions/runs/38177217157/job/114582032283)
passed 85 of 86 tests and failed the same test at exactly the same 6.257539%
spread. The macOS CI job succeeded.
The same observation on the local macOS arm64 build gave 4.836411%.
An Ubuntu 24.04 amd64 / GCC 13.3 reproduction recovered the CI result exactly.

The repair changes only [the test's observation windows and estimator checks](../../Tests/AcustraEngineTests.cpp).
H8 now uses 0.20–0.40 s and 0.60–0.80 s instead of 0.20–0.60 s and 1.00–1.40 s.
Each pair consists of equal-length Hann windows; the estimator tracks the
partial's frequency separately in each window and divides the level difference
by the separation of their centers. H1 retains its original windows. The **6%
H8 bound, 0.1% H1 bound, positive decay checks and 2.5:1 round-trip loss bound
are unchanged**.

## Evidence and limits

| Measurement | macOS arm64 | Linux amd64 / GCC 13.3 |
| --- | ---: | ---: |
| Original H8 maximum spread, MIDI 76–84 | 4.836411% | 6.257539% |
| Earlier H8 maximum spread, same notes and rates | 4.76537% | 4.967414% |

For the failing MIDI 84 case, the original three decay rates in dB/s were
55.738697, 56.582871, 53.910905 on macOS and 56.235015, 56.627239,
53.191272 on Linux, in ascending sample-rate order. The earlier Linux window
pair gives 56.043784, 56.928874, 54.169515 dB/s.

The macOS original late-window partial is approximately −128, −122 and −121
dBFS at the three rates, 84–91 dB below the fundamental. Its peak remains more
than 60 dB above the neighboring spectral floor. Consequently this evidence
does **not** establish a broadband noise-floor or peak-search failure. It shows
platform-sensitive decay in a very weak rendered late partial; the precise
mechanism has not been isolated. The earlier interval measures the sustained
decay before that regime. This is a bounded measurement correction, not a
claim that the complete instrument is sample-rate independent.

Reading raw bridge reaction force instead of the Piezo output changes the
macOS original measured rates by less than 0.024 dB/s and yields effectively
the same rate spread. Piezo processing does not explain the discrepancy.
Removing the parallel polarization changes the macOS original spread to
4.197458%, while its earlier-window spread remains of similar size; that
experiment does not establish polarization beating as the cause.

## Independent estimator check

The new test synthesizes a known exponentially decaying 8430 Hz partial beside
a much louder 1046.5 Hz fundamental, sums them into float samples, and checks
the measured slope against its independently specified value. It covers all
three sample rates and both steady pitch and a −10 Hz/s upper-partial glide.
The fundamental starts at amplitude 0.1 and decays at 10 dB/s.

| Upper partial | Initial amplitude | Earlier-window maximum error | Test tolerance |
| --- | ---: | ---: | ---: |
| 55 dB/s decay | 0.003 | 0.0000132 dB/s | 0.001 dB/s |
| 80 dB/s decay | 0.0001 | 0.002981 dB/s | 0.01 dB/s |

These errors are from the native scratch probe. The original late-window pair
also measures the representative 55 dB/s synthetic case accurately, confirming
that low amplitude alone does not explain the rendered-string discrepancy.
Its worst error in the deliberately harder 80 dB/s case is 0.036318 dB/s,
which the new 0.01 dB/s guard rejects. The existing slow-decay pitch-glide
estimator test remains in place.

## Provenance

Compact probe results are retained in [linux-decay-2026-10-11](linux-decay-2026-10-11/README.md).
Native probes reused the already validated `build-audit/libAcustraDSP.a` and
current production headers; Linux probes reused one GCC-built engine object
across baseline and candidate measurements. The rendered engine is unchanged
between the two test versions. The probe matrix does not constitute another
full instrument audit or a listening judgment.

No production DSP, parameters, models, native plugin implementation or Rack
source changes are required. Rack 1.2.8f1 therefore does not require rebuilding
for this test-only correction. Final repaired suite results and source hashes
are recorded separately with this evidence.

## Final local validation

The complete repaired `Acustra.Engine` suite passes on macOS arm64 (72.79 s)
and Ubuntu 24.04 amd64 with GCC 13.3. The latter reuses the exact engine object
that reproduced the original CI failure. The new synthetic cases pass in both
complete suites; existing bounds remain unchanged. See the
[validation receipt](linux-decay-2026-10-11/final-local-validation.json).
The configured hosted CI will run on the committed repair before public main
is advanced; local Linux emulation is not a performance qualification.
