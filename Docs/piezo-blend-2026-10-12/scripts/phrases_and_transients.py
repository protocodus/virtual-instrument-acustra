#!/usr/bin/env python3
"""Strummed phrase spectra and loudness, transients, and broadband alignment.

Variants by superposition: V0 = microphones (Piezo Mix 0); V1 = V0 + 0.2 P;
V2 = V0 - 0.2 P; V3own = the construction's best E2-C6 grid setting
(evenness.json); V3common = + polarity with the piezo 0.75 ms earlier (the grid
setting with the lowest mean RMS deviation over the 12 constructions).

1. Phrases (phrases/*.f32; 'strums' = Tools/MeasureMaterialLoudness.py
   strums(), 'calibration' = Tools/CalibrateConstructionLoudness.py
   phrase_set(); Finger, stereo mic, Room 0.5): BS.1770-4 integrated loudness
   (MeasureMaterialLoudness.integrated_loudness) and third-octave band levels
   80 Hz-8 kHz (Welch PSD, nperseg 16384, mean of the two channels' power),
   plus the band correlation of the mono microphones and the piezo,
   rho = sum Re S_MP / sqrt(sum S_MM sum S_PP) over the band.
2. Transients: single notes (sweep notes, Finger v96; picks/, Pick v120):
   sample peak (max over both channels, dBFS) and the energy of the first
   5 ms (mean square over both channels, dBFS) from a common onset (first
   sample where V0 or 0.2 P exceeds 1e-3 of V1's peak), and 5-80 ms ('att').
3. Broadband alignment: normalised cross-correlation of the mono microphones
   and the piezo over the E2-C6 notes and over the strums, lags +-2 ms.

Writes phrases.json, phrases.txt, transients.json, transients.txt,
alignment.txt.
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.signal import welch, csd

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureMaterialLoudness as meter  # noqa: E402
import MeasureNoteEvenness as ev  # noqa: E402
sys.path.insert(0, str(STUDY / "scripts"))
from evenness_variants import SHAPES, WOODS, MIX, load, shifted  # noqa: E402

RATE = 48000
CENTRES = [1000 * 2 ** (n / 3) for n in range(-11, 10)]  # 79.4 Hz .. 7937 Hz (nominal 80..8000)
NOMINAL = [80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
           2000, 2500, 3150, 4000, 5000, 6300, 8000]
COMMON = (+1, -36)


def setting_from_label(label):
    return (+1 if label[0] == "+" else -1, int(round(float(label[1:-2]) * 48)))


def variant(mic, piezo, setting):
    if setting is None:
        return mic
    sign, k = setting
    return mic + sign * MIX * shifted(piezo, k)[:, None]


def bands(stereo):
    f, pl = welch(stereo[:, 0], fs=RATE, nperseg=16384)
    _, pr = welch(stereo[:, 1], fs=RATE, nperseg=16384)
    psd = 0.5 * (pl + pr)
    step = f[1] - f[0]
    out = []
    for c in CENTRES:
        sel = (f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(psd[sel].sum() * step + 1e-30))
    return np.array(out)


def band_rho(mono, piezo):
    f, smp = csd(mono, piezo, fs=RATE, nperseg=16384)
    _, smm = welch(mono, fs=RATE, nperseg=16384)
    _, spp = welch(piezo, fs=RATE, nperseg=16384)
    out = []
    for c in CENTRES:
        sel = (f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(float(np.real(smp[sel]).sum() / np.sqrt(smm[sel].sum() * spp[sel].sum())))
    return np.array(out)


def xcorr(mono, piezo, reach=96):
    norm = np.sqrt(np.sum(mono ** 2) * np.sum(piezo ** 2))
    lags = np.arange(-reach, reach + 1)
    r = np.array([np.sum(mono[reach:-reach] * piezo[reach - k:len(piezo) - reach - k]) for k in lags]) / norm
    return lags, r


def phrases(evenness):
    results, lines = {}, []
    p = lines.append
    for phrase in ("strums", "calibration"):
        p(f"=== phrase '{phrase}' (Finger, stereo mic, Room 0.5) ===")
        p("Integrated loudness LUFS: V0, V1, V2, V3own, V3common; differences V1-V0, V2-V0, V2-V1, V3own-V1, V3common-V1 (LU)")
        for shape in SHAPES:
            for wood in WOODS:
                key = f"{shape}-{wood}"
                base = STUDY / "phrases" / f"{key}-{phrase}"
                mic = load(f"{base}-mic.f32")
                piezo = load(f"{base}-piezo.f32")[:, 0]
                own = setting_from_label(evenness[key]["V3_best_label"])
                settings = {"V0": None, "V1": (+1, 0), "V2": (-1, 0), "V3own": own, "V3common": COMMON}
                lufs, spectra = {}, {}
                for label, setting in settings.items():
                    stereo = variant(mic, piezo, setting)
                    lufs[label] = meter.integrated_loudness(stereo)
                    spectra[label] = bands(stereo)
                rho = band_rho(mic.mean(axis=1), piezo)
                results[(phrase, key)] = {"lufs": lufs, "bands": {k: v.tolist() for k, v in spectra.items()},
                                          "rho": rho.tolist(), "own": evenness[key]["V3_best_label"]}
                p(f"  {key:22} " + " ".join(f"{lufs[l]:7.2f}" for l in settings)
                  + f"   {lufs['V1']-lufs['V0']:+5.2f} {lufs['V2']-lufs['V0']:+5.2f} {lufs['V2']-lufs['V1']:+5.2f} "
                    f"{lufs['V3own']-lufs['V1']:+5.2f} {lufs['V3common']-lufs['V1']:+5.2f}   (own {evenness[key]['V3_best_label']})")
        p("")
        for title, a, b in (("V2 - V1 (inverting the piezo)", "V2", "V1"), ("V1 - V0 (the shipped piezo against none)", "V1", "V0"),
                            ("V3own - V1", "V3own", "V1"), ("V3common - V1 (+, piezo 0.75 ms earlier)", "V3common", "V1")):
            p(f"Third-octave band difference {title}, dB, nominal centres:")
            p(f"  {'':22} " + " ".join(f"{n:>5}" for n in NOMINAL))
            for shape in SHAPES:
                for wood in WOODS:
                    key = f"{shape}-{wood}"
                    r = results[(phrase, key)]
                    diff = np.array(r["bands"][a]) - np.array(r["bands"][b])
                    p(f"  {key:22} " + " ".join(f"{d:+5.1f}" for d in diff))
            p("")
        p("Band correlation rho of the mono microphones and the piezo (+: the piezo adds, -: it cancels):")
        p(f"  {'':22} " + " ".join(f"{n:>5}" for n in NOMINAL))
        for shape in SHAPES:
            for wood in WOODS:
                key = f"{shape}-{wood}"
                p(f"  {key:22} " + " ".join(f"{v:+5.2f}" for v in results[(phrase, key)]["rho"]))
        p("")
    (STUDY / "phrases.json").write_text(json.dumps({f"{k[0]}|{k[1]}": v for k, v in results.items()}, indent=1) + "\n")
    (STUDY / "phrases.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


def transient_metrics(stereo, onset):
    peak = float(20 * np.log10(np.max(np.abs(stereo)) + 1e-30))
    first5 = stereo[onset:onset + 240]
    e5 = float(10 * np.log10(np.mean(first5 ** 2) + 1e-30))
    att = stereo[round(0.005 * RATE):round(0.08 * RATE)]
    e_att = float(10 * np.log10(np.mean(att ** 2) + 1e-30))
    first_peak_sign = int(np.sign(first5.mean(axis=1)[np.argmax(np.abs(first5.mean(axis=1)))]))
    return {"peak_dbfs": peak, "first5ms_dbfs": e5, "att_5_80ms_dbfs": e_att, "first_peak_sign": first_peak_sign}


def transients(evenness):
    rows, lines = [], []
    p = lines.append
    p("Single-note transients: peak dBFS | first-5 ms mean-square dBFS | 5-80 ms dBFS, V1 vs V2 (and V0, V3own)")
    sets = []
    for shape in SHAPES:
        for wood in WOODS:
            key = f"{shape}-{wood}"
            for midi in (40, 45, 50, 55, 59, 64, 72, 78):
                sets.append((key, f"finger v96 {ev.note_name(midi)}",
                             STUDY / "sweeps" / key / "mic" / f"note-{midi}.f32",
                             STUDY / "sweeps" / key / "piezo" / f"note-{midi}.f32"))
            for midi in (40, 55, 60, 64, 66):
                sets.append((key, f"pick v120 {ev.note_name(midi)}",
                             STUDY / "picks" / f"{key}-{midi}-mic.f32",
                             STUDY / "picks" / f"{key}-{midi}-piezo.f32"))
    for key, label, mic_path, piezo_path in sets:
        mic = load(mic_path)
        piezo = load(piezo_path)[:, 0]
        v1 = variant(mic, piezo, (+1, 0))
        ref = np.max(np.abs(v1))
        env = np.maximum(np.abs(mic).max(axis=1), MIX * np.abs(piezo))
        onset = int(np.argmax(env > 1e-3 * ref))
        own = setting_from_label(evenness[key]["V3_best_label"])
        row = {"construction": key, "note": label, "onset_sample": onset}
        for name, setting in (("V0", None), ("V1", (+1, 0)), ("V2", (-1, 0)), ("V3own", own), ("V3common", COMMON)):
            row[name] = transient_metrics(variant(mic, piezo, setting), onset)
        rows.append(row)
    (STUDY / "transients.json").write_text(json.dumps(rows, indent=1) + "\n")
    p(f"{'construction':20} {'note':16} {'V0 pk':>6} {'V1 pk':>6} {'V2 pk':>6} {'V2-V1':>6} | {'V0 5ms':>7} {'V1 5ms':>7} {'V2 5ms':>7} {'V2-V1':>6} | {'V1 att':>7} {'V2 att':>7} {'V2-V1':>6} | sign V0/V1/V2")
    for r in rows:
        if not r["construction"] in ("parlor-spruce", "jumbo-spruce", "dreadnought-spruce"):
            continue
        a, b, c = r["V0"], r["V1"], r["V2"]
        p(f"{r['construction']:20} {r['note']:16} {a['peak_dbfs']:6.1f} {b['peak_dbfs']:6.1f} {c['peak_dbfs']:6.1f} {c['peak_dbfs']-b['peak_dbfs']:+6.2f} | "
          f"{a['first5ms_dbfs']:7.1f} {b['first5ms_dbfs']:7.1f} {c['first5ms_dbfs']:7.1f} {c['first5ms_dbfs']-b['first5ms_dbfs']:+6.2f} | "
          f"{b['att_5_80ms_dbfs']:7.1f} {c['att_5_80ms_dbfs']:7.1f} {c['att_5_80ms_dbfs']-b['att_5_80ms_dbfs']:+6.2f} | {a['first_peak_sign']:+d}/{b['first_peak_sign']:+d}/{c['first_peak_sign']:+d}")
    p("")
    p("All 12 constructions, mean (min..max) of V2-V1 over the listed notes: peak dB | first 5 ms dB | 5-80 ms dB; same for V3common-V1")
    for shape in SHAPES:
        for wood in WOODS:
            key = f"{shape}-{wood}"
            sel = [r for r in rows if r["construction"] == key]
            def stat(v, metric):
                d = np.array([r[v][metric] - r["V1"][metric] for r in sel])
                return f"{d.mean():+5.2f} ({d.min():+5.2f}..{d.max():+5.2f})"
            p(f"  {key:22} V2: {stat('V2','peak_dbfs')} | {stat('V2','first5ms_dbfs')} | {stat('V2','att_5_80ms_dbfs')}"
              f"   V3common: {stat('V3common','peak_dbfs')} | {stat('V3common','first5ms_dbfs')} | {stat('V3common','att_5_80ms_dbfs')}")
    (STUDY / "transients.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


def alignment():
    lines = []
    p = lines.append
    p("Normalised cross-correlation r(k) = sum M[n] P[n-k] / norms of the mono microphones (Piezo Mix 0) and the piezo;")
    p("k > 0: the piezo delayed. r(0), and the lags of max r and min r within +-2 ms (96 samples):")
    for source in ("sweep", "strums"):
        p(f"-- {source}")
        for shape in SHAPES:
            for wood in WOODS:
                key = f"{shape}-{wood}"
                if source == "sweep":
                    M = np.concatenate([load(STUDY / "sweeps" / key / "mic" / f"note-{m}.f32").mean(axis=1) for m in range(40, 85)])
                    P = np.concatenate([load(STUDY / "sweeps" / key / "piezo" / f"note-{m}.f32")[:, 0] for m in range(40, 85)])
                else:
                    M = load(STUDY / "phrases" / f"{key}-strums-mic.f32").mean(axis=1)
                    P = load(STUDY / "phrases" / f"{key}-strums-piezo.f32")[:, 0]
                lags, r = xcorr(M, P)
                i0 = int(np.where(lags == 0)[0][0])
                p(f"  {key:22} r(0) {r[i0]:+.3f}   max {r.max():+.3f} at {lags[r.argmax()]:+d} smp ({lags[r.argmax()]/48:+.3f} ms)"
                  f"   min {r.min():+.3f} at {lags[r.argmin()]:+d} smp ({lags[r.argmin()]/48:+.3f} ms)")
    (STUDY / "alignment.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


def main():
    evenness = json.loads((STUDY / "evenness.json").read_text())["constructions"]
    alignment()
    phrases(evenness)
    transients(evenness)


if __name__ == "__main__":
    main()
