# Age transition precision — 2026-10-13

Before this fix, the 90-suite check of the chorus repair and retained CPU iteration 08
failed only `Acustra.RepeatedPluckRealism`. The failing fixture changes String
Age from 0.150 to 0.151, 64 frames after a same-note G2 reattack at 48 kHz.
Its unchanged first-256-frame slope-error limit is `2.7e-4 * steadyPeak`.
The 16-frame variant passed; the 64-frame variant reached 0.000279671 against
the 0.000270000 limit. This was a sound-transition failure, unrelated to the
CPU optimization: the frozen sound-only source produces identical results.

## Two rounding defects

`StringLoop::advance` advanced a requested feedback-gain transition with
`loopGain += loopGainStep`. A typical 462-frame Age transition requests a
step near `6.2e-9` at a gain near 0.9922. This is below half a float ULP, so
the applied gain stayed completely unchanged until the exact endpoint jump.
The fix reconstructs applied progress from `target - step * remaining`, as
the intrinsic-filter transition already does, retaining float gain state,
monotone bounds and the same finite deadline. Same-target configuration still
leaves current value, step bits and counter unchanged. A revised target keeps
the remaining deadline. This is independently covered by rising and falling
sub-ULP moves, midpoint progress, reversal, copy and reset tests.

The speaking-delay follower had a second defect. `settlingGlide` snapped to
the target when a float increment rounded to zero, even if thousandths of a
sample remained. In the traced steady branch, a normal control update at
frame 191 snapped the played G2 from 461.207031 to 461.204224 samples: 92
float ULPs in one frame. Its Thiran coefficient moved from about 0.718584 to
0.722047. The changed branch continued moving by an ordinary ULP instead;
the difference emerged through the seven-frame microphone pipeline and
capture response as a narrow alternating slope residual near frame 202.
All twelve integer delay anchors stayed unchanged. Another idle string
snapped 144 ULPs near frame 199. This was premature follower completion,
rather than an intended change in a delay tap or physical model.

The delay fix carries exponential progress in a double, then publishes the
existing float `currentDelay` and uses the existing float line read and
Thiran coefficients. It keeps the established six-millisecond exponential
speed and `1e-4`-sample exact-target completion threshold. It does not add a
new phase ramp or alter the settled speaking length. A published-bit marker
detects explicit prescription and dispersion-switch writes; reset seeds the
progress from the prescribed delay, and full-loop copies retain in-flight
progress. All hard prescriptions explicitly seed the double even when the
prescribed float has the same bits as the last published value. This matters
at long supported delays, where a float ULP exceeds the final `1e-4` threshold.
Nominal tuning/cache keys and the source model are unchanged.
The signal, coefficient, observation and delay-history formats remain float.

## Signal evidence and retained limits

The original Age guard and its 99–101% paired slope-energy guard remain
unchanged. Focused Age-only diagnostic results were:

| Source | Reattack→Age delay | Maximum first-256 slope difference | Difference / steady peak |
| --- | ---: | ---: | ---: |
| Before fix, retained CPU 08 | 16 frames | 5.708e-6 | 0.000235116 |
| Before fix, retained CPU 08 | 64 frames | 6.789e-6 | 0.000279671 |
| Gain progress alone | 16 frames | about 6.77e-6 | 0.000278796 |
| Gain progress alone | 64 frames | about 6.81e-6 | 0.000280603 |
| Gain + precise delay progress | 16 frames | 3.092e-6 | 0.000127413 |
| Gain + precise delay progress | 64 frames | 2.924e-6 | 0.000120486 |

The gain fix addresses a real endpoint defect but does not alone cure the
observed first-256-frame failure. Removing the premature delay snap brings
both existing fixtures below their original limit with ample margin.

