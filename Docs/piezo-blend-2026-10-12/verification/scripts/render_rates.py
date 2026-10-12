import subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"; R = SP / "piezo-study" / "bin" / "AcustraPerformanceRenderer"
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(OUT / "scripts"))
from render import note_score  # noqa
jobs = []
for rate in (44100, 96000):
    for cons in ("parlor-spruce", "jumbo-spruce", "dreadnought-spruce", "auditorium-mahogany", "parlor-maple", "jumbo-maple"):
        sh, w = cons.split("-")
        for midi in range(40, 85):
            s = ev.lowest_fret_string(midi)
            p = OUT / "scores" / f"r{rate}-s{s}-{midi}.events"
            if not p.exists(): note_score(p, s, midi, rate=rate)
            for name, cap in (("mic", "stereo_mic"), ("piezo", "piezo")):
                o = OUT / "rates" / str(rate) / cons / name / f"note-{midi}.f32"
                o.parent.mkdir(parents=True, exist_ok=True)
                if not o.exists():
                    jobs.append([str(R), str(p), str(o), cap, "finger", "--body-shape", sh, "--body-material", w, "--room", "0.5", "--piezo-mix", "0"])
with ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(lambda c: subprocess.run(c, check=True, capture_output=True), jobs))
print(len(jobs))
