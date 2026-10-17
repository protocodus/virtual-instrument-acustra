#!/usr/bin/env python3
"""Check Main(piezoMix=0.2) == Main(0) + 0.2 * Main(capture=piezo, piezoMix=0).

Every render in sweeps/, phrases/ and picks/ is compared, per channel, sample
by sample. Writes superposition.json and prints a summary.
"""
import json
from pathlib import Path

import numpy as np

STUDY = Path(__file__).resolve().parent.parent
KNEE = 0.89125094
MIX = np.float32(0.2)


def load(path):
    return np.fromfile(path, dtype="<f4").reshape(-1, 2)


def triples():
    for directory in sorted((STUDY / "sweeps").iterdir()):
        if not directory.is_dir():
            continue
        for note in range(40, 85):
            yield ("sweep", directory.name, f"note-{note}",
                   *(directory / v / f"note-{note}.f32" for v in ("mic", "piezo", "default")))
    for folder, kind in (("phrases", "phrase"), ("picks", "pick")):
        for mic in sorted((STUDY / folder).glob("*-mic.f32")):
            stem = mic.name[:-len("-mic.f32")]
            yield (kind, stem.split("-")[0] + "-" + stem.split("-")[1], stem,
                   mic, mic.with_name(stem + "-piezo.f32"), mic.with_name(stem + "-default.f32"))


def main():
    rows = []
    for kind, construction, label, mic_path, piezo_path, default_path in triples():
        mic, piezo, default = load(mic_path), load(piezo_path), load(default_path)
        assert mic.shape == piezo.shape == default.shape
        predicted64 = mic.astype(np.float64) + 0.2 * piezo.astype(np.float64)
        predicted32 = mic + MIX * piezo  # float32 arithmetic, as the engine adds
        diff64 = np.abs(default.astype(np.float64) - predicted64)
        diff32 = np.abs(default.astype(np.float64) - predicted32.astype(np.float64))
        rows.append({
            "kind": kind, "construction": construction, "label": label,
            "piezo_channels_identical": bool(np.array_equal(piezo[:, 0], piezo[:, 1])),
            "max_abs_diff_float64": float(diff64.max()),
            "max_abs_diff_float32_sum": float(diff32.max()),
            "peak_default": float(np.abs(default).max()),
            "peak_piezo_term": float(0.2 * np.abs(piezo).max()),
            "relative_max_diff": float(diff64.max() / max(np.abs(default).max(), 1e-30)),
            "over_limiter_knee": bool(np.abs(default).max() > KNEE),
            "samples": int(mic.shape[0]),
        })
    (STUDY / "superposition.json").write_text(json.dumps(rows, indent=1) + "\n")
    for kind in ("sweep", "phrase", "pick"):
        subset = [r for r in rows if r["kind"] == kind]
        worst = max(subset, key=lambda r: r["max_abs_diff_float64"])
        print(f"{kind}: {len(subset)} renders, max |diff| float64 sum "
              f"{max(r['max_abs_diff_float64'] for r in subset):.3e} "
              f"(worst {worst['construction']} {worst['label']}, its peak "
              f"{worst['peak_default']:.4f}), max relative to own peak "
              f"{max(r['relative_max_diff'] for r in subset):.3e}; "
              f"max |diff| float32 sum {max(r['max_abs_diff_float32_sum'] for r in subset):.3e}; "
              f"piezo L==R in all: {all(r['piezo_channels_identical'] for r in subset)}; "
              f"any over knee: {any(r['over_limiter_knee'] for r in subset)}; "
              f"max peak {max(r['peak_default'] for r in subset):.4f}")


if __name__ == "__main__":
    main()