An additional 2048-frame diagnostic covers the entire 462-frame intrinsic
transition and continuing delay follow. Its maxima are 17.39e-6 at frame
392 and 17.35e-6 at frame 344; the gain-only maxima were 23.13e-6 at frame
392 and 17.81e-6 at frame 408. The original early artifact was reduced rather
than merely shifted beyond its window. Paired energy and exact intrinsic/
gain deadline checks still pass. The original first-256-frame threshold is
not presented as a 2048-frame contract: the longer subtraction also counts
the intended developing timbre difference. Independently rendered pre-polish
`8ea51f9` already has a 12.97e-6 maximum over 1024 frames despite passing
the original 256-frame guard. These longer results do not prove every possible
automation trajectory free of numerical ripple.

The historical first-256 controls support keeping the existing guard:
`8ea51f9` has normalized residuals about 0.000081/0.000076 and `1d1f432`
about 0.000121/0.000094. Disabling intrinsic retune continuity rejects both
historical versions and the before-fix source at the unchanged threshold;
the current negative-control ratios are about 0.000585/0.000578. No
normalization change or threshold relaxation was needed.

## Regression and provenance

`RetuneContinuityTests` compares 92-ULP rising and falling moves with an
independent closed-form double exponential at 44.1, 48 and 96 kHz. It checks
exact settled targets, target reversal, full-loop copy, reset, explicit
prescriptions and both second-dispersion handovers. A same-bit hard prescription
at 4096 samples clears a hidden 0.0002-sample residual before a subsequent
retarget. The earlier 92-ULP regression compiled against the frozen before-fix
source rejects its premature snap. The separate
gain-progress negative control rejects the old stalled gain implementation.

The evidence directory is
`/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/age-transition-investigation`.
It retains the original failed full-suite log and source under `before-fix`,
the independently matched historical controls and all twelve-loop traces in
`observer-audit/receipt.json`, the direct diagnostic sources, and rejected
shelf/phase/ease experiments. Those experiments are absent from canonical DSP.
The accidentally mismatched-header extended probe is retained as
`age-double-delay-progress-extended-ABI-invalid.log` and is not admitted as
evidence; the corrected probe uses the matching source/header snapshot.

The JUCE-free Release build and C++17 compatibility target pass. All eight
first-stage sound checks, including Engine, pass in 207.53 seconds. After the
same-bit hard-prescription synchronization helper, all seven affected focused
checks pass again in 61.17 seconds: ChorusRegression, all 15 ModalTuning cases,
VelocityRealism, StringPitchRealism, RepeatedPluckRealism, RetuneContinuity and
PitchGeometryCache. The final all-target build, including C++17, and **all 90
canonical CTests pass in 667.67 seconds** on the final source below. Python
tool tests were required and present. The [build log](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/final-canonical/build.log)
and [full CTest output](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/final-canonical/ctest.log)
retain this final broad validation. Package and host checks remain pending.

The [final source/check receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/age-transition-investigation/final/receipt.json)
retains all eighteen DSP files, the new contract test and complete focused
output. Final Engine.cpp SHA-256 is
`1d086c4052e9509529adc4e526409778d81a28e8067b049b6f3fefff54e45a27`;
Engine.h is `9c1b15f2e18882cd7d40b9f0f19c92f0a9b826cfbb011dd0f152d03c62229140`.
Actual SDK CPU and parity evidence was refreshed against this corrected sound
source in iteration 09: the declared fourteen-cell CPU aggregate improves
7.2642%, and all nineteen audio/write-flag quality comparisons are bit-identical.
The [matched source receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/source-receipt.json),
[CPU decision](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/timing/decision.json) and
[quality receipt](/Users/vojta/Dev/reason-rack-extension/main/Examples/Acustra/Output/validation/polish-1.3.1f1/cpu-iteration-09/quality/receipt.json)
retain the result and its workload/deadline limits. Iteration 08's optimization
measurements remain historical evidence for their earlier frozen checkpoint.
See [CPU optimization report](cpu-optimization-2026-10-13.md)
and [chorus repair report](chorus-repair-2026-10-12.md).
