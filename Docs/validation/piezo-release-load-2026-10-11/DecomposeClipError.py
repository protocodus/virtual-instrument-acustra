from pathlib import Path
import json
import numpy as np

rate = 44100
low, high = -3.8919926, 3.2537111
output = []
for index in (2, 3, 4):
    data = np.fromfile(f"clip-{index}.f64", dtype="<f8").reshape(-1, 2)
    drive, stage = data.T
    fade = rate // 20
    window = np.ones(len(drive))
    window[-fade:] = .5 * (1 + np.cos(np.pi * np.arange(fade) / fade))
    n = 1 << (len(drive) - 1).bit_length()
    spectrum = np.fft.rfft(drive * window, n)
    spectrum[-1] = 0
    up = np.fft.irfft(spectrum, n * 8) * 8
    clipped = np.clip(up, low, high)
    reference = (np.fft.rfft(clipped) / 8)[:n // 2 + 1]
    direct = np.fft.rfft(stage * window, n)
    bare = np.fft.rfft(np.clip(drive, low, high) * window, n)
    bins = np.arange(n // 2 + 1) / n
    power = np.sum(abs(reference[bins <= .45]) ** 2)
    row = {"case": index,
           "integer_clipped_samples": int(np.sum((drive < low) | (drive > high))),
           "oversampled_clipped_sample_equivalents": float(np.sum(up != clipped) / 8),
           "maximum_integer_overdrive_db": float(20 * np.log10(max(np.max(drive / high), np.max(drive / low)))),
           "bands": []}
    for lo, hi in ((0, .25), (.25, .375), (.375, .45), (0, .45)):
        selected = (bins >= lo) & (bins <= hi)
        error = np.sum(abs(direct[selected] - reference[selected]) ** 2)
        raw = np.sum(abs(bare[selected] - reference[selected]) ** 2)
        row["bands"].append({"band_fs": [lo, hi],
             "BLAMP_error_db_vs_total": float(10 * np.log10(error / power)),
             "bare_error_db_vs_total": float(10 * np.log10(raw / power)),
             "improvement_db": float(10 * np.log10(raw / error))})
    selected = bins <= .375
    row["below_0375_improvement_db"] = float(10 * np.log10(
        np.sum(abs(bare[selected] - reference[selected]) ** 2)
        / np.sum(abs(direct[selected] - reference[selected]) ** 2)))
    output.append(row)
Path("clip-decomposition.json").write_text(json.dumps(output, indent=2) + "\n")
