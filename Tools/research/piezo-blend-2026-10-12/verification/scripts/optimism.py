#!/usr/bin/env python3
"""How independent are the study's 'holdouts'? (Room 0.5 stereo, study renders.)
1) Overlap: the E2-C6 sweep's 45 (string, note) renders are bit-identical members of the 126-note per-string set.
2) Per-string RMS dev restricted to the 81 notes NOT in the sweep.
3) Selection optimism: choose the common (polarity, lag) on the sweep from the study's 34-setting grid, on a
   random half of the constructions; evaluate on the other half (sweep and per-string), 200 splits."""
import sys
from pathlib import Path
import numpy as np
OUT = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology")
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(OUT / "scripts"))
import analyse as A  # noqa
GRID = [(s, int(round((-1.0 + 0.125 * i) * 48))) for s in (1, -1) for i in range(17)]
A.LAGS = sorted({k for _, k in GRID})
sweep_set = {(ev.lowest_fret_string(m), m) for m in range(40, 85)}
SW = {c: A.stats(*A.sweep_paths(c, "mic_r05")) for c in A.CONS}
ST = {c: [A.stats(*A.string_paths(c, "mic_r05", s)) for s in range(1, 7)] for c in A.CONS}
keep = [np.array([(s, ev.OPEN_NOTES[s - 1] + f) not in sweep_set for f in range(21)]) for s in range(1, 7)]
print(f"per-string set 126 notes; {sum((~k).sum() for k in keep)} of them are the sweep's own renders")
var = dict(A.VARIANTS)
res = {v: [] for v in var}
for c in A.CONS:
    for v, (s, k) in var.items():
        d = np.concatenate([ev.neighbourhood_deviation(A.levels(ST[c][i], s, 0.2, k))[keep[i]] for i in range(6)])
        res[v].append(float(np.sqrt(np.mean(d ** 2))))
print("non-sweep per-string notes: " + " ".join(f"{v} {np.mean(res[v]):.3f}" for v in var))
for v in ("V2", "V3c", "V3d"):
    x = np.array(res[v]) - np.array(res["V1"]); print(f"  {v}-V1 {(x < -0.05).sum()}/{(x > 0.05).sum()} worst {x.max():+.2f}")
sw = {(c, g): A.rmsdev([A.levels(SW[c], *g[:1], 0.2, g[1])])[0] for c in A.CONS for g in GRID}
st = {(c, g): A.rmsdev([A.levels(x, g[0], 0.2, g[1]) for x in ST[c]])[0] for c in A.CONS for g in GRID}
rng = np.random.default_rng(1)
gain_dev, gain_test, gain_st, chosen = [], [], [], {}
for _ in range(200):
    idx = rng.permutation(12); tr = [A.CONS[i] for i in idx[:6]]; te = [A.CONS[i] for i in idx[6:]]
    best = min(GRID, key=lambda g: np.mean([sw[(c, g)] for c in tr]))
    chosen[best] = chosen.get(best, 0) + 1
    v1 = (1, 0)
    gain_dev.append(np.mean([sw[(c, v1)] - sw[(c, best)] for c in tr]))
    gain_test.append(np.mean([sw[(c, v1)] - sw[(c, best)] for c in te]))
    gain_st.append(np.mean([st[(c, v1)] - st[(c, best)] for c in te]))
print(f"common setting chosen on 6 constructions' sweeps: improvement vs V1 on those 6 {np.mean(gain_dev):.3f} dB; "
      f"on the other 6 sweeps {np.mean(gain_test):.3f} dB (5th pct {np.percentile(gain_test,5):+.3f}); on the other 6 per-string {np.mean(gain_st):.3f} dB (5th pct {np.percentile(gain_st,5):+.3f})")
print("settings chosen:", {f"{'+' if g[0]>0 else '-'}{g[1]/48:+.3f}ms": n for g, n in sorted(chosen.items(), key=lambda x: -x[1])})
own = [min(GRID, key=lambda g: sw[(c, g)]) for c in A.CONS]
print("per-construction best V3 on sweep -> per-string: mean sweep gain vs V1 %.3f, per-string gain vs V1 %.3f" % (
    np.mean([sw[(c, (1, 0))] - sw[(c, g)] for c, g in zip(A.CONS, own)]), np.mean([st[(c, (1, 0))] - st[(c, g)] for c, g in zip(A.CONS, own)])))
