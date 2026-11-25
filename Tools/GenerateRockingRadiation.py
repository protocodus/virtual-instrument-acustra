#!/usr/bin/env python3
"""Hold g21's rocking radiation to the anechoic flamencas' where it stands out.

Steel's body is Mores' g21, and a steel pluck drives it mostly through the
bridge's rocking: 70% of the pluck lies in the plane parallel to the top, and
the bridge turns that into a moment. Of the archive's 63 guitars, g21
radiates its rocking unusually strongly at 1.3-1.5 kHz: at the upper-bout
microphone its rocking path stands 10.7 dB over its own reference level in
the sixth octave at 1414 Hz, where the other 62 guitars' median stands
2.0 dB over theirs (2nd of 63, read as below). On the Jumbo the Shape morph
puts that group on the partial G, C and Cadd9 share across four strings
(1175-1177 Hz).

The rule reads the rocking path's level the way the plate-Q rule reads Q
(GenerateBodyForcePair.py --plate-q median): against the same anechoic
flamencas, and never raising a mode.

  - Every guitar, g21 included, is the archive's calibrated H1 over the full
    one-second record (AuditBridgeSpatialMap.extract), so measurement is
    compared with measurement. g21's record carries the music room it was
    made in; the reference below removes its broad share, and the bank's own
    response (the 250 ms fit) gives the 1.3-1.5 kHz group's factors within
    1.5 dB of these.
  - The rocking path at a microphone is (Ht - Hb)/2, the treble impact's
    response less the bass impact's, as MeasuredBodyData.h's moment residues
    are (Rm = (Rt - Rb)/2).
  - Levels are mean power over sixth octaves on the grid 1 kHz x 2^(k/6),
    the resolution the archive's audit tools read modal detail at
    (AuditBridgeSpatialMap.py): coarse enough that each band holds a few
    modes, fine enough to resolve one group.
  - Each path (guitar and microphone) is referred to its own median band
    level over 500 Hz-3 kHz, the plate modes' main band, so a guitar's
    loudness and its rocking's share against its pushing are not compared,
    only the rocking radiation's shape. Referred to 300 Hz-10 kHz instead,
    the group's factors move by 1.2 dB at most.
  - The population is the five flamencas the plate-Q rule reads
    (PLATE_Q_POPULATION less g41, whose fit fails the generator's gates).
    With g41 the group's factors move by 0.8 dB at most.
  - At each grid band the population's median referred level less g21's is
    interpolated in log frequency to each bank mode from 300 Hz up to 3 kHz.
    The air group, T1 and the rocking modes below 300 Hz are where by-ear
    choices already act (the air-mode gain, the T1 weight): the plate-Q
    rule's first band starts there too. The rule stops at the top of the
    band its levels are referred to. Run on to the plate-Q rule's 10 kHz
    (letter B of the 2026-11-25 sets, Docs/decisions.md), it also pulled
    g21's rocking down by up to 5.5 dB near 3.2 kHz and at 7-10 kHz, where
    g21 ranks 2nd to 12th of the 63 at the treble and bass microphones, and
    took the palm-muted Pick's 7 kHz attack 3.5 dB under released 1.3.3's,
    outside Acustra.MutedPickAttack's 2 dB.

The factor on a mode's moment residues at a microphone is min(1, 10^(d/20))
of that interpolated difference d in dB. The pushing (force) residues, every
frequency and Q, the bridge (MeasuredBridgeData.h) and the joint-pole body's
kept modes (all under 550 Hz, where this rule leaves g21's modes within
0.5 dB) are unchanged.

--check regenerates from the archive and compares values with the committed
table: a factor within 1e-6 of the regenerated one passes, since FFT and
median rounding differ in the last bits between platforms.

    python3 Tools/GenerateRockingRadiation.py --self-test
    python3 Tools/GenerateRockingRadiation.py --raw-mat /path/qualified_selected_impulses.mat --write
    python3 Tools/GenerateRockingRadiation.py --raw-mat /path/qualified_selected_impulses.mat --check
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re

import numpy as np

import AuditBridgeSpatialMap as spatial
import GenerateBodyForcePair as pair
import GenerateMeasuredBridge as bridge

REPOSITORY = Path(__file__).resolve().parents[1]
BODY_HEADER = REPOSITORY / "Source" / "DSP" / "MeasuredBodyData.h"
OUTPUT_HEADER = REPOSITORY / "Source" / "DSP" / "RockingRadiationData.h"
STEEL_GUITAR = 21
POPULATION = (37, 38, 39, 42, 43)
REFERENCE_HZ = (500.0, 3000.0)
BAND_HZ = (pair.PLATE_Q_T1_UPPER_HZ, REFERENCE_HZ[1])
# 140 Hz to 3.2 kHz: the rule's band with a grid band to spare at each end.
GRID = 1000.0 * 2.0 ** (np.arange(-17, 11) / 6.0)
# The header's microphone order, and the archive channel each one is.
MICROPHONES = ("treble", "bass", "upper")
CHANNELS = {"treble": 4, "bass": 5, "upper": 3}
FREQUENCY = np.fft.rfftfreq(bridge.FFT_SIZE, 1 / bridge.SAMPLE_RATE)
HALF_SIXTH = 2.0 ** (1.0 / 12.0)


def referred_db(power: np.ndarray) -> np.ndarray:
    """Sixth-octave levels on GRID, less their median over REFERENCE_HZ."""
    levels = np.array([10.0 * np.log10(np.mean(
        power[(FREQUENCY >= centre / HALF_SIXTH) & (FREQUENCY < centre * HALF_SIXTH)]))
        for centre in GRID])
    reference = (GRID >= REFERENCE_HZ[0]) & (GRID <= REFERENCE_HZ[1])
    return levels - np.median(levels[reference])


def rocking_scales(frequencies: np.ndarray, steel: dict, population: list) -> np.ndarray:
    """Per mode and microphone (MICROPHONES order), the factor on its moment residues.

    steel and each population entry map a microphone to the rocking path's
    power spectrum on FREQUENCY.
    """
    scales = np.ones((len(frequencies), len(MICROPHONES)))
    inside = (frequencies >= BAND_HZ[0]) & (frequencies < BAND_HZ[1])
    for column, microphone in enumerate(MICROPHONES):
        typical = np.median([referred_db(guitar[microphone]) for guitar in population], axis=0)
        difference = typical - referred_db(steel[microphone])
        at_modes = np.interp(np.log(frequencies[inside]), np.log(GRID), difference)
        scales[inside, column] = np.minimum(1.0, 10.0 ** (at_modes / 20.0))
    return scales


def archive_power(values: np.ndarray, guitar: int) -> dict:
    """A guitar's measured rocking path, (Ht - Hb)/2, at each microphone."""
    responses = spatial.extract(values, (guitar,))[0]
    return {microphone: np.abs(0.5 * (responses[guitar, 2, channel]
                                      - responses[guitar, 0, channel])) ** 2
            for microphone, channel in CHANNELS.items()}


