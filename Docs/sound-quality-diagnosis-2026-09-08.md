# Recording diagnosis, 2026-09-08

The strongest repeatable directions are **less loss on nylon wound bass strings**, **less excess fundamental loss in the steel treble register**, and **earlier, faster-settling bass contact energy**. A global brightness, sustain or pitch adjustment is not supported by these recordings. These are experiment priorities, not accepted improvements or a listening verdict.

## Evidence and reproducibility

The source checkpoint is `dfa1002`. This audit extracted descriptors again from **187 actual target/model audio pairs**: 83 dry training, 24 dry development, 8 separate flat-top anchors, and 72 Yamaha/Walden finger-or-thumb pairs (18 pitches × two takes × two materials). No protected test targets were opened. The development pitches have already been inspected in previous work and are not a blind holdout.

Authoritative retained experiment root:

```
/var/folders/jl/cjv384t54g3bpst5xzs9zqnc0000gn/T/acustra-realism-20260908-8bca92he
```

| Artifact under that root | Meaning | Frozen executable SHA-256 |
| --- | --- | --- |
| `dry-shape-final/corpus` | Current Finger, Original, Dreadnought; selected nylon calibration and Gaussian contact | `f2b6b8d2d93b292b05ddc3ba1a1e62d4b18a4614419ea8ef2fe79ffd37371cfb` |
| `recorded-final` | Same selected string calibration/contact; Original, Auditorium; nylon Spruce and steel Mahogany | `3a12e40644eed7cca9881ba8735dd82335f8f5d82def5cd7512e3f925a2421a1` |
| `guitarset-final` | Six annotated performances, default Finger/Dreadnought; baseline and current separately frozen | See its `report.json` and `Docs/realism-performance-comparison-2026-09-08.json` |

`/tmp/acustra-goal-candidate` is **not** this baseline: it contains a rejected body calibration with Q scale about 0.053 and low-mode gain 7.5. A directory called `final` is insufficient provenance by itself.

New local analysis is in `/tmp/acustra-sound-diagnosis-20260908/`: `analyze.py`, `features.json`, `summarize.py`, and `summary.json`. It uses unchanged `Tools/FitPhysicalModel.py` feature extraction, whose SHA-256 is `94ec199da74c3432801225829028abf25a100d73d66015e754d6dea8da80c7bd`. The result records hashes of both frozen executables and the analysis script. The Kaggle archive is verified against `4563a70d5dd9a14919171d103f10f6731fd78e63f1498dd11047bfea471f6a6d`, and each selected source member is verified against the retained selection hash.

The audio was inspected numerically; no human listening judgement is asserted. Dry references are the FreePats Spanish classical, Shinyguitar acoustic microphone, and Eastman E1D anchors described in the repository. The other dry notes are the author's Yamaha CM-40 and Walden G551E recordings from the [Acoustic Guitar Notes dataset](https://www.kaggle.com/datasets/mohammedalkooheji/guitar-notes-dataset). Their capture, velocity, fingering and contact position are unknown. Its `f` label merges finger and thumb. The modeled lowest-fret assignment is an assumption. The source clips are two seconds, which censors slow decay estimates. The dry sample-bank gains are calibrated playback gains, not recorded force or SPL; level differences cannot identify pluck energy.

## Decay: direction matters more than the pooled score

Numbers below are median **paired model-minus-target** errors in decay rate, in dB/s. Positive means the model fades too quickly. H1 is the fundamental; other columns pool the indicated partials. Bass means MIDI below 55, middle 55–63, treble 64 and above. Repeated takes sharing one model are not independent rendered notes.

| Recording/register | Pairs | H1 | H2–H4 | H5–H12 |
| --- | ---: | ---: | ---: | ---: |
| Dry nylon bass | 10 | +6.39 | +10.71 | +8.58 |
| Yamaha nylon bass | 12 | +6.54* | +8.40 | +12.60 |
| Dry nylon treble | 20 | −4.17 | −2.39 | +4.19 |
| Yamaha nylon treble | 16 | +6.42 | +0.16 | −15.76 |
| Dry steel treble | 30 | +4.04 | +4.57 | +10.07 |
| Walden steel treble | 16 | +6.95 | +7.21 | −0.65 |
| Eastman flat-top treble | 4 | +6.62 | +11.41 | +27.14 |

