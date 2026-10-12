# Targeted A/B reproduction

All executed work lives under the ignored directory
`build-audit/capture-implementation-20261011/ab`. The copies under `Docs` are
evidence; **do not run them in the documentation directory**. The scripts find
their working directory from `__file__`, then the repository as `parents[2]`.
An ignored replay directory must retain this depth, for example
`build-audit/capture-ab-replay/ab`.

| Stage | Executed command or requirement |
|---|---|
| Baseline | Git `d54efb2800a12130cd1027a8d64c1c0970e0ae9b`; original renderer SHA `02aad6afbf2ef5e533aec06ca522c5ac530068f75f64f468542e944fd06ce6be`; library SHA `9ac3e0b2d92ad626e31951e04b383bb6af764660ef6c7b4919647920344d7278`. Headers and original renderer source were read from that exact Git revision. |
| After | Root-declared frozen production source and rebuilt library SHA `ff258ecf3b80ac69fedc96d1b3f3ceed7413fd051e3500b2ae641b089321ba60`; matching headers, library and source retained in ignored `after/`. |
| Build CLI | `/usr/bin/clang++ -std=c++20 -O3 -DNDEBUG -I<snapshot>/Source -I<snapshot>/Tools TunedPerformanceRenderer.cpp <snapshot>/libAcustraDSP.a -o <snapshot>/TunedPerformanceRenderer`. Both exact expanded commands and all snapshot hashes are in the build manifests. |
| Zero-cent proof | `python3 build-audit/capture-implementation-20261011/ab/preflight.py` |
| Freeze cases | `python3 build-audit/capture-implementation-20261011/ab/targeted_ab.py prepare` |
| Before rendering | `OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 build-audit/capture-implementation-20261011/ab/targeted_ab.py before --workers 1` |
| After rendering | `OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 build-audit/capture-implementation-20261011/ab/targeted_ab.py after --workers 1` |
| Measures, figure and sweep WAVs | `OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 build-audit/capture-implementation-20261011/ab/compare_ab.py` |
| Short exact-centre WAVs | `python3 build-audit/capture-implementation-20261011/ab/package_centres.py` |
| Independent kernel check | `OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 build-audit/capture-implementation-20261011/ab/green_series_review.py` |
| Tracked evidence | `python3 build-audit/capture-implementation-20261011/ab/package_evidence.py` |

For a fresh local replay, copy the Python scripts, scratch C++ fixture, both
frozen `before/` and `after/` snapshots and their `*-manifest.json` files to the
fresh ignored directory. Run the zero-cent proof, freeze the plan, render both
versions, compare, and package centre clips there. Do not copy existing
`events`, `*-renders`, `preflight`, `comparison` or `plan.json`: existing output
directories are intentionally refused. The exact same copied script bytes and
snapshot receipts are required because the plan verifies their hashes.

The source/library snapshots are deliberately not tracked here. Rebuilding
them elsewhere requires the matching sources/compiler; never substitute the
current library for the frozen baseline. These are native arm64 macOS builds.
There is no runtime observer-injection file or custom studio environment in
the A/B fixture. The sole fixture extension is bounded `--master-tune-cents`,
called before notes; event wheel bend remains zero.

The two full per-render reports are stored as deterministic gzip archives of
their **exact original JSON bytes**. Decompress them with Python's `gzip` to
inspect all commands/windows/hashes; `receipt.json` records both uncompressed
and archived SHA-256 values. No results were recomputed or rounded for storage.
Comparison JSON and CSV remain uncompressed and readable.

The supplementary independent kernel check uses the earlier research
implementation under `build-audit/studio-mic-research`, not the production
generator's numerical functions. Its exact helper copies are supplied under
`references/`. To replay this check alone if those ignored references are
missing, place `prototype_observer.py` and `export_candidates.py` at that
research-directory root and `free-source-fit-manifest.json` at
`runtime-free-candidates-v1/manifest.json`. The current repository's
`Tools/AuditBridgeSpatialMap.py` is an import dependency; its analysis function
is not executed by this check. Kernel source hashes are recorded separately.

Centre auditions contain before at 0–4s and after at 4.35–8.35s. Full sweep
auditions contain alternating before/after at −100c, 0c and +100c; exact offsets
are in their manifests. One AFTER gain per capture is derived from the whole
primary Finger96 sweep and reused for every audition. Raw measurements retain
native levels. No human listening approval is claimed.
