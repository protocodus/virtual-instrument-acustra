#!/usr/bin/env python3
"""Compact per-construction table from evenness.json and extra.json: RMS
deviation, deepest hole, highest peak and the hole-nearest note's dev for
V0, V1, V2, the construction's best V3 grid setting, and the two common
settings V3c (+, piezo 0.75 ms earlier) and V3d (-, piezo 0.625 ms later);
plus V3d's strum third-octave table. Writes summary_table.txt."""
import json
from pathlib import Path

import numpy as np

STUDY = Path(__file__).resolve().parent.parent
ev = json.loads((STUDY / "evenness.json").read_text())["constructions"]
xt = json.loads((STUDY / "extra.json").read_text())
NOMINAL = [80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000]


def label(l):
    sign = "+" if l[0] == "+" else "-"
    ms = float(l[1:-2])
    return f"{sign}pol {abs(ms):.3f} ms {'earlier' if ms < 0 else 'later' if ms > 0 else ''}".strip()


lines = []
p = lines.append
p("E2-C6, Finger v96, stereo mic, Room 0.5: RMS dev | deepest hole | highest peak | dev at the note nearest the located hole (*)")
for key, r in ev.items():
    hn = r["hole_note"]
    p(f"\n{key} (hole {r['hole_hz']} Hz -> {hn}); best V3 = {label(r['V3_best_label'])}")
    rows = [("V0", r["V0"]), ("V1", r["V1"]), ("V2", r["V2"]), ("V3best", r["V3_best"])]
    for name, v in rows:
        p(f"  {name:7} {v['rms_deviation_db']:5.2f} | {v['deepest_hole_note']:>4} {v['deepest_hole_db']:+6.1f} | {v['highest_peak_note']:>4} {v['highest_peak_db']:+5.1f} | {hn}* {v['dev'][hn]:+6.1f}")
    for name in ("V3c", "V3d"):
        v = xt[key]["sweep"][name]
        p(f"  {name:7} {v['rms']:5.2f} | {v['hole']:>11} | {v['peak']:>10} | {hn}* {v['dev'][hn]:+6.1f}")
p("\nV3d (-, piezo 0.625 ms later) strums, third-octave minus V0 (dB):")
p(f"  {'':22} " + " ".join(f"{n:>5}" for n in NOMINAL))
for key in xt:
    d = np.array(xt[key]["strum"]["V3d"]["bands"]) - np.array(xt[key]["strum"]["V0"]["bands"])
    p(f"  {key:22} " + " ".join(f"{x:+5.1f}" for x in d))
(STUDY / "summary_table.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
