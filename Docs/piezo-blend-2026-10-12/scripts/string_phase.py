#!/usr/bin/env python3
"""M/P at each note's fundamental, by string: the same pitch on different strings.

From the per-string renders (strings/), sustain window, as transfer.py. For the
pitches within a fifth either side of each construction's hole, prints
|M|/|0.2P| (dB) and the phase of M/P (deg) for every string that reaches the
pitch, and the level change of the whole note with +0.2P and -0.2P.
Writes string_phase.txt.
"""
import sys
from pathlib import Path

import numpy as np

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402
sys.path.insert(0, str(STUDY / "scripts"))
from evenness_variants import HOLES, SHAPES, WOODS, load  # noqa: E402
from transfer import partials  # noqa: E402

S0, S1 = round(ev.SUSTAIN[0] * ev.RATE), round(ev.SUSTAIN[1] * ev.RATE)
lines = []
p = lines.append
p("Fundamental M/P by string: |M|/|0.2P| dB, phase deg; [note level change +0.2P / -0.2P, dB]")
for shape in SHAPES:
    for wood in WOODS:
        key = f"{shape}-{wood}"
        hole = HOLES[(shape, wood)]
        centre = int(round(69 + 12 * np.log2(hole / 440)))
        p(f"\n{key} (hole {hole} Hz, nearest {ev.note_name(centre)})")
        p(f"  {'pitch':>5} " + " ".join(f"{'string ' + str(s):>30}" for s in range(1, 7)))
        for midi in range(centre - 4, centre + 5):
            cells = []
            for string, open_note in enumerate(ev.OPEN_NOTES, start=1):
                fret = midi - open_note
                if not 0 <= fret <= 20:
                    cells.append(f"{'-':>30}")
                    continue
                mic = load(STUDY / "strings" / key / "mic" / f"s{string}-{midi}.f32").mean(axis=1)
                pz = load(STUDY / "strings" / key / "piezo" / f"s{string}-{midi}.f32")[:, 0]
                row = [r for r in partials(mic, pz, midi) if r["k"] == 1]
                l0 = ev.db(np.mean(mic[S0:S1] ** 2))
                lp = ev.db(np.mean((mic + 0.2 * pz)[S0:S1] ** 2)) - l0
                lm = ev.db(np.mean((mic - 0.2 * pz)[S0:S1] ** 2)) - l0
                if row:
                    r = row[0]
                    cells.append(f"{r['mic_over_02piezo_db']:+5.1f} {r['phase_deg']:+6.0f} [{lp:+4.1f}/{lm:+4.1f}]".rjust(30))
                else:
                    cells.append(f"{'(no k1)':>18} [{lp:+4.1f}/{lm:+4.1f}]".rjust(30))
            p(f"  {ev.note_name(midi):>5} " + " ".join(cells))
(STUDY / "string_phase.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
