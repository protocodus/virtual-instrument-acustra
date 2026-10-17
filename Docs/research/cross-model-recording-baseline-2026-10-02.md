# Recording baselines for both current models, 2026-10-02

The current instrument has two models: Original and Bellido 1978, both
steel-strung. Previous consolidation scores covered Original only. The
external and bank renderers now accept an offline `--body-material` option,
so Bellido's Auditorium/Mahogany preset can be evaluated explicitly. The
option reaches synthesis and recorded metadata. Omitting it retains Spruce
for both models; choosing a model alone does not apply its preset.

No production DSP or visible controls change in this follow-up. The
measurement compares the two current presets with available recordings to
establish separate baselines for future changes, rather than choosing one
model to replace the other.

## Controls and recording coverage

Original uses Dreadnought/Spruce; Bellido uses Auditorium/Mahogany. Both use
the unchanged shipping calibration, Stereo Mic, width 0.62, Touch 0.58,
Pluck Position 0.28, String Age 0.15, dry Room 0 and velocity from each
declared row. A fresh engine renders each unique note/tool/velocity.

The bank's 54 training and 14 development rows are picked, so they are
rendered with Pick. Its eight flat-top rows are rendered with Finger.
This differs from the historical Finger-rendered picked-bank scores;
those are retained separately for continuity. A lower score from using the
recorded tool is not a new sound-engine improvement. Development and
external sets have already informed previous work and are not pristine
final test sets.

Eastman provides 49 picked and 55 finger rows, with their declared tools
and assumed velocity 91. Martin HD28 provides 11 rows with unknown tool and
assumed velocity 91;
the primary protocol retains its existing Finger assumption. None of these
external sets establishes a velocity-response curve. They cannot alone
calibrate a Finger release law or contact pressure.

The measured g21 and Bellido source bodies were nylon-strung and are adapted
for steel in Acustra. The pinned generator metadata and current README
correction identify g21 as a nylon-strung DeVoe with Savarez Tomatito strings.
An earlier historical decision called it steel; that is not current evidence.
Available played steel recordings are of other instruments. These are
cross-instrument transfer benchmarks, not exact played-guitar validation
of either source body. The two models' measured body/bridge responses remain
separate evidence from these plucked-note comparisons.

## Scores

Descriptor loss is lower when the scored features are closer. It is not a
listener realism rating or a universal ranking of guitar constructions.

| Recording set | Rows | Original preset | Bellido preset |
|---|---:|---:|---:|
| Bank picked training | 54 | 6.3339 | 6.0731 |
| Bank picked development | 14 | 6.1315 | 5.6372 |
| Bank finger flat-top | 8 | 6.4139 | 7.4195 |
| Eastman Pick | 49 | 5.4953 | 6.1279 |
| Eastman Finger | 55 | 6.2300 | 7.0312 |
| Martin, assumed Finger | 11 | 6.3418 | 7.3683 |

Bellido is closer on the picked archtop bank; Original is closer on these
flat-top sets. Every model/recording combination is retained. Shape, wood,
recording instrument, microphone and technique differ between sources, so
the table does not identify a single body parameter to adjust.

Both models show an Eastman Finger attack discrepancy: median first-15-ms
2–12 kHz energy shares exceed the recording by about 14.5/14.7 dB, and the
80–250 ms H5–H12 versus H1–H4 balances by 9.3/7.8 dB. Martin's assumed
Finger attack is approximately matched. Partial-decay discrepancies also
change sign between external flat-top recordings and the picked bank.
This supports bounded, technique-specific experiments with Martin and
picked-note guards, not a shared damping or brightness adjustment.

The subsequent [bounded-screen report](realism-screens-2026-10-02.md)
retains the Finger-width, low-mode radiation and native reciprocal-Q
experiments. None passes its stated eligibility checks, so none changes
the instrument's sound.

## Verification and reproduction

The affected PhysicalFitRenderer, CalibrationVector and OpenCorporaBenchmark
tests pass (3/3). Manual native checks show:

- Omitted wood and explicit Spruce produce identical float audio for each
  model; each of the three supported woods produces distinct audio.
- Unknown, missing, repeated and retired Cedar options fail before output.
- Finger MIDI 64/velocity 91 is byte-identical between the bank and external
  renderers for both model presets. Their reported controls match the
  intended shape, wood and model.

The accompanying [JSON](cross-model-recording-baseline-2026-10-02.json)
records all twelve score summaries, full weighted residuals, source/binary
hashes, controls, commands, target/manifest hashes and option-check results.
Raw audio remains in ignored local folders.

With the prepared reference corpora, build both renderers and choose fresh
output directories:

```sh
cmake --build build-realism/next-current --target AcustraPhysicalFitRenderer AcustraExternalCorpusRenderer -j 4
python3.11 Tools/BenchmarkOpenCorpora.py build-realism/corpora/eastman/rows.json build-realism/corpora/martin/rows.json --renderer build-realism/next-current/AcustraExternalCorpusRenderer --shape dreadnought --body-material spruce --guitar-model original --output build-realism/new-original --keep --jobs 2
python3.11 Tools/BenchmarkOpenCorpora.py build-realism/corpora/eastman/rows.json build-realism/corpora/martin/rows.json --renderer build-realism/next-current/AcustraExternalCorpusRenderer --shape auditorium --body-material mahogany --guitar-model bellido1978 --output build-realism/new-bellido --keep --jobs 2
```

The recorded bank commands give all 37 shipping calibration values and
`--archtop-picking pick`; its flat-top schedule retains Finger. Native
timing is not compared during these concurrent measurement runs.
