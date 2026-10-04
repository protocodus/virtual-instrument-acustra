# Ten guitar behavior improvements and a listening review

Ten changes are committed separately against baseline
`567cf692b975addeded686b38f161b95ea212691`. The DSP candidate is
`9dc7ac9fbb26f44fbbafad987d8c3539d4681c19`. Each declared candidate
regression passed. The real-recording benchmark preserves isolated-note
behavior exactly; it does **not** demonstrate a perceptual realism increase.
The user subsequently accepted this candidate, requested a fret-friction
follow-up, and instructed adopting and pushing the current candidate. See
`Docs/decisions.md` for the verdict and follow-up validation. The benchmark
results below remain the measurements of the original ten-change candidate.

| # | Commit | Result and engineering evidence |
|---|---|---|
| 1 | `1cb1345` | Actual stroke force changes Finger/Thumb release brightness and Pick impact consistently. Random stroke gain no longer cancels itself out of the brightness reference. Tests cover three sample rates and preserve first, nonstrummed attacks. |
| 2 | `0d69440` | Sliding follows physical speaking length and fractional fret decay; attack settling follows bent tension. Slide-versus-physical-fret decay mismatch falls from 38.085% to 0.00886% in the fixture. |
| 3 | `5f68f82` | Intrinsic partial loss follows played pitch. Worst physical-law error falls from 102.84% to 6.44% across 1,264 comparisons. Expensive dispersion fitting is bounded; extreme six-string slides incur six fit misses per rate in the diagnostic. |
| 4 | `fbbd48e` | Superseded same-sample construction requests preserve existing body/bridge tails. Continuity and cancellation regressions cover 44.1/48/96 kHz. |
| 5 | `719c6b8` | Automatic voicings require a feasible conventional four-finger/barre grip. Open or lower frets block false barres. Already sounded notes and fallback behavior are preserved. Thumb-over playing is outside this model. |
| 6 | `3ab86a0` | Live retuning preserves idle sympathetic string waves. Reset still clears them; palm-contact control remains effective. Three rates and two pressures are exercised. |
| 7 | `46cdcb3` | Recent equal-note, equal-velocity single repeats get bounded ±1 dB force variation with an independent deterministic stream. First notes, accents, changed notes, strums, duplicates and long rests retain their authored force. The 512-draw fixture has mean gain 1.00221. |
| 8 | `e843e4d` | Rapid soft strums finish physical string traversal using the last observed stroke interval; pending far-string attacks no longer get indefinitely replaced. Reset, direction, duplicate dyads and determinism are covered. |
| 9 | `c874027` | Explicit connected finger gestures preserve the vibrating string and transfer ownership. CC65 opts in, and one-shot CC84 identifies the held source. A smooth 2–3 ms contact has at most 2% of the source slope-energy budget. Ordinary overlap remains a chord; CC68 stays ignored. |
| 10 | `9dc7ac9` | Explicit key-up velocity changes passive damping speed. MIDI 64/127 and unspecified release retain nominal behavior; soft/firm releases take roughly twice/half the nominal time. Key-up does not create another picking attack. |

The baseline fails the executed regressions for changes 1–6, 8 and 10.
Changes 7 and 9 use **compile negative controls**, because the old engine lacks
the new test/API state; those are not reported as executed behavioral failures.
The regression log records exact commands, exit statuses and commit IDs.
These fixtures test physical and event contracts, separately from auditory
preference. The new repeat, finger-contact and release mappings are authored
listening hypotheses rather than recording-calibrated force laws.

## Measurements against real recordings

The same dry-note protocol scores 115 rows for each guitar preset: 49 Eastman
Pick, 55 Eastman Finger, and 11 Martin notes with an assumed Finger tool.
All rows are retained, with no rejected, skipped or unplayable rows. Descriptor
term coverage is unchanged. Every static render remains byte-identical to its
frozen baseline (83 distinct renders per preset), so every loss and term is
exactly unchanged.

