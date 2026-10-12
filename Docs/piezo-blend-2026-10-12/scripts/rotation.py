#!/usr/bin/env python3
"""A constant 90-degree phase rotation of the piezo term (ideal, non-causal:
the imaginary part of the FFT analytic signal, zero-padded to twice the
length), as the classic mic/DI 'phase rotation' remedy, against V1, V2 and
the power-sum bound PS. R-90 = 0.2 H{P} (cos -> sin), R+90 = -0.2 H{P}.
Same three note sets, the strums and the transients as extra_variants.py.
Writes rotation.txt / rotation.json.
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.signal import hilbert

STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(STUDY / "scripts"))
import extra_variants as xv  # noqa: E402

_original = xv.piezo_term


def rotated(piezo, sign):
    n = len(piezo)
    h = np.imag(hilbert(np.concatenate([piezo, np.zeros(n)])))[:n]
    return sign * 0.2 * h


def piezo_term(piezo, name):
    if name == "R-90":
        return rotated(piezo, +1)
    if name == "R+90":
        return rotated(piezo, -1)
    return _original(piezo, name)


xv.piezo_term = piezo_term
xv.VARIANTS = ["V0", "V1", "V2", "PS", "R-90", "R+90"]


def main():
    res = {}
    for shape in xv.SHAPES:
        for wood in xv.WOODS:
            key = f"{shape}-{wood}"
            res[key] = {"sweep": xv.sweep_set("sweeps", key), "pick": xv.sweep_set("pick-sweeps", key),
                        "strings": xv.strings_set(key), "strum": xv.strum_set(key)}
    V = xv.VARIANTS
    lines = []
    p = lines.append
    for setname in ("sweep", "strings", "pick"):
        p(f"RMS deviation (dB), {setname}")
        p(f"  {'':22} " + " ".join(f"{v:>6}" for v in V))
        for key, r in res.items():
            p(f"  {key:22} " + " ".join(f"{r[setname][v]['rms']:6.2f}" for v in V))
        p(f"  {'mean':22} " + " ".join(f"{np.mean([r[setname][v]['rms'] for r in res.values()]):6.2f}" for v in V))
        better = [sum(r[setname][v]["rms"] < r[setname]["V1"]["rms"] - 0.05 for r in res.values()) for v in V]
        worse = [sum(r[setname][v]["rms"] > r[setname]["V1"]["rms"] + 0.05 for r in res.values()) for v in V]
        p(f"  {'#better/#worse vs V1':22} " + " ".join(f"{b:>3}/{w:<2}" for b, w in zip(better, worse)))
        p("")
    p("Named notes (Finger sweep / Pick sweep dev):")
    for key, note in (("parlor-spruce", "F#5"), ("jumbo-spruce", "C5")):
        p(f"  {key} {note}: " + "  ".join(f"{v} {res[key]['sweep'][v]['dev'][note]:+.1f}/{res[key]['pick'][v]['dev'][note]:+.1f}" for v in V))
    p("")
    p("Deepest hole / highest peak, Finger sweep:")
    for key, r in res.items():
        p(f"  {key:22} " + " | ".join(f"{v}: {r['sweep'][v]['hole']}, {r['sweep'][v]['peak']}" for v in V if v != "PS"))
    p("")
    for v in ("R-90", "R+90"):
        p(f"Strums: third-octave, {v} minus V0 (dB); last column LUFS minus V0:")
        p(f"  {'':22} " + " ".join(f"{n:>5}" for n in xv.NOMINAL))
        for key, r in res.items():
            d = np.array(r["strum"][v]["bands"]) - np.array(r["strum"]["V0"]["bands"])
            p(f"  {key:22} " + " ".join(f"{x:+5.1f}" for x in d) + f"   {r['strum'][v]['lufs'] - r['strum']['V0']['lufs']:+.2f}")
        p("")
    (STUDY / "rotation.json").write_text(json.dumps(res, indent=1) + "\n")
    (STUDY / "rotation.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
