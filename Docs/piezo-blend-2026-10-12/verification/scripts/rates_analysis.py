import json, sys
from pathlib import Path
import numpy as np
OUT = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology")
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(OUT / "scripts"))
import analyse as A  # noqa
d = json.loads((OUT / "stats" / "rates.json").read_text())
CONS = ("parlor-spruce", "jumbo-spruce", "dreadnought-spruce", "auditorium-mahogany", "parlor-maple", "jumbo-maple")
VAR = {"V0": (0, 0), "V1": (1, 0), "V2": (-1, 0), "V3c": (1, 0), "V3d": (-1, 0)}
names = [ev.note_name(n) for n in range(40, 85)]
A.LAGS = [0, -36, 30]
for rate in (48000, 44100, 96000):
    tab = {v: [] for v in VAR}; nm = {}
    for c in CONS:
        if rate == 48000:
            st = A.stats(*A.sweep_paths(c, "mic_r05"))
            lv = {v: A.levels(st, *A.VARIANTS[v][:1], 0.2, A.VARIANTS[v][1]) for v in VAR}
        else:
            recs = [d[f"{rate}|{c}|{n}"] for n in range(40, 85)]
            lv = {}
            for v, (s, _) in {"V0": (0, 0), "V1": (1, 0), "V2": (-1, 0), "V3c": (1, 0), "V3d": (-1, 0)}.items():
                if s == 0:
                    lv[v] = np.array([10 * np.log10(r["mm"]) for r in recs])
                else:
                    lv[v] = np.array([10 * np.log10(r["mm"] + 2 * s * 0.2 * r[v][0] + 0.04 * r[v][1]) for r in recs])
            ks = {v: recs[0][v][2] for v in ("V3c", "V3d")}
        for v in VAR:
            dv = ev.neighbourhood_deviation(lv[v]); tab[v].append(float(np.sqrt(np.mean(dv ** 2))))
            if c == "parlor-spruce": nm[v] = dv[names.index("F#5")]
    x = np.array(tab["V3d"]) - np.array(tab["V1"])
    print(f"{rate} Hz{(' (V3c lag %d, V3d lag %d samples)' % (ks['V3c'], ks['V3d'])) if rate != 48000 else ''}: " + " ".join(f"{v} {np.mean(tab[v]):.3f}" for v in VAR)
          + f" | V3d-V1 b/w {(x < -0.05).sum()}/{(x > 0.05).sum()} worst {x.max():+.2f}"
          + " | per cons V1/V3d " + " ".join(f"{c} {tab['V1'][i]:.2f}/{tab['V3d'][i]:.2f}" for i, c in enumerate(CONS))
          + " | ParlorSpruce F#5 " + " ".join(f"{v} {nm[v]:+.1f}" for v in VAR))
