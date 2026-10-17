# Fixed excitation holdout of two selected microphone mixtures

256 native clips per version; 768 total. Frozen weights were selected before this holdout and were not adjusted afterwards. All four shapes, Spruce/Mahogany, Stereo/Mono, pick velocity120 and thumb velocity56, MIDI40/41/42/45/47/51/53/59 on first-position strings; 48kHz, 3s held, 4s total. Native output with no post-render gain. Frozen metric and copied input/binary hashes are in manifest.json; exact commands and file hashes are in each report.json.

Target notes are Parlor47, Auditorium45 and Jumbo40/41. H1 balance means H1 power relative to the mean H2/H3 power. A negative balance change does not itself establish a healthier note: absolute H1 and upper-partial losses are reported separately.

## balanced_all

| Shape | Window | H1 balance change range | Median | Absolute H1 change range |
|---|---|---:|---:|---:|
| parlor | attack | -4.04 to -1.62 dB | -2.71 dB | -2.17 to -2.14 dB |
| parlor | early | -3.07 to -1.75 dB | -2.75 dB | -2.12 to -2.08 dB |
| parlor | held | -3.60 to -2.84 dB | -3.04 dB | -2.12 to -2.07 dB |
| parlor | late | -4.43 to -2.18 dB | -2.96 dB | -2.61 to -1.46 dB |
| auditorium | attack | -2.70 to -1.76 dB | -2.51 dB | -2.07 to -2.05 dB |
| auditorium | early | -2.60 to -2.20 dB | -2.53 dB | -2.02 to -2.00 dB |
| auditorium | held | -3.03 to -2.44 dB | -2.58 dB | -1.92 to -1.90 dB |
| auditorium | late | -3.92 to -2.62 dB | -2.89 dB | -2.43 to -2.12 dB |
| jumbo | attack | -4.27 to -1.95 dB | -2.93 dB | -2.31 to -2.12 dB |
| jumbo | early | -3.55 to -2.54 dB | -3.02 dB | -2.40 to -2.06 dB |
| jumbo | held | -3.63 to -2.67 dB | -3.16 dB | -2.45 to -1.96 dB |
| jumbo | late | -4.83 to -3.02 dB | -3.72 dB | -3.63 to -2.35 dB |

Target balance increases in 0/128 target/window cases. Maximum output peak -13.24dBFS; full-scale samples 0; Mono channel difference 0.

- Worst lower-note H1: -5.32dB, auditorium-spruce-mono_mic-pick-v120-m41-s1, late.
- Largest upper-note H1 gain: +1.97dB, parlor-spruce-mono_mic-pick-v120-m59-s5, held.
- Largest upper-note balance increase: +7.83dB, jumbo-spruce-mono_mic-thumb-v56-m59-s5, attack.
- Largest H2–H4 power loss: -16.99dB, H3, jumbo-spruce-mono_mic-pick-v120-m59-s5, late (baseline -83.38dBFS, candidate -100.37dBFS).
- Lowest stereo fold-down H1 retention relative to stereo channel-average power: -5.32dB, auditorium-mahogany-stereo_mic-pick-v120-m40-s1, late.
- Worst change of fold-down H1 retention versus baseline: -0.351dB, auditorium-mahogany-stereo_mic-pick-v120-m40-s1, held.

| Window | Worst lower-note H1 change | Baseline H1 | Candidate H1 | Case |
|---|---:|---:|---:|---|
| attack | -4.23dB | -57.73dBFS | -61.96dBFS | parlor-mahogany-mono_mic-thumb-v56-m40-s1 |
| early | -4.30dB | -60.35dBFS | -64.66dBFS | parlor-mahogany-mono_mic-thumb-v56-m40-s1 |
| held | -4.81dB | -75.36dBFS | -80.17dBFS | auditorium-spruce-mono_mic-pick-v120-m40-s1 |
| late | -5.32dB | -88.36dBFS | -93.67dBFS | auditorium-spruce-mono_mic-pick-v120-m41-s1 |

The largest upper-note balance increase is jumbo-spruce-mono_mic-thumb-v56-m59-s5, attack: H1 changes +0.93dB, while H2 changes -7.16dB (-64.58 to -71.74dBFS). Thus the roughly +8dB balance change mainly reflects H2 weakening.

## measured_continuity

