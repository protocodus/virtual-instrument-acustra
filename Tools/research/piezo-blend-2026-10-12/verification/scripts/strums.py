import sys, subprocess, json
from pathlib import Path
import numpy as np
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureMaterialLoudness as mml
sys.path.insert(0, str(Path(__file__).parent))
from render import RENDERER
from analyse import shift
W = Path(sys.argv[1]); W.mkdir(exist_ok=True)
rows, seconds = mml.strums()
mml.write_performance(rows, seconds, W / "strums.txt")
hard = [(s, n, on, off, 127) for s, n, on, off, v in rows]
mml.write_performance(hard, seconds, W / "strums127.txt")
res = {}
for perf in ("strums", "strums127"):
    for c in ("parlor-spruce", "auditorium-maple", "dreadnought-spruce", "dreadnought-mahogany", "jumbo-spruce", "jumbo-maple"):
        shape, wood = c.split("-")
        common = ["finger", "--body-shape", shape, "--body-material", wood, "--room", "0.5"]
        a = {}
        for v, opts in (("mic", ["stereo_mic", *common, "--piezo-mix", "0"]), ("piezo", ["piezo", *common]),
                        ("default", ["stereo_mic", *common, "--piezo-mix", "0.2"])):
            out = W / f"{perf}-{c}-{v}.f32"
            if out.exists(): out.unlink()
            subprocess.run([RENDERER, str(W / f"{perf}.txt"), str(out), *opts], check=True)
            a[v] = np.fromfile(out, dtype="<f4").reshape(-1, 2); out.unlink()
        mic, pie, dft = a["mic"], a["piezo"], a["default"]
        pred = mic.astype(np.float64) + 0.2 * pie.astype(np.float64)
        d = float(np.abs(dft - pred).max()); pk = float(np.abs(dft).max())
        m64, p64 = mic.astype(np.float64), pie.astype(np.float64)
        lufs = {"V0": mml.integrated_loudness(m64), "V1": mml.integrated_loudness(dft.astype(np.float64)),
                "V2": mml.integrated_loudness(m64 - 0.2 * p64),
                "V3c": mml.integrated_loudness(m64 + 0.2 * np.stack([shift(p64[:, 0], -36)] * 2, axis=1)),
                "V3d": mml.integrated_loudness(m64 - 0.2 * np.stack([shift(p64[:, 0], 30)] * 2, axis=1))}
        res[f"{perf}/{c}"] = {"max_diff": d, "rel": d / pk, "peak": pk, "peak_mic": float(np.abs(mic).max()),
                              "lr": bool(np.array_equal(pie[:, 0], pie[:, 1])), "lufs": lufs}
        print(perf, c, f"diff {d:.2e} rel {d/pk:.2e} peak V1 {pk:.3f} lr {res[f'{perf}/{c}']['lr']}",
              " ".join(f"{k}-V1 {v - lufs['V1']:+.2f}" for k, v in lufs.items() if k != "V1"), flush=True)
(W / "strums.json").write_text(json.dumps(res, indent=1))