*Only three of twelve Yamaha bass H1 pairs have a usable target slope. Its validation bass H1 has no usable slopes. Do not treat the H1 value as an independently established Yamaha bass trend. H2–H4 and H5–H12 have 21/36 and 88/96 comparable elements respectively. The 13 missing model upper-partial slopes in dry nylon treble remain explicit in the artifact; missing values were not scored as zero error.*

The nylon bass direction holds for all ten dry pitches individually. On dry training/development, H1 errors are +5.28/+7.59 dB/s; H2–H4 +10.71/+9.64; H5–H12 +7.41/+9.73. Yamaha training/development upper-partial errors are also positive: H5–H12 +12.73/+11.73. This supports a wound-string construction change, while the conflicting treble results argue against another global nylon T60/loss adjustment.

Steel treble low-order over-decay also occurs in both pitch splits. Dry H1 training/development errors are +4.04/+3.60 dB/s, and Walden +4.05/+13.10; dry H2–H4 +2.97/+10.08 and Walden +2.63/+7.53. There are exceptions at individual pitches, including dry MIDI 84 whose H1 already decays too slowly. Upper-order steel damping is less consistent across instruments and must be guarded separately. The Eastman subset has only four treble notes.

The slope extractor follows each signal's settled partial frequency, retains only a useful interval above its tail/noise estimate, and needs at least 180 ms of usable data. These slopes can include beating and coupled modes; they are not identified isolated-string loss constants. A microphone null or noise floor can change which decay segment is measurable. Report both descriptor availability and per-pitch errors for any candidate.

## Attack: too much late relative to early high-frequency energy

For each target/model pair, take the change in the same six logarithmic bands from 2.26–12 kHz between 0–12 ms and 40–100 ms after the scorer's onset. Then subtract the recorded change from the modeled change. This removes scalar gain, the attack normalization, and static per-band transfer. Positive means the model retains/builds too much late high-frequency energy relative to its initial attack.

| Bass recordings | Median error, training | Median error, development | Median across all pairs |
| --- | ---: | ---: | ---: |
| Dry nylon | +10.51 dB | +10.68 dB | +10.56 dB |
| Yamaha nylon | +14.28 dB | +14.05 dB | +14.28 dB |
| Dry steel | +18.86 dB | +23.88 dB | +20.02 dB |
| Walden steel | +10.88 dB | +10.40 dB | +10.49 dB |
| Eastman flat-top, 3 bass anchors | — | — | +10.34 dB |

For context, the median absolute difference in this temporal descriptor between available same-condition recorded takes is 2.35 dB for dry steel, 4.28 dB for Yamaha bass, and 4.35 dB for Walden bass. This does not establish confidence intervals—the bands and repeated takes are correlated—but the model discrepancy exceeds ordinary repeat variation in this sample.

The existing scorer sums FFT power without multiplying by bin width. Its different attack-window FFT sizes add **6.02 dB** to the raw late-minus-early quantity. That factor cancels exactly from each model-minus-target change above. It must be removed before interpreting either signal's physical growth alone. With that correction, median bass high-band changes are approximately −12.11/−2.44 dB for dry nylon target/model, −14.15/+0.60 for Yamaha, −18.77/−1.02 for dry steel, and −10.80/−1.30 for Walden. Differences of these separately computed medians need not equal the paired median in the table.

A broad static EQ or microphone trim cannot correct this timing pattern. Contact release, the propagated initial condition, body ring-down, and the onset detector are plausible contributors. The first 12 ms can contain finger/pick noise and source edits that are not part of a sustained harmonic string. Absolute onset latency is unknown and was not fitted. A candidate must retain the existing onset protocol and also show its effect with fixed source-relative windows; do not improve the score by silently realigning each candidate.

## Pitch and body balance are separate limitations

The median settled H1 tuning of the references differs from equal temperament: dry nylon +2.71 cents, dry steel −4.55, Eastman +2.74, Yamaha +6.77, Walden −0.91. Current models are near nominal (+0.02 to +0.45 cents). This is not evidence for retuning the engine to one corpus. After subtracting each note's own H1 tuning, H2–H12 mean absolute relative-placement errors remain 6.01 cents for dry nylon, 5.47 for dry steel, 6.05 for Eastman, 11.85 for Yamaha and 8.78 for Walden. Their median H12 errors have mixed signs, so these measurements do not justify a common stiffness adjustment. Median pitch-trajectory absolute errors are only 0.60–1.93 cents across these groups, much smaller than the attack/decay discrepancies in their respective tolerances.

