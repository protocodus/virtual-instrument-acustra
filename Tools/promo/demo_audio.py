"""Acustra trio demos: each of trios.py's pieces whole, mixed from its three
players' renders and mastered as promo_audio.py masters the promo.

Run in the work directory holding out/<piece>/<part>.wav from SongRender. The
mix is level and place only (promo_audio's `placed`, each part's `level` on
integrated loudness against the first part); the piece ends 0.9 s faded after
its last note has rung TAIL seconds. Writes out/<piece>/<piece>.wav and
out/demos-report.json.

    python3 demo_audio.py <dowland-dir>
"""
import json
import pathlib
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from promo_audio import FADE_OUT, master, meter, notes_of, placed, sr  # noqa: E402
from trios import pieces  # noqa: E402

TAIL = 4.0

report = {}
for name, build in pieces(sys.argv[1]).items():
    parts = build()
    stems = [sf.read(f"out/{name}/{part.name}.wav")[0] for part in parts]
    end = min(min(len(x) for x in stems), int((max(n[1] for part in parts for n in notes_of(part.p)) + TAIL) * sr))
    mix, mixed = np.zeros((end, 2)), []
    for part, x in zip(parts, stems):
        stem = placed(x[:end], part.pan)
        trim = -20 + part.level - meter.integrated_loudness(stem)
        mix += stem * 10 ** (trim / 20)
        mixed.append(dict(part=part.name, patch=part.patch, pan=part.pan, level_db=part.level, trim_db=round(float(trim), 2)))
    fade = int(FADE_OUT * sr)
    mix[-fade:] *= (np.linspace(1, 0, fade) ** 2)[:, None]
    report[name] = dict(parts=mixed, **master(mix))
    report[name]["left_minus_right_db"] = round(float(10 * np.log10((mix[:, 0] ** 2).mean() / (mix[:, 1] ** 2).mean())), 2)
    sf.write(f"out/{name}/{name}.wav", mix, sr, subtype="PCM_24")
json.dump(report, open("out/demos-report.json", "w"), indent=1)
print(json.dumps(report, indent=1))
