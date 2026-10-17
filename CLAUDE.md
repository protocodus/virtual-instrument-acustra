# Acustra development guide

Use this as the starting workflow; [README.md](README.md) describes the product
and [Docs/decisions.md](Docs/decisions.md) records accepted choices and rejected
experiments. Read the relevant recent entries before repeating an experiment.
Older reports include retired models and controls; check their date and scope.
The 2026-10-09 cleanup also removed archived experimental implementations.
Their dated reports and measurements remain; source paths in those records
refer to the historical Git revision rather than the current tool inventory.

## Find the right layer

- `Source/DSP/AcustraEngine.*`: six steel strings, excitation, retained tails,
  shared bridge/body, capture, piezo and room. The production instrument has
  no recorded-note player; `Source/DSP/SampleBank/` supplies offline references.
- `Source/DSP/AcustraPerformer.*`: front-end-neutral MIDI, ownership, strums,
  Gather, MPE and sample-accurate scheduling. Front ends play through Performer.
- `Source/PluginProcessor.*`: JUCE parameters, state migration, host tempo,
  latency and buses. Preserve saved parameter IDs and automation behavior.
- `Source/PluginEditor.*` and `Assets/UI/`: interface and display behavior.
- `Source/DSP/*Data.h`: measured, fitted or generated model inputs. Establish
  provenance and use the relevant generator/checker when changing a table.
- `Tests/`: signal, independent-math, state, scheduling and tool contracts.
  `Tools/`: offline rendering, fitting, scoring and listening-pack generation.
- The Reason Rack adapter consumes this canonical DSP. Keep Reason-specific
  mappings in its wrapper; update its submodule/pin only within requested scope.
  It mounts this repository as the pinned submodule `Examples/Acustra/Instrument`
  in the private `protocodus/reason-rack-extensions` and compiles
  `Source/DSP/*.cpp` directly; DSP changes land here first, then the pin advances.
- The DSP version is the product version: `project(Acustra VERSION X.Y.Z)`.
  Any DSP or behaviour change bumps it, and the Rack device's `version_number`
  takes the same `X.Y.Z` plus its own stage suffix. Versions only go up.

Keep DSP/Performer JUCE-free and C++17-compatible for the Rack toolchain, even
though the main project uses C++20. Avoid audio-thread allocation, file I/O,
blocking locks and exceptions. Large engine/test fixtures belong on the heap.

## Documentation layout

Repository layout, naming, CI, versions and the `Docs/` taxonomy follow the
Protocodus portfolio standard kept in the project, not in this repository.
`README.md` opens with the title and intro, the editor screenshot,
**Audio demos**, a concise **Version history** and **Downloads** (the block
between the `build-download` markers is rewritten by `main-build.yml`); after
a `---` come **How it works**, **Known gaps**, **Build** and **Licensing**.
Nothing sits loose in `Docs/` except `decisions.md`:

| Folder | Holds |
| --- | --- |
| `audio/` | rendered demos and committed listening tests |
| `screenshots/` | editor images |
| `guide/` | user guide and parameter meaning |
| `research/` | modelling research, experiments and ablations (md + same-stem json) |
| `validation/` | evidence that a version or change works |
| `benchmarks/` | CPU and performance measurements |
| `plans/` | improvement plans and open work |
| `release/` | release notes, the detailed release history, readiness |

Name documents in lowercase kebab-case, `<topic>-YYYY-MM-DD.md` for dated
work; an evidence folder takes its report's stem. Moving a document is a
rename only: keep its content and date, and update every link in the same
commit. Long per-version notes go to `Docs/release/release-history.md`, not
the README.

## Build once, verify the affected behavior first

