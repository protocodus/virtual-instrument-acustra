import json
from pathlib import Path
import numpy as np
OUT = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology")
d = json.loads((OUT / "stats" / "strums.json").read_text())
CONS = [f"{a}-{b}" for a in ("parlor", "auditorium", "dreadnought", "jumbo") for b in ("spruce", "mahogany", "maple")]
NOM = [80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000]
for cond in ("mic_r05", "mic_r0", "mic_r1", "mono_r05", "mono_r0"):
    cs = [c for c in CONS if f"{c}|{cond}" in d]
    print(f"== {cond} ({len(cs)} constructions)")
    for v in ("V1", "V2", "V3c", "V3d"):
        lu = np.array([d[f"{c}|{cond}"][v]["lufs"] - d[f"{c}|{cond}"]["V0"]["lufs"] for c in cs])
        print(f"  LUFS {v}-V0: {lu.min():+.2f}..{lu.max():+.2f} (spread {lu.max()-lu.min():.2f}); "
              + (f"{v}-V1: " + f"{(lu - np.array([d[f'{c}|{cond}']['V1']['lufs'] - d[f'{c}|{cond}']['V0']['lufs'] for c in cs])).min():+.2f}..{(lu - np.array([d[f'{c}|{cond}']['V1']['lufs'] - d[f'{c}|{cond}']['V0']['lufs'] for c in cs])).max():+.2f}" if v != "V1" else ""))
    # ripple: 1/12-oct difference vs V0 minus 1-octave running mean, std
    rip = {}
    for v in ("V1", "V2", "V3c", "V3d"):
        r = []
        for c in cs:
            x = np.array(d[f"{c}|{cond}"][v]["b12"]) - np.array(d[f"{c}|{cond}"]["V0"]["b12"])
            t = np.convolve(x, np.ones(13) / 13, mode="same")
            r.append(np.std((x - t)[6:-6]))
        rip[v] = (np.mean(r), np.max(r))
    print("  comb ripple vs V0 (dB std, mean/max): " + "  ".join(f"{v} {a:.2f}/{b:.2f}" for v, (a, b) in rip.items()))
    # 1/3-octave abs deviation from V0, 80 Hz-8 kHz: mean |dB|
    for v in ("V1", "V2", "V3c", "V3d"):
        md = [np.mean(np.abs(np.array(d[f"{c}|{cond}"][v]["b3"]) - np.array(d[f"{c}|{cond}"]["V0"]["b3"]))) for c in cs]
        mx = [np.max(np.abs(np.array(d[f"{c}|{cond}"][v]["b3"]) - np.array(d[f"{c}|{cond}"]["V0"]["b3"]))) for c in cs]
        print(f"  third-octave |{v}-V0|: mean {np.mean(md):.2f} dB, largest band {np.max(mx):.1f} dB")
    for v in ("V2", "V3c", "V3d"):
        rows = []
        for i, shape in enumerate(("parlor", "auditorium", "dreadnought", "jumbo")):
            cc = [c for c in cs if c.startswith(shape)]
            diff = np.mean([np.array(d[f"{c}|{cond}"][v]["b3"]) - np.array(d[f"{c}|{cond}"]["V1"]["b3"]) for c in cc], axis=0)
            rows.append(",".join(f"{diff[NOM.index(b)]:+.1f}" for b in (80, 100, 125, 160, 250, 500, 630)))
        print(f"  {v}-V1 third-octave @80,100,125,160,250,500,630 Hz  P: {rows[0]} | A: {rows[1]} | D: {rows[2]} | J: {rows[3]}")
