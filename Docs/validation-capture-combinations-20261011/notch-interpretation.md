# Force/moment notch interpretation

The fixed blends improve the reported spectral-balance metrics, but retain physically relevant cancellation limits. The sweep sets force F=1 and normalized moment M=-1. This means physical torque divided by the 23.2 mm impact half-spacing; it is the measured bass-side hammer input, not an arbitrarily huge torque. The six string lever arms are -1.25, -0.75, -0.25, +0.25, +0.75 and +1.25. The horizontal polarization contributes additional rocking through h/a=8.1/23.2≈0.349. Coupled and sympathetic strings make the actual frequency-domain force/moment ratio complex and variable, so a fixed real ratio is a stress condition rather than a played-note prediction.

Source definitions: Source/DSP/AcustraEngine.cpp saddleLeverArm (line1314), BridgeLoad::process (line3737), saddleHeightRatio (line5382), and radiation drive (line11545); Tools/GenerateBodyForcePair.py defines F=Fb+Ft and M=Ft-Fb in its opening documentation.

Widths below refer to the connected interval around the refined minimum where candidate magnitude divided by baseline magnitude is below -6 or -10 dB. The point depth is relative to baseline at that same frequency. Local-notch levels compare center level with the mean dB level two semitones above and below.

| Blend | Construction / output | Center Hz | Point loss dB | Width below -6 dB | Width below -10 dB | Local notch: baseline → blend dB |
|---|---|---:|---:|---:|---:|---:|
| balanced_all | parlor/spruce stereo_mid | 254.106 | -15.27 | 0.809 Hz | 0.422 Hz | -18.84 → -35.01 |
| balanced_all | parlor/mahogany stereo_mid | 244.691 | -17.54 | 0.799 Hz | 0.440 Hz | -18.78 → -37.27 |
| balanced_all | auditorium/maple stereo_mid | 220.657 | -38.90 | 0.697 Hz | 0.408 Hz | -19.25 → -59.17 |
| measured_continuity | parlor/spruce right_mono | 252.731 | -32.61 | 0.727 Hz | 0.395 Hz | -21.58 → -54.49 |
| measured_continuity | dreadnought/maple stereo_mid | 196.799 | -49.02 | 0.252 Hz | 0.144 Hz | -28.59 → -77.84 |
| measured_continuity | auditorium/maple right_mono | 219.544 | -30.18 | 0.650 Hz | 0.355 Hz | -21.09 → -51.68 |

The finer search resolves deeper minima than the original 7,001-point logarithmic sweep, especially the Maple cases. Both sampled and refined depths are retained in notch-widths.json. Direct complex-pole evaluation independently agrees with the rational response within 2.2e-8 dB at the minima and interval boundaries; these are not numerical artifacts of the transfer formula.

A sub-hertz notch is not necessarily inaudible. A sustained harmonic can be narrower than the notch, and tuning or bending can move a harmonic through it. Conversely, the point loss is not the loss of a complete guitar note: actual force/moment phase, other harmonics and the selected capture all matter. Stereo-mid denotes summing L/R, not the separate MonoMic capture. It also drives the modeled room send.

The played grid and Pick/Thumb holdout cover Spruce and Mahogany, not Maple. The native reports therefore complement, but do not close, these Maple stress cases. Broad H1 or low-band summary flags can miss upper-partial cancellation. The separate native holdout already demonstrates some upper-partial weakening, so the appropriate description is a promising spectral-balance compromise with cancellation limits—not an artifact-free or complete physical repair.

No production source or coefficients were changed by this analysis. Reproduce with:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 build-audit/studio-mic-combinations-20261011/quantify_notches.py
```
