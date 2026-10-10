#!/usr/bin/env python3
"""Render two frozen DSP revisions and package a gain-only matched blind audition.

Uses the same RenderRealismSongs.cpp with each verified revision's DSP sources.
Requires Git, a C++ compiler, ffmpeg, NumPy and SciPy; no downloaded music or effects.
Audio, generated pages and the ZIP stay in the requested ignored output folder.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
TITLES = {
    "01-evening-fingerpicking": ("Evening fingerpicking", "Alternating bass, overlapping upper voices and repeated melody notes."),
    "02-open-road-strumming": ("Open road strumming", "Accented chord brushes, soft rapid strokes and deliberate stops."),
    "03-connected-melody": ("Connected melody", "Explicit hammer/pull gestures on one string, then a pitch-wheel slide."),
    "04-repeated-note-groove": ("Repeated-note groove", "Repeated notes, thumb attacks, contrasting key-up firmness and palm contact."),
    "05-resonance-and-body-transitions": ("Resonance and body transitions", "Additional study: live retuning and same-sample body-choice changes while the instrument rings."),
}
def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()
def run(command: list[str]) -> str:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stderr[-4000:]}")
    return result.stdout
def revision_inputs(revision: str, source: Path, repository: Path) -> dict:
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Supply an exact 40-character Git commit")
    if run(["git", "-C", str(repository), "rev-parse", revision+"^{commit}"]).strip() != revision:
        raise ValueError("Revision is not available in the candidate repository")
    files = run(["git", "-C", str(repository), "ls-tree", "-r", "--name-only",
                 revision, "--", "Source/DSP"]).splitlines()
    actual = {str(path.relative_to(source)) for path in (source/"Source/DSP").rglob("*") if path.is_file()}
    if not files or actual != set(files):
        raise ValueError(f"DSP source inventory differs from commit: {source}")
    hashes = {}
    for name in files:
        expected = subprocess.run(["git", "-C", str(repository), "show", revision+":"+name],
                                  check=True, stdout=subprocess.PIPE).stdout
        current = (source/name).read_bytes()
        if current != expected:
            raise ValueError(f"DSP source differs from commit: {name}")
        hashes[name] = hashlib.sha256(current).hexdigest()
    return {"commit": revision, "source_sha256": hashes,
            "verification": "all DSP files byte-equal to the named Git commit; compiled directly for this render"}
def loudness(path: Path) -> dict[str, float]:
    result = subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-i", str(path),
        "-af", "loudnorm=I=-20:TP=-2:LRA=11:print_format=json", "-f", "null", "-"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr[-2000:])
    match = re.search(r'\{\s*"input_i".*?\}', result.stderr, re.S)
    if not match:
        raise RuntimeError("ffmpeg did not report measured input loudness")
    values = json.loads(match.group())
    measured = {"lufs": float(values["input_i"]), "true_peak_db": float(values["input_tp"])}
    if not all(math.isfinite(v) for v in measured.values()):
        raise RuntimeError(f"Silent/nonfinite loudness result: {path}")
    return measured
def read(path: Path) -> tuple[int, np.ndarray]:
    rate, pcm = wavfile.read(path)
    if rate != 48000 or pcm.ndim != 2 or pcm.shape[1] != 2:
        raise ValueError(f"Expected 48 kHz stereo: {path}")
    if np.issubdtype(pcm.dtype, np.integer):
        pcm = pcm.astype(np.float64) / (float(np.iinfo(pcm.dtype).max) + 1)
    else:
        pcm = pcm.astype(np.float64)
    if not np.isfinite(pcm).all() or not np.any(pcm):
        raise ValueError(f"Invalid/silent render: {path}")
    return rate, pcm
def match_pair(first: Path, second: Path, outputs: list[Path]) -> tuple[list[dict], dict]:
    sources = [first, second]
    measured = [loudness(path) for path in sources]
    # Choose ONE common loudness, lowered if either side needs peak headroom.
    # loudnorm is used for measurement only. The actual transform is one gain.
    target = min(-20.0, *(m["lufs"] - 2.5 - m["true_peak_db"] for m in measured))
    versions = []
    arrays = []
    for source, out, before in zip(sources, outputs, measured):
        rate, pcm = read(source)
        estimate = target - before["lufs"]
        # BS.1770 gating changes which quiet windows count after scaling, so
        # measured loudness jumps with gain (up to 0.16 LU on song 05) and a
        # fixed-point update can oscillate across the target. Take the
        # matching whole-file gain nearest the estimate instead.
        for step in sorted(range(-50, 51), key=abs):
            gain_db = estimate + 0.01 * step
            scaled = pcm * 10 ** (gain_db / 20)
            if np.max(np.abs(scaled)) >= 0.99:
                raise ValueError("Peak headroom calculation failed")
            wavfile.write(out, rate, np.rint(scaled * 32767).astype(np.int16))
            after = loudness(out)
            if abs(after["lufs"] - target) <= 0.05:
                break
        if abs(after["lufs"] - target) > 0.05 or after["true_peak_db"] > -1.85:
            raise ValueError(f"Invalid gain match: {after}, target {target}")
        versions.append({"file": "audio/" + out.name, **after, "gain_db": gain_db,
            "sha256": digest(out), "source_sha256": digest(source), "source_loudness": before})
        arrays.append(pcm)
    if arrays[0].shape != arrays[1].shape:
        raise ValueError("Paired durations differ")
    return versions, {"equal_source_pcm": bool(np.array_equal(*arrays)),
        "raw_difference_rms": float(np.sqrt(np.mean((arrays[1]-arrays[0])**2))),
        "loudness_delta_lu": versions[1]["lufs"] - versions[0]["lufs"],
        "matching": "whole-file scalar gain only; no EQ/compression/limiting/alignment"}
def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline-source", "candidate-source", "out"):
        p.add_argument("--"+name, type=Path, required=True)
    p.add_argument("--baseline-commit", required=True)
    p.add_argument("--candidate-commit", required=True)
    p.add_argument("--track", action="append", choices=list(TITLES),
        help="Package only selected songs as required passages; omit for the full review")
    p.add_argument("--reference-rows", type=Path,
        help="Optional verified Eastman rows.json for clearly separate real-note references")
    p.add_argument("--reference-evidence", type=Path,
        help="Frozen provenance JSON for these prepared rows: rows_sha256 and targets[{id,sha256}]")
    a = p.parse_args()
    repository = a.candidate_source.resolve()
    inputs = {label: revision_inputs(commit, source.resolve(), repository)
              for label, commit, source in (("baseline", a.baseline_commit, a.baseline_source),
                                            ("candidate", a.candidate_commit, a.candidate_source))}
    reference = json.loads(a.reference_rows.read_text()) if a.reference_rows else None
    reference_hashes = {}
    if reference:
        if not a.reference_evidence:
            p.error("--reference-rows requires --reference-evidence")
        published = json.loads(a.reference_evidence.read_text())
        if digest(a.reference_rows) != published["rows_sha256"]:
            raise ValueError("Reference row manifest differs from frozen provenance")
        reference_hashes = {row["id"]: row["sha256"] for row in published["targets"]}
        for technique in ("finger", "pick"):
            selected = next(row for row in reference["rows"]
                if row["picking"] == technique and row["midi"] == 64)
            target = a.reference_rows.parent / selected["target"]["path"]
            if digest(target) != reference_hashes.get(selected["id"]):
                raise ValueError(f"Reference PCM differs from frozen hash: {selected['id']}")
    out = a.out.resolve()
    if out.exists():
        raise SystemExit("Output must be new")
    out.mkdir(parents=True)
    audio_dir = out / "audio"
    audio_dir.mkdir()
    renderer_source = ROOT / "Tools/RenderRealismSongs.cpp"
    template = ROOT / "Tools/RealismListeningTemplate.html"
    raw = {}
    compiler = shutil.which("c++")
    if not compiler:
        raise ValueError("No C++ compiler")
    for label, source in (("baseline", a.baseline_source), ("candidate", a.candidate_source)):
        binary = out / f"render-{label}"
        source = source.resolve()
        command = [compiler, "-std=c++20", "-O3", "-DNDEBUG", "-I"+str(source/"Source"),
            str(renderer_source), str(source/"Source/DSP/AcustraEngine.cpp"),
            str(source/"Source/DSP/AcustraPerformer.cpp"), "-o", str(binary)]
        run(command)
        inputs[label].update({"compile_command": command, "renderer_binary_sha256": digest(binary)})
        for mode, room in (("dry", "0"), ("room", "0.5")):
            directory = out / f"raw-{label}-{mode}"
            run([str(binary), str(directory), room])
            raw[label, mode] = directory
    review = {"review_id": "acustra-"+a.baseline_commit[:10]+"-"+a.candidate_commit[:10],
        "baseline_commit": a.baseline_commit, "candidate_commit": a.candidate_commit,
        "renderer_sha256": digest(renderer_source), "tracks": [],
        "template_sha256": digest(template), "builder_sha256": digest(Path(__file__)),
        "inputs": inputs, "compiler": {"path": compiler, "sha256": digest(Path(compiler)),
            "version": run([compiler, "--version"]).strip()},
        "method": "identical original scores; 48 kHz; Room 0/0.5; scalar gain matching; no external processing"}
    if reference:
        review["reference_provenance"] = {"rows_sha256": digest(a.reference_rows),
            "frozen_evidence_sha256": digest(a.reference_evidence), "recorded_provenance": published}
    for number, (song, (title, description)) in enumerate(TITLES.items(), 1):
        if a.track and song not in a.track:
            continue
        item = {"id": song, "title": title, "description": description, "modes": {},
            "required": bool(a.track) or number <= 4}
        for mode in ("dry", "room"):
            first, second = (raw[label, mode]/(song+".wav") for label in ("baseline", "candidate"))
            scores = [raw[label, mode]/(song+".events") for label in ("baseline", "candidate")]
            if scores[0].read_bytes() != scores[1].read_bytes():
                raise ValueError("Performance inputs differ between revisions")
            item["events_sha256"] = digest(scores[0])
            item["duration"] = len(read(first)[1])/48000
            versions, comparison = match_pair(first, second,
                [audio_dir/f"{number:02d}-{mode}-{letter}.wav" for letter in ("x", "y")])
            item["modes"][mode] = {"versions": versions, "comparison": comparison}
        if reference:
            technique = "pick" if number == 2 else "finger"
            selected = next(row for row in reference["rows"]
                if row["picking"] == technique and row["midi"] == 64)
            spec = selected["target"]
            if spec["channels"] != 2 or spec["sample_rate"] != 48000:
                raise ValueError("Expected verified 48 kHz stereo reference")
            source = a.reference_rows.parent / spec["path"]
            if digest(source) != reference_hashes.get(selected["id"]):
                raise ValueError(f"Reference PCM differs from frozen hash: {selected['id']}")
            samples = np.fromfile(source, dtype="<f4").reshape(-1, 2)
            raw_reference = out/f"raw-reference-{number:02d}.wav"
            wavfile.write(raw_reference, 48000, samples)
            destinations = [audio_dir/f"reference-{number:02d}.wav", out/f"unused-reference-{number:02d}.wav"]
            measured, _ = match_pair(raw_reference, raw_reference, destinations)
            destinations[1].unlink()
            item["optional_reference"] = {**measured[0],
                "title": f"Real Eastman E1D: {'picked' if technique == 'pick' else 'fingerpicked'} E4",
                "source": reference["source"], "license": "CC0 1.0",
                "note": "Different instrument and isolated performance; context, not the same demo song.",
                "corpus_row": selected["id"], "source_sha256": digest(source)}
        review["tracks"].append(item)
    # Changed scores/audio/provenance must never inherit an older approval.
    fingerprint = hashlib.sha256(json.dumps(review, sort_keys=True).encode()).hexdigest()
    review["review_id"] += "-" + fingerprint[:20]
    review["package_content_sha256"] = fingerprint
    # The score is original and authored here; real references are scored by
    # the separate corpus benchmark, not misrepresented as these same songs.
    (out/"manifest.json").write_text(json.dumps(review, indent=2)+"\n")
    text = template.read_text()
    if text.count("__REVIEW_DATA__") != 1:
        raise ValueError("Invalid listening template placeholder")
    embedded = json.dumps(review).replace("<", "\\u003c")
    (out/"index.html").write_text(text.replace("__REVIEW_DATA__", embedded))
    (out/"README.txt").write_text(
        "Acustra blind A/B approval test\n\nOpen index.html in a browser. For seamless Web Audio switching, "
        "serve this folder with: python3 -m http.server 8000\nThen open it in your browser. Direct file "
        "opening uses the synchronized media-element fallback.\n\nUse headphones. Vote on each of the "
        "required songs, then reveal the revisions and record your approval. Other studies are optional. "
        "Export your JSON decisions and send them back with your review.\n\nRealism remains a listening "
        "judgment; engineering regression passes and descriptor losses are separate evidence.\n")
    with zipfile.ZipFile(out/"Acustra-AB-listening-test.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in [out/"index.html", out/"manifest.json", out/"README.txt", *audio_dir.glob("*.wav")]:
            archive.write(path, path.relative_to(out))
    print(json.dumps({"index": str(out/"index.html"), "zip": str(out/"Acustra-AB-listening-test.zip"),
        "pairs": len(review["tracks"])*2, "checks": "equal scores, finite audio, true peaks, LUFS matching, source hashes"}))
if __name__ == "__main__":
    main()
