#!/usr/bin/env python3
"""Synthetic known-mode oracles for the fretboard measurement protocol."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools"))
from MeasureFretboardSweep import RATE, inharmonicity, band_t60


def signal(fundamental, stiffness, missing_sixth=False):
    time = np.arange(RATE) / RATE
    result = np.zeros(len(time))
    for n in range(1, 9):
        amplitude = 1 / n
        if n == 6:
            amplitude = 0 if missing_sixth else 0.01
        if n == 7:
            amplitude = 1.0  # A hundred times the sixth: identity must win.
        frequency = n * fundamental * np.sqrt(1 + stiffness * n * n)
        result += amplitude * np.sin(2 * np.pi * frequency * time + 0.13 * n)
    return result * np.exp(-time / 3)


for fundamental in (82.406889, 164.813778, 440.0, 1046.502261):
    for stiffness in (0.0, 0.0003, 0.002):
        measured = inharmonicity(signal(fundamental, stiffness), fundamental, 0.1, 0.6)
        assert abs(measured - stiffness) < 3e-6, (fundamental, stiffness, measured)
    missing = inharmonicity(signal(fundamental, 0.0003, True), fundamental, 0.1, 0.6)
    assert np.isnan(missing), (fundamental, "missing H6 relabelled", missing)

ambiguous = inharmonicity(signal(82.406889, 0.03), 82.406889, 0.1, 0.6)
assert np.isnan(ambiguous), ("strongly stretched modes were guessed", ambiguous)
print("Fretboard mode identity, absent partial and ambiguous stiffness checks passed")

# The F2 fundamental band lies strictly between the original 4096-point bins.
# Its known exponential envelope must still produce a usable decay estimate.
time = np.arange(2 * RATE) / RATE
for fundamental in (82.406889, 87.307058, 92.498606, 440.0):
    target = 4.0
    wave = np.sin(2 * np.pi * fundamental * time) * np.exp(-np.log(1000) * time / target)
    measured = band_t60(wave, 0.94 * fundamental, 1.06 * fundamental, 0.1, 1.1)
    assert np.isfinite(measured) and abs(measured / target - 1) < 0.001, (fundamental, measured)
print("Fundamental decay remains resolved across the low-register FFT-bin boundary")
