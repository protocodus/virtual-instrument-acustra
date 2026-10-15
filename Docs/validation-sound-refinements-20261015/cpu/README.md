# Sound-refinement CPU evidence — 2026-10-15

The two retained sound changes add about **0.47%** to the median DSP callback
cost on this cloud machine. This is the median of 56 configuration-level
candidate/baseline median changes, ranging from −0.52% to +1.77%. Across the main
workloads, Room 0 changes by +0.05%, Room 0.5 by +0.58%, and Room 1 by +0.62%.
Positive percentages mean more callback time.

One quiet run contains **61,852 paired callbacks**. Median changes are +0.45%
for held chords, +0.43% for repeated attacks and +0.88% for explicit full-force
edge cases. These costs are small relative to the existing engine; the result
does not establish performance on another processor or a host deadline guarantee.

Representative 48 kHz / 128-frame callbacks, in microseconds:

| Workload | Room | Before median / p95 | After median / p95 |
| --- | ---: | ---: | ---: |
| Finger held chord | 0 | 133.722 / 159.921 | 133.962 / 159.230 |
| Finger repeated attacks | 1 | 155.895 / 217.949 | 156.576 / 223.006 |
| Pick held chord | 1 | 149.235 / 254.704 | 151.118 / 248.595 |
| Pick repeated attacks | 1 | 168.895 / 284.991 | 171.879 / 294.164 |

The median configuration-level p95 change is +0.38%. Individual tail results
vary: the largest p95 increase is +14.71%, and the worst observed callback is
8.766 ms before versus 7.166 ms after, in different configurations. These
cloud scheduling outliers are retained in the reports. No extra timing runs
were selected or discarded.

## Workloads and checks

Forty-eight main configurations cross Finger/Pick, held six-string chords or
repeated attacks, Room 0/0.5/1, 48/96 kHz and 64/128 frames. Repeated Finger
attacks use four fingers; Pick uses queued six-string strums. Main attacks
cycle MIDI velocities 50, 80, 105 and 127. Eight additional direct
string-per-channel cases cover Finger/Pick, 48/96 kHz and panel Touch 0/1 at
full force, Room 0.5 and 64 frames.

All 56 scenarios pass finite/nonzero Main L/R and Piezo output, observed
contacts, active strings and zero dropped events. The direct force cases
reach actual contact velocity 1. Effective Touch is measured: at full force it
is 0.553125 with panel Touch 0 and 1 with panel Touch 1. This avoids assuming
the panel setting is the actual contact value. All eight dry Finger output
hashes remain equal; the other 48 configurations change sound as intended.
Audio equality is not a general requirement for this sound-change benchmark.

## Timing and provenance

The machine exposes AMD EPYC 9V74 CPUs, with a four-CPU cgroup quota. The final
run is pinned to CPU 0. Both adapters use the same GCC 14.2 executable and
`-std=c++17 -O3 -DNDEBUG -fPIC -Wall -Wextra -Wpedantic -Werror`, with each
snapshot's own headers and a distinct namespace. No fast-math flag is used.

Every 1.5-second scenario has at least 250 ms of untimed preroll. Baseline-first
and candidate-first callback order alternates. Construction, preparation,
event authoring, observation, validation, hashing, result allocation, sorting
and report writing remain outside the timed region. Port observers and the
separate Piezo bus are enabled. Builds, tests, A/B rendering, browser checks
and archive creation had finished before the final run. The recorded run
lasted 18.10 seconds; exact UTC times and process snapshots are preserved.

- [Native per-configuration results](run-1.json) preserve timing quantiles,
  audio fingerprints and actual-contact observations.
- [Compact text table](run-1.summary.txt) and [structured summary](run-1.summary.json)
  retain every configuration's absolute times and changes.
- [Run manifest](run-1.run.json) records the exact command, affinity, times,
  process snapshots and output hashes.
- [Build manifest](build-manifest.json) records source, compiler, harness,
  object and executable hashes, flags and machine details.
- [Copy receipt](copy-receipt.json) proves the preserved evidence and tools are
  exact byte copies and that the canonical DSP still matches the measured
  candidate snapshot.
- [Standalone reproduction tool](../../../Tools/SoundRefinementCpu/README.md)
  includes the measured harness/builder, runner, summarizer and protocol.

The 1.70 MB raw callback CSV remains at
`/workspace/acustra-realism-20261015/cpu/run-1.csv`; its SHA256 and size are
preserved in the run manifest and copy receipt. Frozen source roots and the
measured executable remain under `/workspace/acustra-realism-20261015/` at the
paths recorded in those manifests. The repository retains bounded evidence,
not those full snapshots or raw callback rows. Preservation did not rerun or
modify the measured benchmark.