| Reference | Original baseline / candidate | Bellido baseline / candidate |
|---|---:|---:|
| Eastman Pick | 5.494985 / 5.494985 | 6.128078 / 6.128078 |
| Eastman Finger | 6.229272 / 6.229272 | 7.031288 / 7.031288 |
| Martin, assumed Finger | 6.344293 / 6.344293 | 7.373790 / 7.373790 |

Lower loss means closer measured features on these same recordings, not a
perceptual realism score. Instrument, microphone, source tuning, pre-roll and
clean-duration differences contribute to these totals. Each isolated-note
render starts a fresh engine; this protocol does not measure successive
plucks, automatic strum timing, connected articulations or explicit release
gestures. These recordings also informed earlier development and are not an
untouched held-out evaluation.

Eastman Opus masters match their repository-pinned SHA-256 values. Preparation
passes all sixteen published onset/pitch anchors. Linux-generated target
float bytes differ from the historical macOS artifacts; historical hashes
were retained. The new local provenance freezes source masters, preparation,
decoder/dependency versions, rows and targets and links them to this task's
baseline. Its row hash is
`78b03c69d3c2de85201446fe65e291093f5d35e6e5eff43406af484a29e7ba2f`.
Martin downloads also pass their pinned Git-blob and SHA-256 checks. Both
reference sets are CC0; source credits are retained in the evidence.

Eastman repeated-pitch groups show 20–250 ms RMS median within-group standard
deviations of 1.09 dB Finger and 1.47 dB Pick across nine groups each. Actual
force, velocity and string/fret assignment are unknown. This contextualizes
the conservative repeat cap, without identifying a force or contact-noise law.

## A/B approval package

`build-realism/listening-test/Acustra-AB-listening-test.zip` contains a static
page, manifest, instructions and 25 stereo PCM16 WAV files. Unzip it and open
`index.html`. Vote on the four songs, reveal the revisions, choose an explicit
decision and export the review JSON. No score automatically approves changes.

The four original songs are Evening fingerpicking (17 s), Open road strumming
(19 s), Connected melody (17 s), and Repeated-note groove (17 s). The optional
10-second study exercises sympathetic retuning and superseded body choices.
Each has Dry and Studio room versions: ten A/B pairs in total. The extra real
Eastman E4 clips are labeled as different instruments/isolated performances,
not real takes of these songs.

Both revisions receive byte-identical event scores at 48 kHz. Each renderer is
compiled directly from DSP files verified byte-for-byte against its exact Git
commit. The manifest records source, compiler, renderer, score and audio
hashes. Matching applies one scalar gain per whole file; loudness measurement
uses BS.1770, with no EQ, compression, limiting, alignment or per-note gain.
Actual paired loudness differs by at most 0.02 LU. All audio is finite and
nonsilent, with true peaks below −2 dBTP. Every pair has a nonzero waveform
difference; this demonstrates a changed performance, not an improved one.

A/B identities are randomized once per track and persist locally. The package
content hash prevents regenerated audio or scores from inheriting an earlier
vote or approval. Source-decoded votes remain hidden in exports until Reveal.
HTTP playback schedules both decoded sources on the same Web Audio clock and
crossfades gains over 10 ms without restarting the phrase. Direct file opening
uses synchronized media elements; use a local static server if tighter
switching is needed. The page discloses this fallback's weaker alignment.

## Completed validation

The final Release build succeeds, including the C++17 shared-DSP target and
song renderer. The complete serial CTest run executes **57/57 passing tests**,
with zero failures and zero skips, in 428.28 seconds. It includes all ten new
regressions and the existing engine, allocator, performer, release, capture,
construction and Python tool checks.

The final listening package passes **14 browser checks**, with zero page or
console errors: decoding and synchronized switching, keyboard controls,
seeking, mode changes, persistence, both-score voting, reveal/approval gates,
blind exports, explicit decisions, mobile layout, and missing/silent audio.
One additional check is explicitly skipped: managed Chromium's administrator
policy blocks direct `file:` navigation. The shared media-element fallback
passes over HTTP with Web Audio unavailable. Browser evidence records the
exact review ID and manifest hash.

