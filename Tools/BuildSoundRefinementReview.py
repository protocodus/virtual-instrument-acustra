#!/usr/bin/env python3
"""Freeze DSP sources and build a balanced, blind, local A/B listening pack.

python3 Tools/BuildSoundRefinementReview.py BASELINE CANDIDATE NEW_OUTPUT

BASELINE/CANDIDATE may be repository roots, Source directories, or DSP directories.
Requires g++ and NumPy. Does not fetch, install, or modify either input tree.
The source snapshots, tools, build logs, exact event scores, native float WAVs,
24-bit audition WAVs and provenance remain in NEW_OUTPUT. Serve the complete
directory over HTTP, or extract the full portable pack before opening index.html.
"""

import argparse
import hashlib
import html
import json
import math
import platform
import random
import secrets
import shutil
import struct
import subprocess
import sys
import wave
from datetime import datetime, timezone
from pathlib import Path

import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def dsp_directory(source):
    for path in (source / "Source/DSP", source / "DSP", source):
        if (path / "AcustraEngine.cpp").is_file() and (path / "AcustraPerformer.cpp").is_file():
            return path
    raise ValueError(f"Cannot find DSP sources in {source}")


def read_float_wave(path):
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE" or len(data) != struct.unpack_from("<I", data, 4)[0] + 8:
        raise ValueError(f"Invalid WAV container: {path}")
    offset, fmt, payload = 12, None, None
    while offset + 8 <= len(data):
        name, size = struct.unpack_from("<4sI", data, offset)
        chunk = data[offset + 8:offset + 8 + size]
        if len(chunk) != size:
            raise ValueError(f"Truncated WAV: {path}")
        if name == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", chunk)
        if name == b"data":
            payload = chunk
        offset += 8 + size + (size & 1)
    if fmt != (3, 2, 48000, 384000, 8, 32) or payload is None:
        raise ValueError(f"Expected stereo 48 kHz float WAV: {path}")
    audio = np.frombuffer(payload, dtype="<f4").reshape(-1, 2).astype(np.float64)
    if audio.shape != (480000, 2) or not np.isfinite(audio).all():
        raise ValueError(f"Wrong length or nonfinite audio: {path}")
    return audio


def write_pcm24(path, audio):
    if not np.isfinite(audio).all() or np.max(np.abs(audio)) > 1:
        raise ValueError(f"Unsafe audition audio: {path}")
    # Deterministic 24-bit quantization; no independent dither/noise obscures a
    # null comparison. At -144 dBFS the quantization step is below this task.
    samples = np.rint(audio.reshape(-1) * 8388607).astype(np.int32)
    packed = np.column_stack((samples & 255, (samples >> 8) & 255, (samples >> 16) & 255)).astype(np.uint8)
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(3)
        stream.setframerate(48000)
        stream.writeframes(packed.tobytes())


def db(value):
    return 20 * math.log10(value)


STYLE = """body{max-width:980px;margin:36px auto;padding:0 22px 60px;background:#151a18;color:#eee9df;font:17px/1.55 system-ui}h1{font-size:38px;line-height:1.15}h2{font-size:23px}p,.muted{color:#c8cec6}section{padding:22px 0;border-top:1px solid #47534a}button,select{font:inherit;border:1px solid #86978b;background:#25332b;color:#f4f2e9;border-radius:7px;padding:8px 14px;cursor:pointer}button[aria-pressed=true]{background:#d3dfba;color:#132014}button:focus-visible,a:focus-visible,input:focus-visible{outline:3px solid #f4d77e;outline-offset:3px}a{color:#d8dfa5}label{display:inline-block;margin:6px 18px 6px 0}.transport,.votes{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.transport{margin:14px 0}input[type=range]{width:240px;max-width:75vw}textarea{display:block;width:95%;min-height:55px;font:inherit;background:#1e2922;color:#fff;border:1px solid #617468;border-radius:6px;padding:8px;margin:12px 0}.time{font-variant-numeric:tabular-nums}.error{color:#ffc9b9}.small{font-size:14px}audio{max-width:100%}details{margin:18px 0}iframe{width:100%;height:680px;border:1px solid #728b79;margin-top:15px}table{border-collapse:collapse;width:100%}td,th{text-align:left;border-bottom:1px solid #47534a;padding:8px}"""