On the six GuitarSet clips, current model-minus-reference whole-clip octave-band differences average −18.75 dB at 80–160 Hz, +3.84 at 320–640 Hz, −5.36 at 1.28–2.56 kHz, −9.17 at 2.56–5.12 kHz and −9.67 at 5.12–10 kHz. The 80–160 Hz deficit spans −33.73 to −12.14 dB across players. These spectra use whole-clip RMS normalization, not per-band matching. The current comparison improves spectral convergence and chroma distance but worsens log-magnitude error (13.148 → 13.541 dB). Gesture, note velocity, guitar body, room and microphone orientation are unmatched. This remains a useful performance guard, not proof that adding bass and treble to every dry note is correct: the Eastman anchors already have excess modeled attack high-band energy, while the dry archtop has too little in the first window.

## Three concrete experiments, in order

1. **Remove the unintended frequency dependence of the extra vertical per-cycle attenuation.** `configureVoice` first compensates the loss filters to achieve `fundamentalT60`, then multiplies the result by `0.9995` for the radiating loop (`0.9988` for the other axis). A fixed multiplier per period adds `−20*f0*log10(0.9995)` dB/s: 0.358 at E2, 1.43 at E4, and 3.41 at G5. It shortens high-note sustain even after the intended T60 calculation. A conservative test preserves the E2 loss and uses `pow(0.9995, midiFrequency(40)/frequency)` for the vertical multiplier only. Keep horizontal unchanged initially to isolate the audible path. This is an authored loss cleanup, not a new measured constant. Inspect the final `loopGain` clamp: if compensated gain hits `0.999995`, the effective change can be smaller than the formula suggests. Log pre/post-clamp gains and native H1 decay, including the adverse MIDI-84 case. The complete loop/bridge passivity check remains necessary because compensating a filter at H1 does not alone bound every frequency.

2. **Separate wound-nylon intrinsic losses from the plain-nylon law.** The current `(4.1 seconds)*fundamentalT60Scale` law has only a common material scale and fixed fret shortening, while `broadLoss` uses the same nylon viscous coefficient for all strings. Start with one bounded experiment on `stringIndex < 3`: multiply intrinsic nylon T60 by 1.2, 1.35 or 1.5, selecting on training only; the available dry/Yamaha H2–H12 trends support less bass loss. If H1 improves while H2–H4 remain too fast, separately test a 0.8–1.0 multiplier on that construction's broad loss, preserving H1 compensation. Do not alter plain nylon from the conflicting treble evidence. Keep bridge mobility fixed: its real resonances and calibrated floor are additional losses, not intrinsic string damping. A decrease in a global bridge floor could make multiple notes appear better while erasing a measured interaction.

3. **Test physically timed contact injection before raising contact amplitude.** Current `renderExcitation` emits a short burst, and `finishVoice` writes it into the collapsed round-trip loop; it reaches the radiating junction roughly one period later. A retained native audit at revision `5e9b158...` measured a low-E causal body difference at 11.54 ms, while the conditional local-pluck direct arrival was 0.71 ms. The current code retains this topology. The report is `/tmp/acustra-attack-noise-path-review/report.json`; its older absolute spectrum should not be treated as current sound evidence. A bounded candidate should propagate the same existing steel contact through direct and nut-reflected paths from the physical pluck position, with explicit displacement/velocity units, signs and energy accounting. Do not add a second microphone noise layer or duplicate the existing loop write. Nylon currently has `transientScale=0`; steel should establish whether timing helps before introducing a new nylon burst. Simply changing initial triangle phase is insufficient: previous rest-state/preload experiments already documented unresolved coupled filter/history state and held-out regressions. This is an exploratory source experiment, less certain than the first two loss changes.

The stronger shape morph, named-body choices, scalar microphone trims and renewed horizontal-axis routing are not part of these proposed changes. Existing saddle-height experiments must not be repeated as though untested; their coupled tuning/phase issue remains documented in README and `Tools/SaddleHeightExperiment.patch`.

