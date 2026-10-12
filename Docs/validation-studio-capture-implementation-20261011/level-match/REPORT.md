# Flat microphone level restoration

The unchanged seeded calibration phrase fits one flat gain for each of 36 construction/picking cells on each of the two microphone routes. It restores each cell to its own preceding d54 loudness; it does not fit individual notes or the independent construction-loudness regression. All legacy reference arrays and the Piezo chain stay unchanged.

Measured 72 paired cells and both versions of the hardest score for all three techniques: 288 renders. All phrase renders stayed below the safety limiter. Generated gains span -0.1448 to +2.7774 dB. Float32 arithmetic predicts phrase agreement within 4.88e-07 LU; final native checks must verify the rebuilt engine.

The hardest-case peak is recovered by the existing exact inverse limiter, then multiplied by the flat factor. The worst predicted final peak is Dreadnought/Mahogany/Pick/Stereo at −2.10003 dBFS. No case exceeds −2 dBFS, so no target was lowered for headroom. These are predictions pending the final native render, not final-engine headroom claims.

| Shape | Stereo gain range | Mono gain range |
|---|---:|---:|
| parlor | +0.864…+1.240 dB | +1.236…+2.250 dB |
| auditorium | -0.003…+0.925 dB | +0.613…+2.777 dB |
| dreadnought | -0.137…+0.912 dB | +0.452…+2.666 dB |
| jumbo | -0.145…+0.869 dB | +0.474…+2.457 dB |

Each table is indexed `(shape * 3 + wood) * 3 + picking`. The room send uses the stereo reference; mono room return inherits the separate mono reference. This is level matching of the approved complex observation, with no change to EQ, modal coefficients, mechanics or spatial weights.

## Reproduction

Run from the repository root, using the ignored frozen snapshots whose hashes are in the paired manifests:

```sh
python3 Tools/CalibrateStudioCaptureLoudness.py --baseline-renderer build-audit/capture-implementation-20261011/ab/before/TunedPerformanceRenderer --blend-renderer build-audit/capture-implementation-20261011/ab/after/TunedPerformanceRenderer --json build-audit/capture-implementation-20261011/level-match/replayed.json --jobs 2
```

To reproduce just the generated header from measured data (writes the production header, no renders):

```sh
python3 Tools/CalibrateStudioCaptureLoudness.py --measurements Docs/validation-studio-capture-implementation-20261011/level-match/report.json --write-header
```

The archived Python dependencies in this folder preserve the measurement protocol. Execute the repository tool rather than the archived copy: the tool locates its output relative to the repository `Tools` directory. WAV/raw files were temporary; reports retain their hashes and complete deterministic scores.

## Final native verification

The final compiled engine (library SHA256 `282ae53ffe4a0f977bb9c11ec4c06858303dd118aa00a03bc8315e021da7ca00`) was rendered for all 72 phrase cells and 72 hardest-score cells. Phrase loudness is restored within **0.015770414 LU** of each preceding cell. The hardest native peak is **−2.100032869 dBFS**, within 0.00000154 dB of prediction, with no limiter-active phrase or hardest-score samples.

The auxiliary 0.001-LU algebraic-precision diagnostic flagged five Mono cells. That original flag remains in `final-verification.json`; it is not relabeled as a pass. The worst-cell witness compares final native audio with an ideal flat multiplication of the raw-blend audio: maximum sample difference 2.45e−8 and relative RMS difference −148.86 dB. BS.1770's fixed absolute gate admits 7 additional quiet blocks (396→403), shifting the relative gate so one additional block enters integration (257→258). Ideal postscaling and final native integrated loudness agree within 3.53e−8 LU. The 0.01577-LU change is therefore a meter-gating boundary effect, not spectral modification or a failure of flat runtime scaling. No gains were refitted. The original product construction-loudness regression passed without changing its tolerance.

`final-assessment.json` lists all five flags. `gate-witness.json` retains the block powers and gate membership. `verify_final.py` reproduces the diagnostic and exits nonzero on the deliberately tighter 0.001-LU assertion; `gate_witness.py` explains its worst case without changing any parameter. The accepted restoration claim is bounded at 0.016 LU, not exact gated-LUFS equality. No more grid rerenders were used to reach that assessment.
