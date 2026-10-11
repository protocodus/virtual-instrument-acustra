# Production observer isolation

The final production result is in
[`normalized-phase2/summary.json`](normalized-phase2/summary.json): library
`282ae53ffe4a0f977bb9c11ec4c06858303dd118aa00a03bc8315e021da7ca00`,
including independent flat Stereo/Mono level compensation. It passes every
isolation check below. The top-level `after.json` and `summary.json` retain
the earlier raw-blend phase 1; they are not the final library. Phase 2 reused
the preserved baseline raw results and rendered only the final candidate.

This receipt compares the **actual pre-change and post-change production
libraries**, using the same diagnostic translation unit and each library's
matching headers. `ProductionObserverTrace.cpp` never calls the observation
injector and rejects an observer-file environment variable. This supplements
the earlier scratch-injector audit; it is not merely another injection test.

All 36 cases (four Shapes × three Woods × 44.1/48/96 kHz) pass: 564,300
per-sample explicit bridge force/mobility/motion/work and Piezo voltage values,
with bridge/body modal-state hashes, are byte-identical. The independently
written Piezo bus audio and every prepared construction's cached radiation
pole bits also match. Microphone audio changes in every case. A further
512-sample witness compares every active/fading bridge and body modal state
as 1,965 explicitly serialized doubles per sample, also exactly equal.

The score uses Finger plucks, release, re-pluck and a six-string chord,
default Body/Width/Output controls and Room 0. It is a bounded sounding-state
fixture, not exhaustive playing-technique or controller coverage. String
delay buffers are not dumped. Extremely late idle-reset timing below the
engine's inaudible threshold is not asserted. The full trace schema is in
the preserved `StudioObserverHarness.h`; the production probe uses only its
read-only tracing/pole serialization methods.

`before.json` and `after.json` retain hashes for every raw result and all
executed binary/library/source inputs. `summary.json` is the compact result.
Large raw traces, audio and state dumps remain in the scratch directory
recorded there. `observer-blend-final-focused.log` records the permanent
regression test: 576 independent approved observations, complex sample-rate
mapping, and 768 calibration-corner pole checks. The earlier 480-observation
negative control and rate-law positive control are preserved and explicitly
superseded by the expanded final negative control.

`release-observation/summary.json` records why one release regression now
observes the travelling strings directly. The microphone-dependent damping
ratio changed with the observer, while the direct string decay agreed. The
original threshold remains intact and rejects a wrong-pitch hand-loss
mutation. This diagnosis and its focused Release pass precede the final flat
level compensation; they are not a substitute for the final full suite.

## Reproduce

Build the identical diagnostic twice, changing only the matching production
header root and library. Use the recorded frozen baseline and final libraries
or independently build those exact sources; never combine one version's
library with another version's private-layout header.

```sh
c++ -O3 -DNDEBUG -std=c++20 -arch arm64 -mmacosx-version-min=11.0 \
  -I /absolute/matching/Source -I /absolute/this/evidence/directory \
  /absolute/this/evidence/directory/ProductionObserverTrace.cpp \
  /absolute/matching/libAcustraDSP.a -o /absolute/ProbeBefore
```

Repeat as `ProbeAfter` with the final inputs. For each stage, run:

```sh
python3 /absolute/this/evidence/directory/verify_production_isolation.py \
  --stage before --binary /absolute/ProbeBefore \
  --library /absolute/before/libAcustraDSP.a \
  --headers /absolute/before/Source --output-root /absolute/new/scratch/results
```

Then run `--stage after` with `ProbeAfter` and the final library/headers,
keeping the same output root. Each stage refuses an existing stage directory.
The reusable runner adds explicit output-directory selection and reads the
adjacent helper. Its unchanged executed predecessor is preserved as
`verify_production_isolation.executed.py` for exact provenance.