Repeated source builds produce byte-identical audio for all 20 raw renders.
The ZIP passes CRC checks and contains the exact final manifest/page. The
builder rejects an incorrect DSP revision and a non-exact commit label.

## Reproduce and inspect

Use the prepared virtual environment, or install the dependencies recorded in
the saved cloud setup: CMake 3.31.6, Ninja 1.11.1.4, NumPy 2.3.5, SciPy 1.17.0
and FFmpeg. The primary build is Release with plugin OFF, tools/tests ON,
universal OFF, and Python tests required. Generated files stay in ignored
`build-realism`; the baseline source is a Git archive, not a worktree.

From the primary repository, build and run the complete suite:

```sh
/workspace/shared/acustra-venv/bin/cmake --build build-realism/current-build -j 3
/workspace/shared/acustra-venv/bin/ctest --test-dir build-realism/current-build --output-on-failure -j 1
```

Stop CPU-heavy render/benchmark/browser processes while timing gates execute.
For the original recording comparison:

```sh
python Tools/BenchmarkOpenCorpora.py \
  build-realism/corpora/eastman/rows.json build-realism/corpora/martin/rows.json \
  --renderer build-realism/current-build/AcustraExternalCorpusRenderer \
  --shape dreadnought --body-material spruce --guitar-model original --room 0 \
  --output build-realism/candidate-original --keep --jobs 2 \
  --compare build-realism/baseline-original
```

The Bellido run uses `--shape auditorium --body-material mahogany
--guitar-model bellido1978`, output `candidate-bellido`, and comparison
`baseline-bellido`. Frozen baselines use the corresponding renderer from
`baseline-build` built from the archived baseline source. Reproduction needs
the same verified corpus bytes and dependency versions.

For a fresh listening output directory:

```sh
python Tools/BuildRealismListeningTest.py \
  --baseline-source build-realism/baseline-source --candidate-source . \
  --baseline-commit 567cf692b975addeded686b38f161b95ea212691 \
  --candidate-commit 9dc7ac9fbb26f44fbbafad987d8c3539d4681c19 \
  --out build-realism/listening-test \
  --reference-rows build-realism/corpora/eastman/rows.json \
  --reference-evidence build-realism/corpora/eastman/verified-provenance.json
NODE_PATH=/workspace/shared/acustra-browser/node_modules \
  node Tests/ListeningReviewTests.cjs build-realism/listening-test \
  --artifacts build-realism/final-listening-ui-check
```

`BuildRealismEvidence.py --help` documents the paired-report and optional
completion-check inputs. The committed [evidence JSON](realism-ten-improvements-2026-10-04.json)
records exact outcomes, reference/model hashes, exclusions and term coverage.
The final CTest sidecar/JUnit and browser report remain in ignored
`build-realism`. Browser automation decisions explicitly say they are test
data, never user approval.

The final evidence command uses a new output file:

```sh
python Tools/BuildRealismEvidence.py \
  --baseline-original build-realism/baseline-original \
  --candidate-original build-realism/candidate-original \
  --baseline-bellido build-realism/baseline-bellido \
  --candidate-bellido build-realism/candidate-bellido \
  --regression-log-dir build-realism/regression-logs \
  --ctest-run build-realism/final-ctest-run.json \
  --browser-report build-realism/final-listening-ui-check/browser-test-report.json \
  --listening-manifest build-realism/listening-test/manifest.json \
  --reference-provenance build-realism/corpora/eastman/verified-provenance.json \
  --out Docs/realism-ten-improvements-2026-10-04.json
```

The shared DSP builds as C++17 for Rack compatibility. Full JUCE plugin and
licensed Reason Jukebox SDK builds were not run; these checks do not qualify
host integration. The current Rack interface cannot send raw CC65/CC84 or
release velocity. At the time of this original review, its Instrument submodule
had not been advanced and no changes had been pushed. The later adoption and
publication instruction is recorded in `Docs/decisions.md`.