Run from the repository root with CMake 3.22+ and a C++20 compiler. Prefer a
JUCE-free Release build for engine work; reuse its configured directory.

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON -DBUILD_TESTING=ON
cmake --build build-dsp --config Release --parallel 2 --target AcustraEngineTests AcustraStringPitchRealismTests
ctest --test-dir build-dsp -C Release -R "^Acustra[.](Engine|StringPitchRealism)$" --output-on-failure
```

Every CTest test carries one tier label: `unit`, or `audio` for the demo
render checks; CI selects tiers with `ctest -L`. `-DACUSTRA_ENABLE_SANITIZERS=ON`
(Clang or GCC) builds everything under ASan + UBSan, as the `sanitizers` job does.
Select targets and an anchored test expression for the affected behavior.
Choose parallelism for available memory; `ctest -N` lists registered tests.
On non-MSVC builds, also build `AcustraDSPCxx17` for DSP/Performer changes.

Useful CTest suites (executable targets are declared in `CMakeLists.txt`):

| Change | Relevant CTest suites |
| --- | --- |
| Attack, velocity, geometry | `Engine`, `VelocityRealism`, `StringPitchRealism` |
| Release, joining, retained waves | `Release`, `ReleaseJoin`, `RepeatedPluckRealism` |
| MIDI, strums, block boundaries | `Performer`, `HandAllocator`, `StrumDirection`, `StrummingRealism` |
| Bridge/body or construction | `BodyShape`, `GuitarModels`, `ConstructionMatrix`, `StringDecayRealism` |
| Capture and routing | `Capture`, `OutputBuses`, `PiezoCircuit` |

All names above have the `Acustra.` prefix. Build the selected test executables
before invoking CTest. Select coverage for the actual change; documentation-only
edits need link/command review, not a DSP rebuild or full audio suite.
For core engine, cache or state-layout changes, include `Engine` early, before
freezing large rendering or measurement runs.

Python tool checks need Python 3; many also need NumPy and SciPy. Configure with
`-DPython3_EXECUTABLE=<interpreter>` and `-DACUSTRA_REQUIRE_PYTHON_TESTS=ON` when
validating the complete tool suite. Inspect configure warnings and registered
tests: a passing partial suite does not prove skipped Python tools work.

Once implementation and focused checks settle, build all targets and run the
full applicable suite for a substantive DSP change:

```sh
cmake --build build-dsp --config Release --parallel 2
ctest --test-dir build-dsp -C Release --output-on-failure
```

For JUCE/host integration, use a separate native build. It fetches pinned JUCE
unless `ACUSTRA_JUCE_PATH` points to a local checkout; platform prerequisites
and packaging are in [README: Build](README.md#build).

```sh
cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=ON -DBUILD_TESTING=ON
cmake --build build-plugin --config Release --parallel 2 --target AcustraPluginProcessorTests AcustraPluginTempoReleaseTests
ctest --test-dir build-plugin -C Release -R "^Acustra[.](PluginProcessor|PluginTempoRelease)$" --output-on-failure
```

Build the actual plug-in formats for changes affecting their deliverables;
wrapper tests alone do not establish a VST3/AU/CLAP/Standalone build. After a late
fix, rerun affected checks and refresh final validation against the new source.
Coordinate one final broad run rather than duplicating full runs across agents.

## Preserve state and scheduling contracts

- Cached and forced configurations must produce identical sample bits with
  the same compiler/settings. Cache exact inputs, not quantized controls;
  coefficient reuse must still advance every filter and history sample.
- An unchanged transition target preserves applied value, step bits and
  remaining count. A distinct discrete retune starts from the current state;
  continuous target revisions preserve the finite deadline. Keep transitions
  inside their physical endpoints and assign the exact target at completion.
- Account for reset, clear, copy/capture, tail restoration and every direct
  writer when adding state. Feedback compensation must not amplify independent
  contact/noise arrivals; retain their bounded loss semantics.
- Scheduled attacks use controls at actual contact. Re-plucks preserve old
  waves/history while refreshing attack-dependent geometry and reference keys.
  Conventional/manager slides and MPE member tension bends have distinct roles.
- Check same-sample event ordering, control-period boundaries, block partition
  invariance, sustain/panic and rates relevant to the change. Ordinary key-up
  includes the global 1/32-note join window; see [release rules](Docs/research/release-join-2026-10-05.md).
- Keep meaningful signal guards and independent math. Diagnose a failed test
  against the baseline before changing a fixture; document stale assumptions
  and demonstrate the correction without loosening a valid numeric tolerance.

## Make audio evidence reproducible

Freeze the actual working source, renderer and scorer, controls, event scores,
reference hashes and corpus selection before comparing variants. Record build
type/compiler/flags and any nondefault calibration. A Git commit alone omits
dirty changes. Off-tree probes need headers and libraries from the same snapshot.

Use matched real/before/after audio with the same baseline/candidate trim;
document reference level matching and retain raw floats and native levels. Keep
variant labels (A–Z where used) consistent across audio, manifests and decisions.
Report listening judgments only when someone actually listened. A descriptor
loss, audible difference or regression pass does not establish listener preference.

Match technique, capture and instrument labels; disclose unknown velocity,
string assignment, cropping and onset assumptions. Avoid treating overlapping
corpora as independent validation or development data as untouched held-out data.
Inspect individual pitches, registers, score terms and transfer tradeoffs, not
just a pooled improvement. Do not quietly retune nuisance controls per reference.
After source changes, re-render or prove relevant same-toolchain byte parity
before reusing measurements. Cross-platform waveform hashes need not match.
See [recording evidence](Docs/research/recording-realism-2026-10-07.md) and
[gesture evidence](Docs/research/audible-realism-2026-10-07.md) for detailed protocols.

## Measure CPU after the source is frozen

Use matched baseline/candidate toolchains, flags, controls and event streams.
Warm up, alternate paired callback order, and exclude setup, rendering exports,
hashing and reporting from timed regions. Measure attacks/queued strums and
transitions as well as held notes; include relevant force, Touch and rate edges.
Verify each edge scenario reaches its intended DSP state: maximum MIDI velocity
can be altered by scheduling or performance variation before actual contact.
Report absolute callback time, medians and tail timings with machine/workload
details. A local speedup is not a guarantee on another processor or host.

Run final timings serially with builds, tests and other renders idle; coordinate
this quiet period with collaborators. Any further source edit invalidates the
freeze and requires refreshed binaries/provenance. Use the existing paired
protocol in [CPU optimization](Docs/benchmarks/cpu-optimization-2026-10-05.md), preserving
audio/state checks for optimizations intended to leave sound unchanged.

## Work safely in a shared tree

Inspect `git status` and the relevant diff before editing. Preserve unrelated
user/collaborator work and existing evidence; coordinate file ownership.
Use separate build/output directories or isolated copies/worktrees for variants.
A clean worktree at HEAD does not include dirty baseline changes: capture them
explicitly when they are part of the reference. Keep old reports and audition
packs intact, and name new outputs so provenance remains clear.
Complete authorized reversible work and make its result reviewable without
adding permission gates. Report what changed, what was verified, audible/CPU
tradeoffs and any unavailable host or SDK coverage plainly.