def build_page(output, pairs, pack_id):
    rows = []
    for n, pair in enumerate(pairs):
        stem = pair["scene"]["name"]
        title = html.escape(pair["scene"]["title"])
        description = html.escape(pair["scene"]["description"])
        rows.append(f'''<section data-scene="{stem}" data-index="{n}">
<h2>{n + 1}. {title}</h2><p>{description}</p>
<div class="transport"><button class="play">Play</button><button class="restart">Restart</button>
<button class="side" data-side="A" aria-pressed="true">A</button><button class="side" data-side="B" aria-pressed="false">B</button>
<input class="seek" type="range" min="0" max="10" step="0.01" value="0" aria-label="Position in {title}">
<span class="time">0.0 / 10 s</span></div>
<label>Audition level <select class="mode"><option value="matched">RMS matched</option><option value="native">Native balance</option></select></label>
<p class="small muted status" role="status">Paused · A · RMS matched</p>
<div class="votes" role="group" aria-label="Preference for {title}"><span>I prefer:</span>
<button class="vote" data-vote="A" aria-pressed="false">A</button><button class="vote" data-vote="B" aria-pressed="false">B</button>
<button class="vote" data-vote="no_preference" aria-pressed="false">No preference</button><button class="vote" data-vote="unsure" aria-pressed="false">Unsure</button></div>
<textarea class="notes" aria-label="Notes for {title}" placeholder="Optional listening notes"></textarea>
<details><summary>Direct audio and sequential comparison</summary>
<p><a href="audio/{stem}-A.wav">Matched A WAV</a> · <a href="audio/{stem}-B.wav">Matched B WAV</a> ·
<a href="audio/{stem}-A-then-B.wav">Matched A then B WAV</a> · <a href="audio/{stem}-native-A-then-B.wav">Native balance A then B WAV</a></p>
<audio controls preload="none" src="audio/{stem}-A-then-B.wav" aria-label="A then B: {title}"></audio></details></section>''')
    page = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Acustra · Sound refinement A/B</title><style>''' + STYLE + '''</style></head><body>
