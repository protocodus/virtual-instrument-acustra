#!/usr/bin/env python3
"""Check that recording-calibration controls actually reach the renderer."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    renderer = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="acustra-calibration-options-") as temporary:
        root = Path(temporary)
        events = root / "note.events"
        events.write_text("ACUSTRA_PERFORMANCE_V1 48000 24000\n0 3 55 91 0\n")
        serial = 0

        def render(*options, valid=True):
            nonlocal serial
            output = root / f"{serial}.f32"
            serial += 1
            result = subprocess.run([str(renderer), str(events), str(output), *options],
                                    capture_output=True, text=True)
            if valid:
                assert result.returncode == 0, result.stderr
                audio = output.read_bytes()
                assert len(audio) == 24000 * 8 and any(audio)
                return audio
            assert result.returncode != 0 and not output.exists(), result.stderr

        default = render()
        assert default == render("--body-shape", "dreadnought", "--body-material", "spruce")
        shapes = [render("--body-shape", shape)
                  for shape in ("parlor", "auditorium", "dreadnought", "jumbo")]
        assert len(set(shapes)) == 4, "body-shape option did not reach the engine"
        assert default != render("--body-material", "cedar")
        calibration = root / "calibration.txt"
        render("--calibration", str(root), valid=False)
        calibration.write_text("")
        assert default == render("--calibration", str(calibration))
        nylon = render("--string-material", "nylon")
        calibration.write_text("nylon.apertureScale 2.4\n")
        assert default == render("--calibration", str(calibration))
        assert nylon != render("--string-material", "nylon", "--calibration", str(calibration))
        for content in ("unknown 1\n", "bodyQScale nan\n", "bodyQScale 999\n", "nylon.apertureScale -1\n",
                        "bodyQScale 1\nbodyQScale 1.2\n",
                        "bodyQScale\n", "bodyQScale 1 trailing\n"):
            calibration.write_text(content)
            render("--calibration", str(calibration), valid=False)
        for options in (("--body-shape", "unknown"), ("--body-material", "unknown"),
                        ("--body-shape",), ("--body-shape", "parlor", "--body-shape", "jumbo")):
            render(*options, valid=False)
    print("Calibration renderer: defaults preserved, model controls effective, malformed input rejected")


if __name__ == "__main__":
    main()
