# Picking style separation — 8 September 2026

The [machine-readable record](picking-styles-2026-09-08.json) retains frozen
source/binary hashes, all 144 before/after Finger audio hashes, fixed controls
and native qualification results.

The former Pick and Thumb controls remapped Touch into its upper/lower halves.
On steel, maximum velocity saturated both Finger and Pick at Touch=1, producing
identical audio for all 18 tested pitches. The distinction was also small at
other velocities. Changing width alone removed the exact identity but produced
only 0.106 dB median normalized attack contrast at velocity 127 on steel.

Picking now selects a contact footprint and an authored hand position. Touch,
velocity, displacement amplitude and the existing release-noise law are shared;
no technique gain, new click or output EQ is added.

| Style | Distance from bridge, relative to panel base | Contact standard deviation, relative to Finger |
| --- | ---: | ---: |
| Finger | 1 | 1 |
| Pick | 0.55 | 0.5 |
| Thumb | 1.60 | 2 |

Finger retains its calibrated waveform. Position jitter and finite-domain
clamps remain in place. Explicit MPE pluck position takes precedence over the
style's distance ratio. Technique changes affect the next actual pluck; ringing
notes and fretting-hand hammer/lift gestures retain their existing state.

These are **authored playing styles**, not universal tool laws or measured
contact dimensions. A real player can use a plectrum near the neck or a thumb
near the bridge. The panel remains the base position for exploring that range.
The mechanism follows the mode-selecting effect of position and finite contact
described by [Traube and Smith, DAFx00](https://www.dafx.de/paper-archive/2000/pdf/Caroline_Traube.pdf),
including its cautions about contact interactions and body filtering. The
[Duke finite-width pluck derivation](https://webhome.phy.duke.edu/~dtl/136126/136m3_gui.html)
also distinguishes contact width from observation position. Neither source
identifies the three numerical ratios above.

## Recorded contrast

The source is Mohammed Alkooheji's
[Acoustic Guitar Notes v3](https://www.kaggle.com/datasets/mohammedalkooheji/guitar-notes-dataset),
archive SHA-256 `4563a70d5dd9a14919171d103f10f6731fd78e63f1498dd11047bfea471f6a6d`.
It labels a Walden G551E steel guitar and Yamaha CM-40 nylon guitar. Capture,
velocity, fingering and pluck position are undocumented; its `f` label merges
finger and thumb. A separate Thumb profile cannot be validated from this data.

Selection is filename-only: the first two numeric ordinals of normal pick and
merged finger/thumb at MIDI 40,42,45,47,50,52,55,57,59,62,64,67,69,72,74,77,79,80.
Average the two takes for each pitch/label before computing a signed contrast.
The existing onset detector and normalized 18-band attack descriptors cover
0–12, 12–40 and 40–100 ms. No recording level is interpreted as MIDI velocity.

The median pick-minus-merged-finger spectral tilt is +2.37/+2.74/+2.90 dB/octave
for steel and +1.92/+1.94/+1.79 for nylon in the three windows. Both later
windows are positive at all 18 pitches. The earliest window has some reversals;
within-label repeat attack differences are around 4–5 dB. This supports a
brighter Pick direction, with substantial performance/capture uncertainty.

## Fixed before/after comparison

The A/B uses frozen Gaussian/nylon-calibrated source, Original bridge,
Auditorium, steel/Mahogany or nylon/Spruce, Stereo mic, the same 18 pitches,
and MIDI velocities 32,64,91,127. Every render lasts 200 ms and begins at frame
zero. Level normalization belongs only to analysis; audio is not modified.

At velocity 127, median absolute differences between normalized attack bands:

| Pair | Steel before | Steel after | Nylon before | Nylon after |
| --- | ---: | ---: | ---: | ---: |
| Finger / Pick | 0.000 dB | 3.678 dB | 0.548 dB | 3.658 dB |
| Finger / Thumb | 0.939 dB | 3.392 dB | 0.709 dB | 3.252 dB |
| Pick / Thumb | 0.939 dB | 5.430 dB | 1.293 dB | 6.303 dB |

The smallest after-change pair contrast across those pitches at velocity 127
is 2.015 dB. At velocities 32/64/91 the median Finger/Pick contrast is
3.669/3.779/3.734 dB on steel and 3.611/3.614/3.620 on nylon. All **144 Finger
renders remain byte-identical** between the frozen before/after binaries.

At velocity 127, mean absolute error against the recorded pick-minus-merged-
finger contrast moves from 9.024 to 8.136 dB on steel and 4.853 to 4.147 on
nylon. Source velocity is unknown: this is exploratory descriptor evidence,
not a velocity-matched fit, blind listening result or proof of realism.

Reproduce with `Tools/ComparePickingAttacks.py`, passing the frozen source ZIP,
one or more `--renderer LABEL=PATH` arguments and a new `--output` directory.
It saves the filename selection, source/binary/audio hashes, every signed band
contrast, per-velocity summary and raw float renders. Audio remains outside
the repository. The native engine suite independently checks all pairwise
attacks after removing best-fit scalar gain at four velocities, four registers,
three sample rates and both materials, alongside preserved ringing/fretting
behavior and the shared amplitude/noise law.