<h1>Acustra · Sound refinement A/B</h1>
<p>Identical-score comparisons. Listen for clean attacks, natural sustain, smooth releases and a sound that sits comfortably in a mix. The first eight pairs compare the complete baseline and candidate. Any additional pairs isolate the named refinement.</p>
<p>A/B identities are randomized and exactly balanced across the scenes. Choose your preferences before revealing the key. A measured difference is not proof of a preference; no listening verdict is assumed.</p>
<p>Start a scene, then switch A/B at the same position. Switching uses browser media time and may introduce a short playback gap. RMS matching applies one fixed gain to each complete clip, including its silence and tail, then shared headroom to the pair. It is <strong>not LUFS matching</strong>. Native balance keeps the original relative levels with shared attenuation only. Neither mode uses EQ, compression, limiting or time alignment.</p>
<p class="small">48 kHz stereo · 10 seconds per side · 24-bit audition WAVs · original 32-bit float files retained. Sequential files play A, 0.75 seconds of silence, then B. This is an authored development score, not an independent real-guitar benchmark.</p>
<p><button id="export">Export votes and notes</button> <span id="export-status" role="status"></span></p>
''' + "".join(rows) + '''
<section><h2>Reveal and provenance</h2><p>The files below disclose identities. The manifest records source hashes, compiler identity and flags, exact controls and MIDI events, level gains and audio hashes. Original source snapshots and raw renders are retained in the research workspace.</p>
<button id="reveal">Reveal A/B key</button><div id="key"></div>
<p><a href="manifest.json">Full manifest (unblinds)</a> · <a href="key.json">Machine-readable key (unblinds)</a> · <a href="scores.json">Exact event scores and controls</a></p></section>
<script>
const packId = ''' + json.dumps(pack_id) + ''';
const states = [], answers = {};
let active = null;
function refresh(s) {
  s.play.textContent = s.loading ? 'Cancel' : s.playing ? 'Pause' : 'Play';
  s.status.textContent = `${s.loading ? 'Loading audio…' : s.playing ? 'Playing' : 'Paused'} · ${s.side} · ${s.mode === 'matched' ? 'RMS matched' : 'Native balance'}`;
  s.el.querySelectorAll('.side').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.side === s.side)));
  s.seek.value = s.position;
  s.el.querySelector('.time').textContent = `${s.position.toFixed(1)} / 10 s`;
}
function stop(s) {
  if (s.playing && !s.loading && s.audio.readyState >= 1) s.position = s.audio.currentTime;
  s.request++; s.audio.pause(); s.playing = false; s.loading = false; refresh(s);
}
function setPosition(s, position) {
  s.position = Math.max(0, Math.min(position, 10));
  if (s.audio.readyState >= 1) s.audio.currentTime = s.position;
  refresh(s);
}
function loadMedia(a) {
  if (a.readyState >= 1) return Promise.resolve();
  if (a.preparing) return a.preparing;
  a.preparing = (async () => {
    // A complete local Blob supports seeking even when the host ignores Range.
    // file:// cannot use fetch in many browsers; extracted packs use media URLs.
    if (!a.src) {
      if (location.protocol === 'file:') a.src = a.assetUrl;
      else {
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 60000);
        try {
          const response = await fetch(a.assetUrl, {signal:controller.signal});
          if (!response.ok) throw new Error(`Audio download returned HTTP ${response.status}.`);
          const data = await response.blob();
          // Some preview routes return the HTML page for a missing asset with 200.
          const header = new Uint8Array(await data.slice(0, 12).arrayBuffer());
          const tag = offset => String.fromCharCode(...header.slice(offset, offset + 4));
          if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') throw new Error('The audio address did not return a WAV file.');
          a.blobUrl = URL.createObjectURL(new Blob([data], {type:'audio/wav'}));
          a.src = a.blobUrl;
        } finally { clearTimeout(timeout); }
      }
    }
    await new Promise((resolve, reject) => {
      const cleanup = () => { clearTimeout(timeout); a.removeEventListener('loadedmetadata', ready); a.removeEventListener('error', failed); };
      const ready = () => { cleanup(); resolve(); };
      const failed = () => { cleanup(); reject(new Error(location.protocol === 'file:'
        ? 'Open index.html from the fully extracted ZIP, with its audio folder beside it.'
        : 'The browser could not read this WAV file.')); };
      const timeout = setTimeout(() => { cleanup(); reject(new Error('Audio loading timed out. Press Play to retry.')); }, 60000);
      a.addEventListener('loadedmetadata', ready, {once:true});
      a.addEventListener('error', failed, {once:true});
      a.load();
      if (a.readyState >= 1) ready();
    });
  })().catch(error => {
    a.preparing = null;
    a.removeAttribute('src');
    if (a.blobUrl) { URL.revokeObjectURL(a.blobUrl); a.blobUrl = null; }
    throw error;
  });
  return a.preparing;
}
async function play(s) {
  if (active && active !== s) stop(active);
  document.querySelectorAll('audio').forEach(a => a.pause());
  active = s;
  const request = ++s.request, a = s.audio;
  s.playing = true; s.loading = a.readyState < 1; refresh(s);
  try {
    await loadMedia(a);
    if (request !== s.request) return;
    a.currentTime = s.position >= 10 ? 0 : s.position;
    s.position = a.currentTime;
    await a.play();
    if (request !== s.request) return;
    s.loading = false; refresh(s);
  } catch (error) {
    if (request !== s.request) return;
    s.playing = false; s.loading = false; refresh(s);
    s.status.textContent = error.name === 'NotAllowedError'
      ? 'Audio is ready. Press Play again to allow playback.'
      : `Audio could not play. ${error.message} Open the hosted comparison or extract the full ZIP; the page needs its audio files.`;
  }
}
function switchSource(s, side, mode) {
  if (s.side === side && s.mode === mode) return;
  const resume = s.playing;
  if (s.playing && !s.loading && s.audio.readyState >= 1) s.position = Math.min(s.audio.currentTime || 0, 10);
  s.request++; s.audio.pause(); s.side = side; s.mode = mode;
  s.audio = s.players[`${mode}-${side}`]; s.loading = false;
  refresh(s); if (resume) play(s);
}
document.querySelectorAll('section[data-scene]').forEach(el => {
  const scene = el.dataset.scene;
  const s = {el, scene, side:'A', mode:'matched', playing:false, loading:false, position:0, request:0, players:{}, play:el.querySelector('.play'), status:el.querySelector('.status'), seek:el.querySelector('.seek')};
  for (const mode of ['matched','native']) for (const side of ['A','B']) {
    const a = new Audio();
    a.assetUrl = new URL(`audio/${scene}-${mode === 'native' ? 'native-' : ''}${side}.wav`, document.baseURI).href;
    a.preload = 'none'; s.players[`${mode}-${side}`] = a;
    a.addEventListener('ended', () => { if (s.audio === a) { s.position = 10; s.playing = false; s.loading = false; refresh(s); } });
    a.addEventListener('timeupdate', () => { if (s.audio === a && !s.loading && s.playing) { s.position = a.currentTime; refresh(s); } });
  }
  s.audio = s.players['matched-A']; states.push(s); answers[scene] = {preference:null, notes:'', last_mode:'matched'};
  s.play.onclick = () => { if (s.playing) stop(s); else play(s); };
  el.querySelector('.restart').onclick = () => setPosition(s, 0);
  el.querySelectorAll('.side').forEach(b => b.onclick = () => switchSource(s, b.dataset.side, s.mode));
  el.querySelector('.mode').onchange = e => { switchSource(s, s.side, e.target.value); answers[scene].last_mode = e.target.value; };
  s.seek.oninput = () => setPosition(s, Number(s.seek.value));
  el.querySelectorAll('.vote').forEach(b => b.onclick = () => {
    answers[scene].preference = b.dataset.vote; answers[scene].mode_at_vote = s.mode;
    el.querySelectorAll('.vote').forEach(v => v.setAttribute('aria-pressed', String(v === b)));
  });
  el.querySelector('.notes').oninput = e => { answers[scene].notes = e.target.value; };
});
document.querySelectorAll('audio').forEach(a => a.addEventListener('play', () => {
  states.forEach(stop); document.querySelectorAll('audio').forEach(other => { if (other !== a) other.pause(); });
}));
let revealed = false;
document.getElementById('reveal').onclick = () => {
  revealed = true; document.getElementById('key').innerHTML = '<iframe title="A/B identity key" src="key.html"></iframe>';
  document.getElementById('reveal').disabled = true;
};
document.getElementById('export').onclick = () => {
  const content = JSON.stringify({pack_id:packId, exported_utc:new Date().toISOString(), key_revealed:revealed, answers}, null, 2);
  const url = URL.createObjectURL(new Blob([content], {type:'application/json'}));
  const a = document.createElement('a'); a.href = url; a.download = 'acustra-listening-votes.json'; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  document.getElementById('export-status').textContent = 'Votes exported. Notes stay in this page until it is closed or reloaded.';
};
</script></body></html>'''
    (output / "index.html").write_text(page, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, help="Optional reproducible blind randomization seed; recorded only in key.json")
    parser.add_argument("--isolated-refinements", action="store_true", help="Verify candidate all-off parity and add isolated pick/room study pairs (requires candidate offline switches)")
    args = parser.parse_args()
    sources = {"baseline": dsp_directory(args.baseline.resolve()), "candidate": dsp_directory(args.candidate.resolve())}
    output = args.output.resolve()
    for source in sources.values():
        if output == source or source in output.parents:
            raise ValueError("Output must not be inside an input DSP tree")
    compiler = shutil.which("g++")
    if compiler is None:
        raise ValueError("g++ is required")
    renderer = Path(__file__).resolve().with_name("RenderSoundRefinementReview.cpp")
    if not renderer.is_file():
        raise ValueError("Renderer must be beside this builder")
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(renderer, output / renderer.name)
    shutil.copy2(Path(__file__).resolve(), output / Path(__file__).name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG"]
    manifest = {
        "protocol": "acustra-sound-refinement-20261015-v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "sample_rate": 48000, "duration_seconds": 10,
        "compiler": {"path": compiler, "sha256": sha(Path(compiler).resolve()),
                     "version": subprocess.check_output([compiler, "--version"], text=True),
                     "flags": flags,
                     "flags_sha256": hashlib.sha256(json.dumps(flags, separators=(",", ":")).encode()).hexdigest()},
        "host": platform.platform(), "python": sys.version, "numpy": np.__version__,
        "tools": {renderer.name: sha(output / renderer.name), Path(__file__).name: sha(output / Path(__file__).name)},
        "source_freeze": "Actual on-disk DSP files; SampleBank reference corpus excluded (not compiled or included by this renderer).",
        "audition_protocol": {"normalization": "whole-clip stereo RMS, including silence and release tail; not LUFS",
                              "matching": "geometric-mean RMS target; one constant per clip, followed by one shared pair headroom factor",
                              "native_balance": "shared attenuation only; original relative levels",
                              "peak_ceiling_dbfs": -1.0, "format": "stereo PCM24, no dither",
                              "sequential_gap_seconds": 0.75,
                              "alignment": "none; original score timing and native pipeline latency preserved"},
        "sources": {}, "pairs": [],
    }
    # Freeze both trees before any compilation/rendering, and verify no source
    # changed during copying. This catches concurrent edits instead of mixing revisions.
    for label, source in sources.items():
        def input_hashes():
            return {str(p.relative_to(source)): sha(p) for p in sorted(source.rglob("*"))
                    if p.is_file() and "SampleBank" not in p.relative_to(source).parts}
        before = input_hashes()
        frozen = output / f"{label}-source/Source/DSP"
        shutil.copytree(source, frozen, ignore=shutil.ignore_patterns("SampleBank"))
        after = input_hashes()
        copied = {str(p.relative_to(frozen)): sha(p) for p in sorted(frozen.rglob("*")) if p.is_file()}
        if before != after or before != copied:
            raise RuntimeError(f"{label} DSP source changed during freeze; use a new output after edits settle")
        manifest["sources"][label] = {"input": str(source), "files": copied}
    for label in sources:
        command = [compiler, *flags, f"-I{label}-source/Source", renderer.name,
                   f"{label}-source/Source/DSP/AcustraEngine.cpp",
                   f"{label}-source/Source/DSP/AcustraPerformer.cpp", "-o", f"{label}-renderer"]
        manifest["sources"][label]["compile_command"] = command
        with (output / f"{label}-build.log").open("w") as log:
            subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT, check=True)
        manifest["sources"][label]["binary_sha256"] = sha(output / f"{label}-renderer")
        subprocess.run([str(output / f"{label}-renderer"), str(output / f"{label}-raw")], check=True)
    if (output / "baseline-raw/scores.json").read_bytes() != (output / "candidate-raw/scores.json").read_bytes():
        raise ValueError("Rendered event scores or controls differ between variants")
    shutil.copy2(output / "baseline-raw/scores.json", output / "scores.json")
    manifest["scores_sha256"] = sha(output / "scores.json")
    scores = json.loads((output / "scores.json").read_text())["scenes"]
    if len(scores) != 8:
        raise ValueError("Expected eight main scenes")
    scene_paths = {scene["name"]: {label: output / f"{label}-raw/{scene['name']}.wav" for label in sources} for scene in scores}
    if args.isolated_refinements:
        legacy_dir = output / "candidate-all-off-raw"
        subprocess.run([str(output / "candidate-renderer"), str(legacy_dir), "--legacy-refinements"], check=True)
        parity = {scene["name"]: (output / f"baseline-raw/{scene['name']}.wav").read_bytes()
                  == (legacy_dir / f"{scene['name']}.wav").read_bytes() for scene in scores}
        manifest["candidate_all_off_parity"] = parity
        if not all(parity.values()):
            raise ValueError("Candidate all-off audio differs from baseline; inspect sources before attributing isolated refinements")
        manifest["ablation_renders"] = []
        for feature in ("pick", "room"):
            paths = {}
            isolated_scene = None
            for label, mode in (("baseline", "--legacy-refinements"), ("candidate", f"--only-{feature}")):
                destination = output / f"isolated-{feature}-{label}-raw"
                command = [str(output / "candidate-renderer"), str(destination), mode, f"--study-{feature}"]
                subprocess.run(command, check=True)
                metadata = json.loads((destination / "scores.json").read_text())
                current_scene = metadata["scenes"][0]
                if isolated_scene is not None and isolated_scene != current_scene:
                    raise ValueError(f"Isolated {feature} event scores or controls differ")
                isolated_scene = current_scene
                paths[label] = destination / f"{current_scene['name']}.wav"
                manifest["ablation_renders"].append({"feature": feature, "side_identity": label,
                    "renderer": "candidate-renderer", "flags": [mode, f"--study-{feature}"],
                    "scores_file": str((destination / "scores.json").relative_to(output)),
                    "scores_sha256": sha(destination / "scores.json")})
            scores.append(isolated_scene)
            scene_paths[isolated_scene["name"]] = paths
    if len(scores) % 2:
        raise ValueError("Exact balancing requires an even number of pairs")
    seed = args.seed if args.seed is not None else secrets.randbits(128)
    half = len(scores) // 2
    assignments = ["baseline"] * half + ["candidate"] * half
    random.Random(seed).shuffle(assignments)
    key = {"seed": seed, "assignment_method": f"Random shuffle of {half} baseline-A and {half} candidate-A identities", "scenes": {}}
    audio_dir = output / "audio"
    audio_dir.mkdir()
    key_rows = []
    ceiling = 10 ** (-1 / 20)
    for scene, side_a in zip(scores, assignments):
        stem = scene["name"]
        paths = scene_paths[stem]
        arrays = {label: read_float_wave(paths[label]) for label in sources}
        peak = {label: float(np.max(np.abs(audio))) for label, audio in arrays.items()}
        rms = {label: float(np.sqrt(np.mean(audio * audio))) for label, audio in arrays.items()}
        if min(rms.values()) < 1e-8:
            raise ValueError(f"Silent scene: {stem}")
        target = math.sqrt(rms["baseline"] * rms["candidate"])
        matching = {label: target / level for label, level in rms.items()}
        shared = min(1.0, ceiling / max(peak[label] * matching[label] for label in sources))
        native_shared = min(1.0, ceiling / max(peak.values()))
        final_gain = {label: gain * shared for label, gain in matching.items()}
        identity = {"A": side_a, "B": "candidate" if side_a == "baseline" else "baseline"}
        key["scenes"][stem] = identity
        files = {}
        for mode in ("matched", "native"):
            prefix = "" if mode == "matched" else "native-"
            processed = {side: arrays[label] * (final_gain[label] if mode == "matched" else native_shared)
                         for side, label in identity.items()}
            for side in ("A", "B"):
                filename = f"{stem}-{prefix}{side}.wav"
                write_pcm24(audio_dir / filename, processed[side])
                files[f"{mode}_{side}"] = {"file": "audio/" + filename, "sha256": sha(audio_dir / filename)}
            filename = f"{stem}-{prefix}A-then-B.wav"
            write_pcm24(audio_dir / filename, np.concatenate((processed["A"], np.zeros((36000, 2)), processed["B"])))
            files[f"{mode}_A_then_B"] = {"file": "audio/" + filename, "sha256": sha(audio_dir / filename)}
        delta = arrays["candidate"] - arrays["baseline"]
        pair = {"scene": scene, "identity": identity, "files": files,
                "raw_files": {label: str(paths[label].relative_to(output)) for label in sources},
                "raw_sha256": {label: sha(paths[label]) for label in sources},
                "native_peak": peak, "native_rms": rms,
                "native_rms_change_db": db(rms["candidate"] / rms["baseline"]),
                "rms_match_gain": matching, "shared_headroom_gain": shared,
                "final_matched_gain": final_gain, "final_matched_gain_db": {label: db(gain) for label, gain in final_gain.items()},
                "native_shared_gain": native_shared,
                "sample_difference_rms": float(np.sqrt(np.mean(delta * delta))),
                "sample_difference_peak": float(np.max(np.abs(delta))),
                "identical_samples": bool(np.array_equal(arrays["baseline"], arrays["candidate"]))}
        manifest["pairs"].append(pair)
        key_rows.append(f'<tr><td>{html.escape(scene["title"])}</td><td>{identity["A"]}</td><td>{identity["B"]}</td></tr>')
    # Votes must identify this randomization too: the same source/audio rebuilt
    # with a fresh blind draw must not share a vote-export ID with another key.
    pack_id = hashlib.sha256(json.dumps({"sources": manifest["sources"], "tools": manifest["tools"],
        "scores": manifest["scores_sha256"], "blind_key": key}, sort_keys=True).encode()).hexdigest()
    manifest["pack_id"] = key["pack_id"] = pack_id
    write_json(output / "manifest.json", manifest)
    write_json(output / "key.json", key)
    (output / "key.html").write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>Acustra A/B key</title><style>' + STYLE + '</style><h1>A/B identity key</h1><p>For the first eight scenes, baseline is the pre-change frozen source and candidate is the proposed implementation. For isolated studies, baseline is the candidate with both refinements off; candidate enables only the named refinement. One preference per pair from one listener is exploratory evidence, not a statistical result.</p><table><tr><th>Scene</th><th>A</th><th>B</th></tr>' + ''.join(key_rows) + '</table></html>', encoding="utf-8")
    build_page(output, manifest["pairs"], pack_id)
    print(json.dumps({"index": str(output / "index.html"), "manifest": str(output / "manifest.json"),
                      "pairs": len(scores), "identical_pairs": sum(p["identical_samples"] for p in manifest["pairs"]),
                      "pack_id": pack_id}, indent=2))


if __name__ == "__main__":
    main()
