#!/usr/bin/env python3
"""Remove the music room's reverberant tail from g21's impact responses.

g21, steel's measured body, is the one guitar the engine plays that was
measured in a room: Mores' List_of_guitars_description.pdf puts g14-g19 and
g21-g26 in the same school music room in Freiburg, "semi-reverberant, 1st
acoustic reflection after 6 m (loop)". A modal fit over g21's 250 ms window
takes that room's reverberation for the body's own ringing. This module
estimates the room's part of each response from the data and subtracts it
before the fit, so the fit sees the body.

Model. The microphones are 10 cm over the top plate, inside the critical
distance, so the direct sound dominates early and the room's diffuse field is
fed by it. Sabine's energy balance for the reverberant energy density w,
dw/dt = P(t)/V - 2 delta w (Kuttruff, Room Acoustics, 5th ed., 2009, ch. 5,
with 2 delta = cA/4V), sampled in frames of HOP samples, is

    E_r(l) = exp(-2 delta HOP/Fs) E_r(l-1) + kappa E_d(l - D),
    E_d(l) = max(E(l) - E_r(l), 0),

per band, with E the measured band energy, E_d its direct (body) part and D
the first reflection's delay: 6 m / 343 m/s = 17.5 ms. It is the recursion of
Habets, Gannot and Cohen, "Late reverberant spectral variance estimation based
on a statistical model", IEEE Signal Processing Letters 16 (2009) 770-773,
whose direct-to-reverberant coupling is written for exactly this
case of a source nearer than the critical distance, here applied to an
impulse response instead of a running signal.

The room is measured, not assumed. delta is, per third-octave band, the median
over the eleven other guitars measured in the same room (every guitar but g21,
all six impact/microphone paths) of the energy decay rate over 200-500 ms.
kappa is the coupling that makes the recursion carry that window's measured
energy; the room's own share is the room guitars' median coupling less the
median of the nine anechoically measured guitars of the same archive (g34-g43)
read with the same delta, in the bands where the room guitars' late energy
exceeds the anechoic ones' at the 5% level (one-sided Mann-Whitney U over
per-guitar medians). Where the anechoic records carry as much late energy as
the room's -- the air mode's band at 100 Hz, and 125 Hz -- the room adds
nothing measurable and nothing is removed. g21 itself is held out, and its own
coupling checks the model.

Bands and synthesis. The bands are a partition of unity: triangular weights
in log frequency between adjacent third-octave centres from 100 Hz to 10 kHz,
flat below the first and above the last, so the band signals sum exactly to
the response. Each band is scaled by the time-varying gain sqrt(E_d/E) and
the bands summed. Before the first reflection the gain is one, and negative
time (the circular end of the buffer) is left untouched.

    python3 Tools/RemoveRoomTail.py --raw-mat /path/qualified_selected_impulses.mat
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

import GenerateMeasuredBody as body

SAMPLE_RATE = body.SAMPLE_RATE
FFT_SIZE = body.FFT_SIZE
RECORD_SAMPLES = body.RECORD_SAMPLES
HOP = 48  # 1 ms frames
SPEED_OF_SOUND = 343.0  # m/s at 20 C
FIRST_REFLECTION_LOOP_M = 6.0  # List_of_guitars_description.pdf, g14-g26
DELAY_FRAMES = int(round(FIRST_REFLECTION_LOOP_M / SPEED_OF_SOUND * SAMPLE_RATE / HOP))
LATE_WINDOW_S = (0.2, 0.5)
CENTRES = 1000.0 * 2.0 ** (np.arange(-13, 11) / 3.0)  # 50 Hz .. 10 kHz
ROOM_GUITARS = (14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25, 26)
ANECHOIC_GUITARS = (34, 35, 36, 37, 38, 39, 41, 42, 43)
IMPACTS = (0, 2)  # bass-side and treble-side bridge impacts
MICROPHONES = (3, 4, 5)  # upper, treble, bass (MATLAB channels 4-6)


def weights() -> np.ndarray:
    """(bands, bins) triangular log-frequency partition of unity."""
    frequency = np.fft.rfftfreq(FFT_SIZE, 1.0 / SAMPLE_RATE)
    position = np.interp(np.log2(np.maximum(frequency, 1.0)), np.log2(CENTRES),
                         np.arange(len(CENTRES)))
    result = np.zeros((len(CENTRES), len(frequency)))
    for band in range(len(CENTRES)):
        result[band] = np.clip(1.0 - np.abs(position - band), 0.0, 1.0)
    return result


WEIGHTS = weights()


def band_signals(impulse: np.ndarray) -> np.ndarray:
    spectrum = np.fft.rfft(impulse, FFT_SIZE)
    return np.fft.irfft(WEIGHTS * spectrum[None, :], FFT_SIZE, axis=1)


def frame_energy(bands: np.ndarray) -> np.ndarray:
    frames = RECORD_SAMPLES // HOP
    return (bands[:, :frames * HOP] ** 2).reshape(len(bands), frames, HOP).mean(axis=2)


def recursion(energy: np.ndarray, kappa: np.ndarray, delta: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    decay = np.exp(-2.0 * delta * HOP / SAMPLE_RATE)
    room = np.zeros_like(energy)
    direct = np.zeros_like(energy)
    for frame in range(energy.shape[1]):
        room[:, frame] = (decay * room[:, frame - 1] if frame else 0.0) + (
            kappa * direct[:, frame - DELAY_FRAMES] if frame >= DELAY_FRAMES else 0.0)
        direct[:, frame] = np.maximum(energy[:, frame] - room[:, frame], 0.0)
    return room, direct


def late_mask(frames: int) -> np.ndarray:
    time = (np.arange(frames) + 0.5) * HOP / SAMPLE_RATE
    return (time >= LATE_WINDOW_S[0]) & (time < LATE_WINDOW_S[1])


def decay_rate(energy: np.ndarray) -> np.ndarray:
    """Amplitude decay rate delta (1/s) per band from the late window's slope."""
    mask = late_mask(energy.shape[1])
    time = (np.arange(energy.shape[1]) + 0.5)[mask] * HOP / SAMPLE_RATE
    slope = np.array([np.polyfit(time, 10.0 * np.log10(row[mask] + 1e-40), 1)[0] for row in energy])
    return np.maximum(-slope, 1e-3) * np.log(10.0) / 20.0


