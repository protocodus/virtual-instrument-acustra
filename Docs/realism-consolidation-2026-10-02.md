# Realism consolidation, 2026-10-02

Baseline: `d116139550ce56d099e5a92591b4fa141fe320eb` (main).
The user approved pursuing the promising work and consolidating existing
experiments only where they add realism. This branch changes DSP and playing
behaviour without adding visible controls or restoring removed legato.

## Selected changes

**Strum the fretted shape.** The Performer previously scheduled a stroke by
MIDI pitch order before asking the allocator for a chord shape. Pitch and
string order can cross: C4 on G5, G4 on B8, E4 on the open high E. It now
forms the same shape first and times the stroke across its actual strings.
Skipped strings take their physical traversal time. Alternating direction,
shared speed variation and existing Gather Chords latency remain in place.
An incomplete plan, natural-harmonic group or controller-owned layout keeps
its previous scheduling. Duplicate keys share a string's rank while retaining
their existing ownership behaviour.

**Keep bridge-hand pressure live on retained waves.** A re-pluck captures the
old wave in a separate tail branch. Its pressure-dependent loss coefficients
previously froze at capture. CC2 now updates that branch's loss and high
frequency damping at the normal control cadence. Its captured pitch, delay,
intrinsic filters, impedance and independent 10 ms damping stay fixed.
Unchanged pressure preserves the original coefficients exactly. This is a
modest correction: the existing 10 ms loss already suppresses the initial
wave strongly, especially on bass strings, but bridge back-drive can keep
the branch ringing. With an identical imposed bridge motion, a high-E tail
returns about 1.2–1.3 dB less wave energy after 30 ms of full pressure at
330 Hz, 2 kHz and 6 kHz. Stationary supplied-work checks remain passive.

**Extend only the Original's principal air decay.** The two Original radiation
components measured near 90 Hz use a Q multiplier of `23.5 / 19.311932`.
Their bridge twins take the same multiplier through the common pole helper.
This raises the main default free radiation T60 from 0.501 to about 0.610 s.
The weaker joint component keeps its relative damping; the separate 83 Hz
joint component, higher modes and Bellido poles retain their Q. Frequencies,
residues and the accepted spring-back share stay fixed. Construction output
gains are remeasured for Original so the longer ring does not undo the
existing loudness and headroom policy. Bellido gains stay fixed.

## Correction to the earlier decay claim

The 2026-10-01 decision-log claim that a median 85–105 Hz envelope T60 of
0.46 s versus 0.47 s established matched air decay was too strong. That band
starts above the model's intrinsic 84.68 Hz pole, includes adjacent string
components and adds its own filter ring. The original entry is retained as
history and corrected by the new dated entry.

`Tools/AuditAirMode.py` fits a damped sinusoid in the source domain after a
broad low-pass, alongside nuisance tones, DC and a trend. It repeats five
combinations of fit window and broad low-pass corner, with a 79–109 Hz model
search. The observed output transient is near 99.04 Hz, even though the
intrinsic radiation pole is 84.68 Hz. A shift caused by the reciprocal
bridge/anchor system is an inference; the rendered transient is measured.
An intrinsic 94.2 Hz retune therefore cannot be assumed to improve the
audible centre frequency.

The recording estimates below are apparent output-component decays, including
microphone and room effects. The stability screen checks fit sensitivity and
component attribution; it is not a statistical confidence interval. Eastman
fits explain less low-frequency variance than Martin's.

| Source | Screened high notes | Median T60 | Interquartile range |
|---|---:|---:|---:|
| Eastman picked | 18 | 0.646 s | 0.573–0.798 s |
| Eastman finger | 7 | 0.508 s | 0.500–0.538 s |
| Martin HD28 | 5 | 0.552 s | 0.535–0.578 s |
| Baseline model, picked | 23 | 0.450 s | 0.450–0.452 s |
| Baseline model, finger | 26 | 0.451 s | 0.451–0.451 s |
| Q23.5 model | 49 Eastman pitches | about 0.504 s | narrow spread |

