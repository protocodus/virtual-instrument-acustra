# Paired CPU benchmark for sound refinements

This standalone C++17 tool compares two frozen DSP trees with identical compiler
flags and separate namespaces. It permits intended audio differences while
checking finite, nonzero output, actual contact state and event delivery. The
measured harness and builder are exact copies of the 2026-10-15 evidence inputs.
See [the result and provenance](../../Docs/validation/sound-refinements-2026-10-15/cpu/README.md)
and [the complete protocol](PROTOCOL.txt).

Both source roots must contain `Source/DSP/AcustraEngine.*`,
`Source/DSP/AcustraPerformer.*` and their matching generated/header dependencies.
A Git revision alone does not recover a snapshot containing uncommitted changes.
Keep the complete frozen source trees with the resulting manifest.

From this directory, with fresh `build/` and output paths:

```sh
python3 build.py prepare /absolute/baseline-source build
python3 build.py finish /absolute/candidate-source build
build/AcustraSoundRefinementBenchmark smoke 0 --smoke
```

The optional three-case smoke establishes functional harness operation. Its
measurements are explicitly marked nonfinal and the summarizer rejects them.

After all collaborators confirm builds, tests and rendering are idle:

```sh
python3 run_final.py 1
python3 summarize.py run-1.json
```

The runner verifies source and executable hashes, pins the process to one
allowed CPU, preserves command/affinity/process metadata, and refuses to
overwrite existing final evidence. `build.py` also refuses to overwrite a
prepared directory. Use a fresh copy of this tool directory for another
independent experiment. Start with one sufficiently sampled run and repeat only
if uncertainty warrants it. The raw CSV contains every measured callback pair;
the JSON contains per-configuration statistics and audio/state checks.

No benchmark target is added to the plug-in or standard test build. Preserve
both the standalone tool and its source snapshots when sharing evidence.