### Follow-up: the first broad steel loss candidate does not generalize

The experiment owner tested the E2-anchored vertical-loss change on steel training only. Dry steel total/decay losses improved from 6.091584/4.095839 to 6.047128/3.961981, but Walden training worsened from 9.132877/5.209248 to 9.191158/5.334756. No development scores were consulted for this candidate. This is grounds to reject the broad change, despite the useful diagnosis of the extra per-cycle loss.

Re-extracting the candidate's nine Walden training models shows why. The larger penalties occur in H5–H12 at MIDI 55, 59, 64, 69 and 74, whose relevant partials were already too slow or become worse under the changed decay. MIDI 74 also worsens H2–H4 and band-decay terms. MIDI 79 improves, but does not offset them. H1's measured reductions vary: −1.47 dB/s at MIDI 64, −1.03 at 69, −0.41 at 74 and −0.98 at 79. There is no usable target H1 slope at MIDI 69, so that improvement cannot contribute to the score. A reduction in fundamental attenuation affects all partials, not just H1; it also changes the steel energy/pitch trajectory and the dominant observed decay. The aggregate conflict is not fixed by interpreting missing slopes as zero. Per-pitch sums of the actual squared-Huber penalty changes are retained in `/tmp/acustra-sound-diagnosis-20260908/loss-candidate-attribution.json`, and frozen inputs/results are in `/tmp/acustra-loop-loss-20260908`.

The contact alternative began as the historical `Tools/ContactTravelExperiment.patch`. It splits the existing ordinary-steel burst into equal-energy direct and nut-reflected traveling waves, inserts them before the shared junction, and removes the old boundary injection for those voices. It introduces no control or fitted amplitude. Its qualified successor is now [integrated with release, repicking and retirement fixes](contact-travel-2026-09-08.md). The held-note recording gates passed, with small attack gains and mixed performance-monitor results. First-arrival dispersion/loss and fractional-delay limitations remain; this is not a complete physical contact-force solver.

The first steel-only residual-loss experiment has since been [rejected on training evidence](steel-loss-rate-experiment-2026-09-08.md): dry steel improves but Walden decay and total loss worsen. Its development split was not scored. Apart from the qualified contact transport, the remaining items above are still proposals.

## Run an isolated candidate against the same recordings

The local helper `/tmp/acustra-sound-diagnosis-20260908/run_frozen.py` copies both supplied executables, the DSP/tool source snapshot, and the existing dry corpus into a **new** output directory. It keeps all 29 calibration values fixed, rerenders dry models with `--models-only`, compares train/development/flat-top against the authoritative baseline, then reruns the fixed Yamaha/Walden selection without fitting. It writes a completion marker only after successful scoring. It never opens a blind set. Its CLI was checked; the full candidate run belongs to the experiment owner.

Use this full runner only after the training decision; it explicitly scores the already-used development sets. The copied source snapshot describes the workspace at invocation. The supplied frozen executable hashes are authoritative, and its builder must independently establish which source produced each executable, especially when rerunning an older baseline.

```sh
python3 /tmp/acustra-sound-diagnosis-20260908/run_frozen.py \
  --dry-renderer /ABSOLUTE/CANDIDATE/AcustraPhysicalFitRenderer \
  --performance-renderer /ABSOLUTE/CANDIDATE/AcustraPerformanceRenderer \
  --output /tmp/acustra-loss-candidate-NEW \
  --jobs 2
```

For an independently rerun frozen baseline, use the authoritative root's `dry-shape-final/renderer-snapshot` and `recorded-final/renderer-snapshot` as the two executable arguments and a different new output path. Baseline dry scores are 6.355798 training, 6.567155 development and 8.791977 flat-top. Candidate dry reports are `train-comparison.json`, `validation-comparison.json` and `flattop-comparison.json`; technique scores are `recorded/report.json`, to compare against authoritative `recorded-final/report.json`.

Acceptance must include signed register/partial residuals and coverage, not only those scalar scores. Use the declared training set to choose one candidate; inspect development once for that selection, preserve rejected results, and seek an additional disjoint instrument/gesture before claiming general realism. Retain native energy, pitch, sample-rate, release and callback checks. No new controls or features are needed for these sound experiments.