The selected extension lengthens the rendered component by about 12%, with
only about 0.06 dB change in its extrapolated onset level. It approaches
finger-plucked Eastman and Martin decay without trying to force the model to the
longest Eastman picked tails. The larger Q28 and Q33 candidates start to
regress picked-note scores.

## Air level and apparent open tones remain separate problems

Matched-pitch reference-minus-baseline measurements put Martin's air onset
about 4.86 dB higher. Screened Eastman finger rows are approximately equal;
the picked model is already about 1.6 dB stronger. A global air gain increase
would not be supported by all three takes, so no such increase is included.

The Eastman pre-onset projected open-tone power is only median 0.56% of
post-onset power for picked notes and 0.22% for finger notes. None of the 58
examined high-note rows has pre-onset amplitude at least half the post-onset
amplitude on a tone holding at least 10% of post-onset projected power.
Earlier-note carry-over does not explain the general finger-take discrepancy.
Noise and unresolved components can still contribute; this does not prove
that all post-onset power is new sympathetic excitation.

Apparent open-tone RMS at 220 ms is 4.65 dB higher in the Eastman finger take
than on matched model pitches, versus 0.89 dB on Eastman picked and 0.07 dB
on Martin. The finger difference is dominated by a quickly dying component
near 165 Hz, which could include the low-E octave or a body mode. The fixed
slow nuisance-tone model does not identify which mechanism owns it. Resolve
that component's frequency/decay and hand state before increasing sympathetic
coupling. The shipping engine already has a reciprocal six-string junction.
Restricting the comparison to MIDI 64 and above leaves the finger discrepancy
unchanged. Removing only the 165 Hz component reverses it: the remaining
projected tones are median 2.93 dB stronger in the model.

## Existing branch disposition

Most useful work is already in the baseline. These are dispositions of
branch tips, not a recommendation to reapply their old patches.

| Existing work | Disposition |
|---|---|
| `cand/hand`, `split/performer`, `cand/snap`, `cand/d` | Already integrated; retained. |
| `cand/body-bd`, `cand/body-blend`, `cand/body-mid`, `cand/body-lite`, `cand/body-one-pole-steel-body`, `cand/body-t1-band-plate-q` | Already integrated; retained. |
| `cand/piezo-circuit`, `feat/outputs-piezo`, `feat/audit-fixes` | Already integrated; retained. |
| `cand/dyntimbre`, `cand/midq`, `candidate/chord-gather` | Adapted/cherry-picked into main (`1a2c7e1`, `9efc25b`, `aebcc2c`); no duplicate merge. |
| `cand/cpu` | Superseded by integrated `cand/cpu2`; no old optimization replay. |
| `cand/body-joint-pole-body` | Its coherent common-pole idea is already used in the weak low-frequency blend. Full replacement failed cancellation/radiation/bridge gates; excluded. |
| `cand/body-decay-q-grid` | Adds 234 mostly upper modes and callback cost; mixed/worse corpus results, and does not fix air decay. Excluded. |
| `cand/hfloss` | Full measured winding loss regressed Eastman Pick by 20.4%, Martin by 19.4% and GuitarSet log error. Existing by-ear partial loss retained. |
| `cand/roomfree` | Most corpus results worse; air Q falls to 16.6, shortening the decay. Excluded. |
| `cand/transient` | Listener rejected the fitted pick click as too loud. Zero-gain mechanism retained; no reactivation. |
| `candidate/anechoic-body` | Damped g21 candidate rejected twice by ear. Excluded. |

## Descriptor checks

Scores are engineering descriptor losses (lower is better), not listener
realism ratings. The air sweep used the same frozen calibration, targets,
technique policy and dry default construction on both sides.

| Corpus | Baseline | Q23.5 | Change |
|---|---:|---:|---:|
| Eastman picked, 49 rows | 5.4962 | 5.4952 | −0.02% |
| Eastman finger, 55 rows | 6.2385 | 6.2289 | −0.15% |
| Martin, 11 rows | 6.3463 | 6.3425 | −0.06% |
| Bank flat-top | 6.4222 | 6.4141 | −0.13% |
| Bank steel training | 6.7386 | 6.7759 | +0.55% |
| Bank steel development validation | 6.7238 | 6.7635 | +0.59% |

