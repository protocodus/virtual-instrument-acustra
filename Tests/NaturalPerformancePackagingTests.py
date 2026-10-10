#!/usr/bin/env python3
"""Protect the audition's independent switches, physical score and byte evidence."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from scipy.io import wavfile

SPEC = importlib.util.spec_from_file_location(
    "natural_package", Path(__file__).resolve().parents[1] / "Tools/PackageNaturalPerformance.py")
assert SPEC is not None and SPEC.loader is not None
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)

# Each schema's ablation switches and the variant that enables each alone.
# Schema 3 is current; schemas 1 and 2 describe directories rendered before
# playerBodyLoading, the "body" variant, was removed on 2026-10-10.
SWITCHES = {2: ("contactRelease", "coherentHand", "gestureDamping", "playerBodyLoading", "retuneContinuity"),
            3: ("contactRelease", "coherentHand", "gestureDamping", "retuneContinuity")}
SINGLE = {"contactRelease": "contact", "coherentHand": "hand", "gestureDamping": "damping",
          "playerBodyLoading": "body", "retuneContinuity": "continuity"}


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="acustra-natural-evidence-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        audio = np.array([[0.1, -0.2], [0.25, 0.3]], dtype=np.float32)
        audio.tofile(self.root / "note.f32")
        wavfile.write(self.root / "note.wav", 48000, audio)
        self.score = "frame\tchannel\tstatus\tdata1\tdata2\n0\t1\t176\t126\t6\n1\t3\t144\t55\t84\n"
        (self.root / "score.tsv").write_text(self.score)
        self.allocation = ("frame\tchannel\tmidi_note\texplicit_strings\texpected_string\tactual_string\topen_midi\tfret\n"
                           "1\t3\t55\t1\t2\t2\t50\t5\n")
        (self.root / "allocations.tsv").write_text(self.allocation)
        self.render = {"case": "fixture", "raw": "note.f32", "wav": "note.wav",
                       "score": "score.tsv", "frames": 2, "peak": float(np.max(np.abs(audio))),
                       "rms": PACKAGE.rms(audio), "dropped_events": 0, "verified_explicit_attacks": 1,
                       "allocations": "allocations.tsv", "parameters": {"picking": "finger"}}
        self.manifest = {"schema": 3, "sample_rate": 48000, "channels": 2, "post_gain": 1,
                         "explicit_string_mode_cc126_value": 6, "renders": self.renders(3)}

    def renders(self, schema):
        variants = PACKAGE.VARIANTS if schema == 3 else PACKAGE.SCHEMA2_VARIANTS
        return [dict(deepcopy(self.render), variant=variant,
                     switches={name: variant == "combined" or variant == SINGLE[name]
                               for name in SWITCHES[schema]})
                for variant in variants]

    def inspect(self, manifest=None):
        (self.root / "manifest.json").write_text(json.dumps(manifest or self.manifest))
        return PACKAGE.inspect_renders(self.root)

    def test_six_independent_variants_and_earlier_schemas(self):
        _, groups, records = self.inspect()
        self.assertEqual(set(groups["fixture"]), set(PACKAGE.VARIANTS))
        self.assertEqual(len(records), 6)
        earlier = deepcopy(self.manifest)
        earlier["schema"] = 2
        earlier["renders"] = self.renders(2)
        self.assertEqual(len(self.inspect(earlier)[2]), 7)
        legacy = deepcopy(earlier)
        legacy["schema"] = 1
        legacy["renders"] = [row for row in legacy["renders"] if row["variant"] != "continuity"]
        for row in legacy["renders"]:
            del row["switches"]["retuneContinuity"]
            del row["allocations"]
            del row["verified_explicit_attacks"]
        self.assertEqual(len(self.inspect(legacy)[2]), 6)

    def test_removed_body_mechanism_is_rejected(self):
        self.manifest["renders"][0]["switches"]["playerBodyLoading"] = False
        with self.assertRaisesRegex(ValueError, "switches do not match"):
            self.inspect()
        self.manifest["renders"] = self.renders(3)
        body = deepcopy(self.manifest["renders"][1])
        body["variant"] = "body"
        self.manifest["renders"].append(body)
        with self.assertRaisesRegex(ValueError, "unknown render variant"):
            self.inspect()

    def test_missing_continuity_flag_is_rejected(self):
        del self.manifest["renders"][0]["switches"]["retuneContinuity"]
        with self.assertRaisesRegex(ValueError, "switches do not match"):
            self.inspect()

    def test_isolation_cannot_silently_leave_continuity_enabled(self):
        self.manifest["renders"][1]["switches"]["retuneContinuity"] = True
        with self.assertRaisesRegex(ValueError, "switches do not match"):
            self.inspect()

    def test_score_differences_are_rejected(self):
        (self.root / "changed.tsv").write_text(self.score.replace("55\t84", "55\t85"))
        self.manifest["renders"][1]["score"] = "changed.tsv"
        with self.assertRaisesRegex(ValueError, "variant input confound.*score_sha256"):
            self.inspect()

    def test_wrong_physical_string_is_rejected(self):
        (self.root / "allocations.tsv").write_text(self.allocation.replace("1\t2\t2\t50", "1\t2\t3\t50"))
        with self.assertRaisesRegex(ValueError, "physical-string mismatch"):
            self.inspect()

    def test_disabled_explicit_mode_cannot_claim_verified_attacks(self):
        (self.root / "score.tsv").write_text(self.score.replace("126\t6", "126\t0"))
        with self.assertRaisesRegex(ValueError, "CC126 value 6"):
            self.inspect()

    def test_missing_mode_metadata_is_rejected(self):
        del self.manifest["explicit_string_mode_cc126_value"]
        with self.assertRaisesRegex(ValueError, "CC126 value 6"):
            self.inspect()

    def test_verified_attack_count_is_checked(self):
        self.manifest["renders"][0]["verified_explicit_attacks"] = 0
        with self.assertRaisesRegex(ValueError, "evidence count mismatch"):
            self.inspect()


if __name__ == "__main__":
    unittest.main()