def bank_frequencies(path: Path = BODY_HEADER) -> np.ndarray:
    return bridge.header_rows(path, "measuredSteelBodyModes")[:, 0]


def render_header(frequencies: np.ndarray, scales: np.ndarray) -> str:
    cpp = pair.body.cpp_float
    population = ", ".join(f"g{guitar}" for guitar in POPULATION)
    lines = [
        "// Generated by Tools/GenerateRockingRadiation.py; do not hand-edit.",
        "// Robert Mores, https://zenodo.org/records/4604577, CC BY 4.0:",
        "// https://creativecommons.org/licenses/by/4.0/",
        "// Per mode of MeasuredBodyData.h, the factor its rocking (moment)",
        "// residues take at each microphone, so that g21's measured rocking",
        "// radiation, in sixth octaves referred to its own median over",
        f"// {REFERENCE_HZ[0]:.0f} Hz-{REFERENCE_HZ[1] / 1000:.0f} kHz, stands no higher than the anechoic",
        f"// flamencas' median ({population}). Never above 1; modes outside",
        f"// {BAND_HZ[0]:.0f} Hz-{BAND_HZ[1] / 1000:.0f} kHz are 1.",
        "",
        "#pragma once",
        "#include <array>",
        "",
        "namespace acustra::detail",
        "{",
        "struct RockingRadiationScale",
        "{",
        "    float frequency, treble, bass, upper;",
        "};",
        "",
        f"inline constexpr std::array<RockingRadiationScale, {len(frequencies)}> "
        "measuredSteelRockingScales {{",
    ]
    for frequency, row in zip(frequencies, scales):
        lines.append("    { " + ", ".join(cpp(value) for value in (frequency, *row)) + " },")
    lines += ["}};", "", "} // namespace acustra::detail", ""]
    return "\n".join(lines)


def generate(raw: Path) -> str:
    values = bridge.load_matrix(raw)
    frequencies = bank_frequencies()
    scales = rocking_scales(frequencies, archive_power(values, STEEL_GUITAR),
                            [archive_power(values, guitar) for guitar in POPULATION])
    return render_header(frequencies, scales)


def table_rows(text: str) -> np.ndarray:
    return np.array([[float(value.rstrip("f")) for value in match.split(", ")]
                     for match in re.findall(r"^    \{ ([^{}]*) \},$", text, flags=re.M)])


