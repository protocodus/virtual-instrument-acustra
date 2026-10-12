#!/usr/bin/env python3
"""Microphone-over-piezo transfer, M/P, at every partial of the E2-C6 notes.

For each construction and note, the sustain window (0.08-1.15 s, as the
evenness metric) of the mono microphone sum M (Piezo Mix 0) and of the piezo
term P (Capture Piezo) is Hann-windowed and transformed (2^18 points); at each
partial (peak of |P| within +-2.5 % of k*f0, k*f0 < 5 kHz, at least 40 dB
under the note's strongest piezo partial floor) the complex ratio M/P is the
transfer from the piezo signal to the summed microphone signal: both come from
the same string partial over the same window. |M|/|0.2 P| and the phase of
M/P say whether +0.2 P adds to or cancels the microphones at that frequency.
A second estimate, Welch H1 = S_PM / S_PP with coherence over the 45 notes
concatenated (nperseg 8192), is written beside it.

Writes transfer/<construction>-partials.csv, transfer/<construction>-welch.csv,
and transfer/summary.txt (the hole region of Parlor and Jumbo Spruce in
detail, and the fundamental's M/P around every construction's hole).
"""
import sys
from pathlib import Path

import numpy as np
from scipy.signal import csd, welch, coherence

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402
sys.path.insert(0, str(STUDY / "scripts"))
from evenness_variants import HOLES, SHAPES, WOODS, load  # noqa: E402

RATE = ev.RATE
S0, S1 = round(ev.SUSTAIN[0] * RATE), round(ev.SUSTAIN[1] * RATE)
NFFT = 1 << 18
OUT = STUDY / "transfer"


def partials(mic, piezo, midi):
    f0 = 440.0 * 2 ** ((midi - 69) / 12)
    window = np.hanning(S1 - S0)
    M = np.fft.rfft(mic[S0:S1] * window, NFFT)
    P = np.fft.rfft(piezo[S0:S1] * window, NFFT)
    freqs = np.fft.rfftfreq(NFFT, 1 / RATE)
    scale = 2.0 / window.sum()  # amplitude of a sinusoid
    rows = []
    strongest = None
    k = 1
    while k * f0 < 5000:
        lo, hi = np.searchsorted(freqs, [k * f0 * 0.975, k * f0 * 1.025])
        i = lo + int(np.argmax(np.abs(P[lo:hi])))
        pa = np.abs(P[i]) * scale
        strongest = pa if strongest is None else max(strongest, pa)
        rows.append((k, freqs[i], M[i], P[i], pa))
        k += 1
    out = []
    for k, f, Mi, Pi, pa in rows:
        if pa < strongest * 1e-2:  # 40 dB under the strongest piezo partial
            continue
        H = Mi / Pi
        out.append({"midi": midi, "note": ev.note_name(midi), "k": k, "hz": float(f),
                    "mic_db": float(20 * np.log10(np.abs(Mi) * scale + 1e-30)),
                    "piezo02_db": float(20 * np.log10(0.2 * np.abs(Pi) * scale + 1e-30)),
                    "mic_over_02piezo_db": float(20 * np.log10(np.abs(H) / 0.2)),
                    "phase_deg": float(np.degrees(np.angle(H))),
                    "plus_db": float(20 * np.log10(np.abs(1 + 0.2 / H))),
                    "minus_db": float(20 * np.log10(np.abs(1 - 0.2 / H)))})
    return out