def coupling(energy: np.ndarray, delta: np.ndarray) -> np.ndarray:
    """kappa per band at which the recursion carries the late window's energy."""
    mask = late_mask(energy.shape[1])
    low, high = np.full(len(energy), 1e-12), np.ones(len(energy))
    for _ in range(40):
        kappa = np.sqrt(low * high)
        room, _ = recursion(energy, kappa, delta)
        over = room[:, mask].sum(axis=1) > energy[:, mask].sum(axis=1)
        high = np.where(over, kappa, high)
        low = np.where(over, low, kappa)
    return np.sqrt(low * high)


def impulse(responses: dict, guitar: int, impact: int, channel: int) -> np.ndarray:
    return np.fft.irfft(responses[guitar, impact, channel], FFT_SIZE)


def energies(values: np.ndarray, guitars: tuple[int, ...]) -> dict:
    import AuditBridgeSpatialMap as spatial
    responses, _ = spatial.extract(values, guitars)
    return {(g, i, c): frame_energy(band_signals(impulse(responses, g, i, c)))
            for g in guitars for i in IMPACTS for c in MICROPHONES}


def per_guitar(couplings: dict, guitars: tuple[int, ...]) -> np.ndarray:
    return np.array([np.median([value for key, value in couplings.items() if key[0] == guitar], axis=0)
                     for guitar in guitars])