The tiny flat-top score improvements alone do not establish audible benefit.
The bank's picked archtop sources prefer a shorter low component; the
regressions are disclosed, rather than pooled away. Selection is grounded in
the rendered decay moving toward the flat-top instrument recordings while
preserving onset level and the chosen frequency. Native playing tests and
listening examples assess the other two changes separately.

After integrating the playing fixes and refreshing Original output gains,
the external totals are 5.4953 picked Eastman, 6.2300 finger Eastman and
6.3418 Martin. Bank flat-top is 6.4139, training 6.7747 and validation 6.7626.
The same small improvements/regressions remain; no new broad gain is hiding
the discrepancy.

## Validation

All 45 native CTests pass with Python tests required. The JUCE 8.0.14 plugin
processor test also passes. Strict C++17 compilation passes. New playing and
actual digital-pole assertions fail on the saved baseline, establishing that
they detect the corrected behaviours. Existing common-pole, positive-real
mobility, residue-PSD, ring stability, pitch, transition and construction
tests remain green. The audit rejects silence and lone decaying open tones,
while recovering known air decays beside stronger neighbouring string tones.

All 72 construction/Picking cells are within ±1 LU on each of three captures.
The Original gains restore the existing Pick sacrifice of 0.9 LU where the
hardest case needs it; Bellido gain entries are unchanged. This policy is not
a promise of unlimited headroom: simultaneous multi-channel velocity-127
chords still engage the limiter, as documented by the prior listening/parity
decision. The worst pre-limiter stereo peak moves from +1.59 to +1.70 dBFS;
the full level report retains every construction.

Native callback timing is retained as a paired diagnostic, separate from
host qualification. It measures noteOn plus process, with alternating
baseline/current order, equal compiler flags, 256 observations and 16 warmup
pairs per case. The two models, 48/96 kHz and 32/64/128 frames cover initial
six-string release, re-pick and an eighth natural harmonic. These cases use
zero CC2; they do not measure pressure gestures or the Performer scheduler.
Paired median ratios range from 0.975 to 1.020 (median 1.003 across cases).
Every case's p95 stays below its native block deadline (worst current ratio
0.508). Isolated overruns occur on both sides: 4 baseline and 5 current among
9,216 observations each, with a worst current ratio of 1.486. All observations
and worst-case values are retained in the JSON; this is one machine's
diagnostic run, not a real-time guarantee for a DAW.

## Reproduction and listening

Raw reference recordings, float renders and local audition WAVs are ignored,
not committed. Compact measurements, source hashes and test results are in
`realism-consolidation-2026-10-02.json`.

Configure the native build with Python tests required and a NumPy/SciPy Python:

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release \
  -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_UNIVERSAL=OFF \
  -DACUSTRA_REQUIRE_PYTHON_TESTS=ON
cmake --build build-dsp --parallel
ctest --test-dir build-dsp --output-on-failure
python3 Tools/AuditAirMode.py --self-test
```

Prepare the pinned sources with the existing Eastman/Martin corpus tools,
then benchmark and audit their manifests:

```sh
python3 Tools/BenchmarkOpenCorpora.py eastman/rows.json martin/rows.json \
  --renderer build-dsp/AcustraExternalCorpusRenderer --output new-scores --keep
python3 Tools/AuditAirMode.py --rows eastman/rows.json --rows martin/rows.json \
  --eastman-source eastman-source --output reference-air.json
python3 Tools/AuditAirMode.py --manifest new-scores/eastman.flattop-pick.json \
  --manifest new-scores/eastman.flattop-finger-all.json \
  --manifest new-scores/martin-hd28.martin-hd28.json --output model-air.json
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer build-dsp/AcustraPerformanceRenderer --check --json levels.json
```

`Tools/RenderRealismReview.cpp` provides four five-second before/after cases:
crossed voicing, skipped strings, re-pluck with palm pressure, and dry C5
decay. Build it against each frozen DSP library with that library's headers.
Both sides retain native relative levels without independent normalization.
Listening remains the check for whether the air change is preferable.
