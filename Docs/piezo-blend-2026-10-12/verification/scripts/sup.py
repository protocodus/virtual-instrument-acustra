import numpy as np, itertools
from pathlib import Path
D = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology/sup")
def load(p): return np.fromfile(p, dtype="<f4").reshape(-1, 2)
for cons in ("dreadnought-maple", "jumbo-spruce", "parlor-spruce"):
    P = load(D / f"{cons}-piezo-pick127-r1-m0.f32").astype(np.float32)
    assert np.array_equal(P[:, 0], P[:, 1])
    for cap in ("stereo_mic", "mono_mic"):
        M0 = load(D / f"{cons}-{cap}-pick127-r1-m0.f32").astype(np.float32)
        for m in ("0.2", "0.5"):
            Mm = load(D / f"{cons}-{cap}-pick127-r1-m{m}.f32")
            pred = M0 + np.float32(m) * P
            d = np.abs(Mm.astype(np.float64) - pred.astype(np.float64))
            peak = np.abs(Mm).max()
            n_over = int((np.abs(pred) > 0.891).sum())
            print(f"{cons:20s} {cap:10s} m{m}: peak {peak:.3f} (pred peak {np.abs(pred).max():.3f}, samples >0.891 in prediction {n_over}); max|diff| {d.max():.2e}; at peak region? argmax {np.unravel_index(d.argmax(), d.shape)}")
