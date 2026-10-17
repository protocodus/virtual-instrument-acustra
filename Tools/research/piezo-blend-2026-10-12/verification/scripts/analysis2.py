#!/usr/bin/env python3
"""All condition analyses from the reduced statistics (stats/*.json + stats/parts)."""
import json, sys
from pathlib import Path
import numpy as np
OUT = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology")
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(OUT / "scripts"))
from reduce import LAGS  # noqa
CONS = [f"{a}-{b}" for a in ("parlor", "auditorium", "dreadnought", "jumbo") for b in ("spruce", "mahogany", "maple")]
VARIANTS = {"V0": (0, 0), "V1": (+1, 0), "V2": (-1, 0), "V3c": (+1, -36), "V3d": (-1, +30)}
LI = {k: i for i, k in enumerate(LAGS)}


def stats(cat):
    d = json.loads((OUT / "stats" / f"{cat}.json").read_text())
    for p in (OUT / "stats" / "parts" / cat).glob("*.json") if (OUT / "stats" / "parts" / cat).exists() else []:
        d.update(json.loads(p.read_text()))
    return d


def level(rec, s, mix, k, ch="mono"):
    if ch == "mono":
        mm, mp = rec["mm"], rec["mp"][LI[k]]
    elif ch == "L":
        mm, mp = rec["LL"], rec["Lp"][LI[k]]
    else:
        mm, mp = rec["RR"], rec["Rp"][LI[k]]
    if s == 0:
        return 10 * np.log10(mm)
    return 10 * np.log10(mm + 2 * s * mix * mp + mix * mix * rec["pp"][LI[k]])


def groups_for(cat, d, cons, cond):
    if cat == "sweeps":
        keys = [[f"{cons}/{cond}/note-{n}.f32" for n in range(40, 85)]]
    elif cat == "strings":
        keys = [[f"{cons}/{cond}/s{s}-{ev.OPEN_NOTES[s-1]+f}.f32" for f in range(21)] for s in range(1, 7)]
    else:
        keys = [[f"{cons}/{cond}/{kind}{f}.f32" for f in range(13)] for kind in ("E", "A")]
    if not all(k in d for g in keys for k in g):
        return None
    return [[d[k] for k in g] for g in keys]


def rmsdev(groups, s, mix, k, ch="mono", power_mean=False):
    devs = []
    for g in groups:
        if power_mean:
            lv = np.array([10 * np.log10(0.5 * 10 ** (level(r, s, mix, k, "L") / 10) + 0.5 * 10 ** (level(r, s, mix, k, "R") / 10)) for r in g])
        else:
            lv = np.array([level(r, s, mix, k, ch) for r in g])
        devs.append(ev.neighbourhood_deviation(lv))
    d = np.concatenate(devs)
    return float(np.sqrt(np.mean(d ** 2))), d


def table(cat, conds, mixes=(0.2,), power_mean=False):
    d = stats(cat)
    lines = []
    for cond in conds:
        G = {c: groups_for(cat, d, c, cond) for c in CONS}
        if any(g is None for g in G.values()):
            lines.append(f"[{cat} {cond}] incomplete: {[c for c, g in G.items() if g is None]}")
            continue
        for mix in mixes:
            r = {v: np.array([rmsdev(G[c], s, mix, k, power_mean=power_mean)[0] for c in CONS]) for v, (s, k) in VARIANTS.items()}
            line = f"[{cat} {cond} m={mix}{' Lpow+Rpow' if power_mean else ''}] " + " ".join(f"{v} {r[v].mean():.3f}" for v in VARIANTS)
            for v in ("V2", "V3c", "V3d"):
                x = r[v] - r["V1"]
                line += f" | {v}-V1 {(x < -0.05).sum()}/{(x > 0.05).sum()} worst {x.max():+.2f} {CONS[int(x.argmax())]}"
            x = r["V3d"] - r["V0"]
            line += f" | V3d-V0 {(x < -0.05).sum()}/{(x > 0.05).sum()}"
            lines.append(line)
            if mix == 0.2:
                lines.append("    per cons V1/V3d: " + " ".join(f"{c.split('-')[0][:3]}{c.split('-')[1][:2]} {r['V1'][i]:.2f}/{r['V3d'][i]:.2f}" for i, c in enumerate(CONS)))
    return lines


def lagscan(cat, cond):
    d = stats(cat)
    G = {c: groups_for(cat, d, c, cond) for c in CONS}
    if any(g is None for g in G.values()):
        return [f"lagscan {cat} {cond} incomplete"]
    res = {(s, k): np.mean([rmsdev(G[c], s, 0.2, k)[0] for c in CONS]) for s in (1, -1) for k in LAGS}
    bp = min(LAGS, key=lambda k: res[(1, k)]); bm = min(LAGS, key=lambda k: res[(-1, k)])
    out = [f"lagscan {cat} {cond}: V1 {res[(1,0)]:.3f} V3d {res[(-1,30)]:.3f} V3c {res[(1,-36)]:.3f}; best +pol {res[(1,bp)]:.3f} at {bp/48:+.3f} ms; best -pol {res[(-1,bm)]:.3f} at {bm/48:+.3f} ms"]
    out.append("   -pol: " + " ".join(f"{k/48:+.3f}:{res[(-1,k)]:.3f}" for k in LAGS if 0 <= k <= 48 and k % 6 == 0))
    return out


if __name__ == "__main__":
    what = sys.argv[1:]
    L = []
    if "sweeps" in what:
        L += table("sweeps", ("mic_r0", "mic_r1", "mono_r05", "mono_r0"), (0.1, 0.2, 0.35, 0.5))
        L += table("sweeps", ("mic_r0", "mic_r1"), (0.2,), power_mean=True)
        for cond in ("mic_r0", "mic_r1", "mono_r05", "mono_r0"):
            L += lagscan("sweeps", cond)
    if "strings" in what:
        L += table("strings", ("mic_r0", "mic_r1", "mono_r05"), (0.1, 0.2, 0.35, 0.5))
        for cond in ("mic_r0", "mic_r1", "mono_r05"):
            L += lagscan("strings", cond)
    if "chords" in what:
        L += table("chords", ("mic_r05", "mic_r0", "mic_r1", "mono_r05"), (0.2, 0.5))
        L += table("chords", ("mic_r05",), (0.2,), power_mean=True)
    print("\n".join(L))