def main():
    OUT.mkdir(exist_ok=True)
    lines = []
    p = lines.append
    p("M/P at the partials: mic = mono mic sum (Piezo Mix 0), P = Capture Piezo; phase of M/P in degrees")
    p("plus/minus = level change of that partial with +0.2P / -0.2P (dB); m/0.2p = |M| over |0.2 P| (dB)")
    for shape in SHAPES:
        for wood in WOODS:
            key = f"{shape}-{wood}"
            base = STUDY / "sweeps" / key
            allrows, mics, piezos = [], [], []
            for midi in range(40, 85):
                mic = load(base / "mic" / f"note-{midi}.f32").mean(axis=1)
                piezo = load(base / "piezo" / f"note-{midi}.f32")[:, 0]
                allrows += partials(mic, piezo, midi)
                mics.append(mic)
                piezos.append(piezo)
            allrows.sort(key=lambda r: r["hz"])
            with (OUT / f"{key}-partials.csv").open("w") as f:
                f.write("hz,note,k,mic_db,piezo02_db,mic_over_02piezo_db,phase_deg,plus_db,minus_db\n")
                for r in allrows:
                    f.write(f"{r['hz']:.2f},{r['note']},{r['k']},{r['mic_db']:.2f},{r['piezo02_db']:.2f},"
                            f"{r['mic_over_02piezo_db']:.2f},{r['phase_deg']:.1f},{r['plus_db']:.2f},{r['minus_db']:.2f}\n")
            M = np.concatenate(mics)
            P = np.concatenate(piezos)
            f, spm = csd(P, M, fs=RATE, nperseg=8192)
            _, spp = welch(P, fs=RATE, nperseg=8192)
            _, coh = coherence(P, M, fs=RATE, nperseg=8192)
            H = spm / spp
            with (OUT / f"{key}-welch.csv").open("w") as fh:
                fh.write("hz,mic_over_02piezo_db,phase_deg,coherence\n")
                for fi, Hi, ci in zip(f, H, coh):
                    if 60 <= fi <= 8000:
                        fh.write(f"{fi:.2f},{20*np.log10(abs(Hi)/0.2+1e-30):.2f},{np.degrees(np.angle(Hi)):.1f},{ci:.3f}\n")
            hole = HOLES[(shape, wood)]
            p("")
            p(f"{key}: located hole {hole} Hz. Fundamentals (k=1) from 0.75x to 1.25x the hole:")
            p(f"   {'note':>4} {'Hz':>7} {'mic dB':>7} {'0.2P dB':>7} {'m/0.2p':>7} {'phase':>7} {'+0.2P':>6} {'-0.2P':>6}")
            for r in allrows:
                if r["k"] == 1 and 0.75 * hole <= r["hz"] <= 1.25 * hole:
                    p(f"   {r['note']:>4} {r['hz']:7.1f} {r['mic_db']:7.1f} {r['piezo02_db']:7.1f} "
                      f"{r['mic_over_02piezo_db']:7.1f} {r['phase_deg']:7.1f} {r['plus_db']:+6.1f} {r['minus_db']:+6.1f}")
            if (shape, wood) in (("parlor", "spruce"), ("jumbo", "spruce")):
                p(f"   all partials 0.6x-1.6x the hole (note k Hz m/0.2p phase):")
                for r in allrows:
                    if 0.6 * hole <= r["hz"] <= 1.6 * hole:
                        p(f"     {r['note']:>4} k{r['k']:<2} {r['hz']:7.1f} {r['mic_over_02piezo_db']:6.1f} dB {r['phase_deg']:7.1f} deg")
                p("   Welch H1 (coherence > 0.9) 0.6x-1.6x the hole, every 4th bin:")
                sel = [(fi, Hi, ci) for fi, Hi, ci in zip(f, H, coh) if 0.6 * hole <= fi <= 1.6 * hole and ci > 0.9]
                for fi, Hi, ci in sel[::4]:
                    p(f"     {fi:7.1f} Hz {20*np.log10(abs(Hi)/0.2):6.1f} dB {np.degrees(np.angle(Hi)):7.1f} deg coh {ci:.2f}")
            # Power-weighted in-phase share by octave band: sum Re(M P*) / sqrt(sum|M|^2 sum|P|^2)
            p("   power-weighted cos(phase) of M against P over partials, by band (Hz: rho):")
            bands = [(70, 140), (140, 280), (280, 400), (400, 560), (560, 800), (800, 1120), (1120, 2240), (2240, 5000)]
            parts = []
            for lo, hi in bands:
                sel = [r for r in allrows if lo <= r["hz"] < hi]
                if not sel:
                    continue
                mags_m = np.array([10 ** (r["mic_db"] / 20) for r in sel])
                mags_p = np.array([10 ** (r["piezo02_db"] / 20) for r in sel])
                ph = np.radians([r["phase_deg"] for r in sel])
                rho = np.sum(mags_m * mags_p * np.cos(ph)) / np.sqrt(np.sum(mags_m ** 2) * np.sum(mags_p ** 2))
                parts.append(f"{lo}-{hi}: {rho:+.2f}")
            p("     " + "; ".join(parts))
    (OUT / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