| Shape | Window | H1 balance change range | Median | Absolute H1 change range |
|---|---|---:|---:|---:|
| parlor | attack | -3.26 to -1.18 dB | -2.12 dB | -1.84 to -1.82 dB |
| parlor | early | -2.56 to -1.69 dB | -2.13 dB | -1.82 to -1.81 dB |
| parlor | held | -2.60 to -1.96 dB | -2.15 dB | -1.77 to -1.73 dB |
| parlor | late | -3.77 to -2.25 dB | -2.64 dB | -2.53 to -1.98 dB |
| auditorium | attack | -2.13 to -1.33 dB | -1.97 dB | -1.88 to -1.82 dB |
| auditorium | early | -2.22 to -1.78 dB | -1.96 dB | -1.89 to -1.82 dB |
| auditorium | held | -2.43 to -1.84 dB | -1.96 dB | -1.75 to -1.71 dB |
| auditorium | late | -3.66 to -2.36 dB | -2.63 dB | -2.44 to -2.19 dB |
| jumbo | attack | -4.21 to -1.66 dB | -2.05 dB | -1.93 to -1.80 dB |
| jumbo | early | -2.58 to -1.88 dB | -2.05 dB | -1.96 to -1.78 dB |
| jumbo | held | -2.64 to -1.91 dB | -2.10 dB | -1.95 to -1.75 dB |
| jumbo | late | -3.94 to -2.46 dB | -3.02 dB | -3.31 to -2.31 dB |

Target balance increases in 0/128 target/window cases. Maximum output peak -13.36dBFS; full-scale samples 0; Mono channel difference 0.

- Worst lower-note H1: -7.38dB, auditorium-spruce-mono_mic-pick-v120-m41-s1, late.
- Largest upper-note H1 gain: +1.58dB, dreadnought-mahogany-mono_mic-pick-v120-m53-s3, late.
- Largest upper-note balance increase: +7.71dB, jumbo-spruce-mono_mic-thumb-v56-m59-s5, attack.
- Largest H2–H4 power loss: -16.86dB, H3, jumbo-spruce-mono_mic-pick-v120-m59-s5, late (baseline -83.38dBFS, candidate -100.24dBFS).
- Lowest stereo fold-down H1 retention relative to stereo channel-average power: -5.63dB, auditorium-mahogany-stereo_mic-pick-v120-m40-s1, late.
- Worst change of fold-down H1 retention versus baseline: -0.002dB, jumbo-mahogany-stereo_mic-pick-v120-m53-s3, early.

| Window | Worst lower-note H1 change | Baseline H1 | Candidate H1 | Case |
|---|---:|---:|---:|---|
| attack | -2.04dB | -34.44dBFS | -36.48dBFS | jumbo-mahogany-mono_mic-pick-v120-m42-s1 |
| early | -2.10dB | -37.15dBFS | -39.25dBFS | jumbo-mahogany-mono_mic-pick-v120-m42-s1 |
| held | -3.74dB | -75.36dBFS | -79.10dBFS | auditorium-spruce-mono_mic-pick-v120-m40-s1 |
| late | -7.38dB | -88.36dBFS | -95.74dBFS | auditorium-spruce-mono_mic-pick-v120-m41-s1 |

The largest upper-note balance increase is jumbo-spruce-mono_mic-thumb-v56-m59-s5, attack: H1 changes +0.72dB, while H2 changes -7.25dB (-64.58 to -71.82dBFS). Thus the roughly +8dB balance change mainly reflects H2 weakening.

## Limits

The holdout tests new excitation strengths/techniques and some new string routes, while sharing notes and the same modeled instrument with selection. It is not an independent studio recording or a generalization test of measured body radiation. No listening acceptance is claimed. There is no Maple render or continuous force/moment sweep. Therefore this cannot dismiss the separately identified Maple220.63Hz FRF notch. Parlor/Mahogany low-E H3 and B1 H2 appear in these native clips, but partial-band integration can hide a narrower notch or a different force/moment trajectory. H1 balance, absolute harmonic power, attack and later windows remain separate conclusions.

Run sequence (from repository root; output directories must be fresh):

```sh
OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 build-audit/studio-mic-combinations-20261011/holdout/run_holdout.py
python3 build-audit/studio-mic-combinations-20261011/holdout/summarize_holdout.py
```