def fit_room(values: np.ndarray, held_out: int = 21) -> dict:
    from scipy.stats import mannwhitneyu
    room_guitars = tuple(g for g in ROOM_GUITARS if g != held_out)
    room_energy = energies(values, room_guitars)
    anechoic_energy = energies(values, ANECHOIC_GUITARS)
    delta = np.median([decay_rate(e) for e in room_energy.values()], axis=0)
    room = per_guitar({k: coupling(e, delta) for k, e in room_energy.items()}, room_guitars)
    anechoic = per_guitar({k: coupling(e, delta) for k, e in anechoic_energy.items()}, ANECHOIC_GUITARS)
    room_kappa, anechoic_kappa = np.median(room, axis=0), np.median(anechoic, axis=0)
    # The room's share is removed only in bands where the room guitars carry
    # more late energy than the anechoic ones at the conventional 5% level
    # (one-sided Mann-Whitney U over per-guitar medians, 11 against 9).
    p = np.array([mannwhitneyu(room[:, b], anechoic[:, b], alternative="greater").pvalue
                  for b in range(len(CENTRES))])
    kappa = np.where(p < 0.05, np.maximum(room_kappa - anechoic_kappa, 0.0), 0.0)
    return dict(delta=delta, room_kappa=room_kappa, anechoic_kappa=anechoic_kappa, p=p, kappa=kappa)


def crossover_frames(energy: np.ndarray, room: np.ndarray) -> np.ndarray:
    """First frame from which the room carries half the band's remaining energy.

    Remaining energy is the backward integral to the end of the late window
    the room was calibrated on (Schroeder's integration). Past this point the
    body is the smaller part of what the microphone hears and subtracting an
    expected room energy from a single noise-like realisation leaves most of
    it, so the band is gated there instead; a band the room never overtakes
    keeps every frame.
    """
    end = int(LATE_WINDOW_S[1] * SAMPLE_RATE / HOP)
    remaining = np.cumsum(energy[:, :end][:, ::-1], axis=1)[:, ::-1]
    room_remaining = np.cumsum(room[:, :end][:, ::-1], axis=1)[:, ::-1]
    result = np.full(len(energy), energy.shape[1])
    for band in range(len(energy)):
        over = np.flatnonzero(room_remaining[band, DELAY_FRAMES:] >= 0.5 * remaining[band, DELAY_FRAMES:])
        if len(over):
            result[band] = DELAY_FRAMES + over[0]
    return result


def gains(energy: np.ndarray, room: dict) -> tuple[np.ndarray, np.ndarray]:
    """Per-frame band gains: the direct part's share, then the crossover gate."""
    reverberant, direct = recursion(energy, room["kappa"], room["delta"])
    gain = np.sqrt(np.divide(direct, energy, out=np.ones_like(energy), where=energy > 0))
    crossover = crossover_frames(energy, reverberant)
    for band, end in enumerate(crossover):
        if end >= energy.shape[1]:
            continue
        # The generator's own convention: the final tenth of what is kept fades.
        fade = max(1, end // 10)
        gain[band, end - fade:end] *= 0.5 + 0.5 * np.cos(np.linspace(0.0, np.pi, fade))
        gain[band, end:] = 0.0
    return gain, crossover


def remove(response: np.ndarray, room: dict) -> np.ndarray:
    """Room-free complex response on the FFT_SIZE grid."""
    bands = band_signals(np.fft.irfft(response, FFT_SIZE))
    gain, _ = gains(frame_energy(bands), room)
    centre = (np.arange(gain.shape[1]) + 0.5) * HOP
    samples = np.arange(RECORD_SAMPLES)
    result = bands.sum(axis=0)
    result[:RECORD_SAMPLES] = sum(np.interp(samples, centre, gain[band]) * bands[band, :RECORD_SAMPLES]
                                  for band in range(len(bands)))
    return np.fft.rfft(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--raw-mat", type=Path, required=True)
    args = parser.parse_args()
    values = body.load_matrix(args.raw_mat)
    room = fit_room(values)
    g21 = energies(values, (21,))
    own = np.median([coupling(e, room["delta"]) for e in g21.values()], axis=0)
    print("band Hz  T60 s  kappa room/anechoic/room-share/g21 dB  p")
    for index, centre in enumerate(CENTRES):
        print(f"{centre:7.0f}  {6.9078 / room['delta'][index]:5.2f}  "
              + "  ".join(f"{10 * np.log10(max(room[key][index], 1e-30)):6.1f}"
                          for key in ("room_kappa", "anechoic_kappa", "kappa"))
              + f"  {10 * np.log10(own[index]):6.1f}  {room['p'][index]:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
