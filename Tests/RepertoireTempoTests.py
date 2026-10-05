#!/usr/bin/env python3
"""Tempo export checks using a synthetic score; no downloaded repertoire."""
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
import unittest


spec = importlib.util.spec_from_file_location(
    "perform_repertoire", Path(__file__).resolve().parents[1] / "Tools/PerformRepertoire.py")
performer = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = performer
spec.loader.exec_module(performer)


def performance():
    piece = performer.Piece(
        name="tempo-fixture", title="Tempo fixture", source="synthetic",
        guitars={"g": performer.Guitar()}, tracks=None,
        form=[performer.Segment((1, 1), tempo=[(0, 0.8)], close="section"),
              performer.Segment((2, 2), tempo=[(0, 1.2)], breath=0.2, close="final")],
        bpm=120.0, style=performer.Style(final_hold=1.0, final_rit=(0.65, 1.0, 2.0)))
    midi = performer.MidiFile(
        division=480,
        tracks=[[(0, 480, 64), (960, 1440, 67),
                 (1920, 2400, 64), (2880, 3840, 69)]],
        timesigs=[(0, 4, 4)], tempo=120.0)
    return performer.perform(piece, midi, 48000)


class RepertoireTempoTests(unittest.TestCase):
    def test_export_follows_every_clock_cell_including_breath_and_hold(self):
        text, _, clock = performance()
        tempos = [tuple(map(float, line.split()[1:]))
                  for line in text.splitlines() if line.startswith("tempo ")]
        self.assertEqual(len(tempos), 8 * performer.GRID + 1)
        self.assertEqual(tempos[0][0], 0.0)  # Also covers early attacks in the lead-in.
        for cell, (seconds, bpm) in enumerate(tempos):
            self.assertTrue(math.isfinite(seconds) and seconds >= 0.0)
            self.assertTrue(math.isfinite(bpm) and bpm > 0.0)
            start = clock(cell / performer.GRID)
            end = clock((cell + 1) / performer.GRID)
            expected_time = 0.0 if cell == 0 else performer.LEAD_IN + start
            self.assertAlmostEqual(seconds, expected_time, delta=5.1e-10)
            self.assertAlmostEqual(bpm * (end - start) / 60.0,
                                   1.0 / performer.GRID, delta=1e-12)
        breath_cell = 4 * performer.GRID - 1
        self.assertGreater(clock(4.0) - clock(breath_cell / performer.GRID), 0.2)
        self.assertLess(tempos[breath_cell][1], tempos[breath_cell - 1][1])
        self.assertAlmostEqual(tempos[-1][1], 120.0)

    def test_authored_events_and_random_timing_are_unchanged(self):
        text, _, _ = performance()
        events = "\n".join(line for line in text.splitlines() if line.startswith("e ")) + "\n"
        # Recorded from this synthetic fixture before adding tempo export.
        self.assertEqual(hashlib.sha256(events.encode()).hexdigest(),
                         "af4ac56e413573b8d06bab56af40c480112fe6e9c8e503f07f4948afbf314f15")
        self.assertEqual(text, performance()[0])


if __name__ == "__main__":
    unittest.main()