def committed_matches_bank(header: Path = OUTPUT_HEADER, body: Path = BODY_HEADER) -> None:
    table = bridge.header_rows(header, "measuredSteelRockingScales")
    frequencies = bank_frequencies(body)
    if len(table) != len(frequencies) or not np.array_equal(table[:, 0], frequencies):
        raise AssertionError(f"{header.name} was written for another body bank: "
                             "rerun Tools/GenerateRockingRadiation.py --write")
    if not (np.all(table[:, 1:] > 0.0) and np.all(table[:, 1:] <= 1.0)):
        raise AssertionError(f"{header.name} raises or removes a mode")
    outside = (table[:, 0] < BAND_HZ[0]) | (table[:, 0] >= BAND_HZ[1])
    if np.any(table[outside, 1:] != 1.0):
        raise AssertionError(f"{header.name} reaches a mode outside {BAND_HZ}")


def self_test() -> None:
    if not set(POPULATION) <= set(pair.PLATE_Q_POPULATION) or STEEL_GUITAR in POPULATION:
        raise AssertionError("the population is no longer the plate-Q rule's")
    # A falling spectrum: each band changed below moves away from its path's
    # median side, so the reference, and every other mode's factor, stays
    # exact.
    shape = 1.0 / (1.0 + FREQUENCY / 1000.0)

    def guitar(level: float) -> dict:
        return {microphone: level * shape.copy() for microphone in MICROPHONES}

    def band(centre: float) -> np.ndarray:
        return (FREQUENCY >= centre / HALF_SIXTH) & (FREQUENCY < centre * HALF_SIXTH)

    levels = (0.5, 2.0, 4.0, 8.0, 16.0)
    population = [guitar(level) for level in levels]
    steel = guitar(3.0)
    # The grid band at 630 Hz stands 9 dB over the population at the upper
    # microphone only; the one at 2.52 kHz sits 6 dB under it at the treble.
    steel["upper"][band(GRID[13])] *= 10.0 ** 0.9
    steel["treble"][band(GRID[25])] *= 10.0 ** -0.6
    half_way = float(np.sqrt(GRID[13] * GRID[14]))
    frequencies = np.array([200.0, 1000.0, GRID[13], half_way, GRID[25], 3000.0])
    scales = rocking_scales(frequencies, steel, population)
    expected = np.ones_like(scales)
    expected[2, 2] = 10.0 ** -0.45
    expected[3, 2] = 10.0 ** -0.225
    if not np.allclose(scales, expected, rtol=0.0, atol=1e-9):
        raise AssertionError(f"the rule does not read the population's median:\n{scales}")
    if np.any(scales[[0, 5]] != 1.0):
        raise AssertionError("the rule reached a mode outside its band")
    # Loudness is not compared: scaling any one path leaves every factor.
    louder = {microphone: 7.0 * power for microphone, power in steel.items()}
    quieter = [guitar(0.1 * level) if index == 2 else population[index]
               for index, level in enumerate(levels)]
    if not np.allclose(rocking_scales(frequencies, louder, quieter), scales, rtol=0.0, atol=1e-9):
        raise AssertionError("a path's overall level changed the rule")
    header = render_header(np.array([300.0, 1320.60205]),
                           np.array([[1.0, 1.0, 1.0], [0.5, 0.25, 0.125]]))
    if "    { 1320.60205f, 0.5f, 0.25f, 0.125f }," not in header \
            or not np.array_equal(table_rows(header)[1], [1320.60205, 0.5, 0.25, 0.125]):
        raise AssertionError("the header changed its microphone order or format")
    committed_matches_bank()
    print("Rocking radiation generator self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--raw-mat", type=Path,
                        help="Mores' qualified_selected_impulses.mat (MD5 checked)")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--self-test", action="store_true")
    action.add_argument("--write", action="store_true",
                        help=f"write {OUTPUT_HEADER.relative_to(REPOSITORY)}")
    action.add_argument("--check", action="store_true",
                        help="compare the committed table with a regenerated one")
    args = parser.parse_args()
    try:
        if args.self_test:
            if args.raw_mat:
                parser.error("--self-test does not read the archive")
            self_test()
            return 0
        if args.raw_mat is None:
            parser.error("--write and --check need --raw-mat")
        text = generate(args.raw_mat)
        if args.write:
            OUTPUT_HEADER.write_text(text, encoding="utf-8")
            print(f"Wrote {OUTPUT_HEADER}")
            return 0
        committed = table_rows(OUTPUT_HEADER.read_text(encoding="utf-8"))
        regenerated = table_rows(text)
        if committed.shape != regenerated.shape or not np.allclose(
                committed, regenerated, rtol=0.0, atol=1e-6):
            print(f"FAIL: {OUTPUT_HEADER.name} differs from the archive's")
            return 1
        print(f"{OUTPUT_HEADER.name} matches the archive")
        return 0
    except (OSError, ValueError, AssertionError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
