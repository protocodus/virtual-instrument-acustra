import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, "/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology/scripts")
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import analyse as A, MeasureNoteEvenness as ev  # noqa
names = [ev.note_name(n) for n in range(40, 85)]
sw = {c: A.stats(*A.sweep_paths(c, "mic_r05")) for c in A.CONS}
stg = {c: [A.stats(*A.string_paths(c, "mic_r05", s)) for s in range(1, 7)] for c in A.CONS}
for label, data, grp in (("sweep", sw, False), ("strings", stg, True)):
    v0 = np.mean([A.rmsdev(([A.levels(st, 0, 0, 0) for st in data[c]] if grp else [A.levels(data[c], 0, 0, 0)]))[0] for c in A.CONS])
    print(f"[{label}] V0 {v0:.3f}")
    for mix in (0.1, 0.2, 0.35, 0.5):
        r = {}
        for v, (s, k) in A.VARIANTS.items():
            if v == "V0": continue
            r[v] = np.array([A.rmsdev(([A.levels(st, s, mix, k) for st in data[c]] if grp else [A.levels(data[c], s, mix, k)]))[0] for c in A.CONS])
        line = f"  m={mix}: " + " ".join(f"{v} {r[v].mean():.3f}" for v in r)
        for v in ("V2", "V3c", "V3d"):
            d = r[v] - r["V1"]
            line += f" | {v}-V1 {(d < -0.05).sum()}/{(d > 0.05).sum()} worst {d.max():+.2f} {A.CONS[int(d.argmax())]}"
        print(line)
        if not grp:
            for c, n in (("parlor-spruce", "F#5"), ("jumbo-spruce", "C5")):
                devs = {v: ev.neighbourhood_deviation(A.levels(sw[c], *A.VARIANTS[v][:1], mix, A.VARIANTS[v][1]))[names.index(n)] for v in ("V1", "V2", "V3c", "V3d")}
                print(f"     {c} {n}: " + " ".join(f"{v} {devs[v]:+.1f}" for v in devs))
            holes = {v: min(ev.neighbourhood_deviation(A.levels(sw[c], A.VARIANTS[v][0], mix, A.VARIANTS[v][1])).min() for c in A.CONS) for v in ("V1", "V2", "V3c", "V3d")}
            print("     deepest hole over 12: " + " ".join(f"{v} {holes[v]:+.1f}" for v in holes))
