# Three-dance promo

Builds Acustra's Full HD promo, its three 9:16 shorts and their thumbnails,
after Aculyre's `Tools/promo/`: the Minuet in G, The Kesh and The Butterfly,
each arranged for four Acustras on factory patches and heard from a late
strain to its final chord. Rendered audio and video are review deliverables
and are never committed.

| File | Does |
| --- | --- |
| `performance.py` | the humanized `Performance` (tempo breathing, timing, velocity), copied from Aculyre's `write_preset_midi.py` |
| `repertoire.py` | writes the three performed pieces as MIDI, one file a player; each part names its patch, pan and level in the mix, and the docstrings say how each tune was re-voiced for guitars |
| `SongRender.cpp` | plays one part as the Rack device does (factory patch, its Rack-only settings, 64-frame batches, the Rack's `OutputRate` decimator and output ceiling, latency trimmed); writes the WAV and a 60 Hz trace of every string's level, state, fret and note (the engine's read-only `getStringActivity`) |
| `promo_audio.py` | `promo` mixes each excerpt from its players' renders (level and an energy-balanced pan only), matches the excerpts' loudness, joins and masters them to -16 LUFS; `short-minuet`, `short-kesh`, `short-butterfly` make one excerpt each, under 30 s; writes `out/<set>/timeline.json` |
| `promo.mjs` | draws the film (one six-string fretboard per guitar, lit by the trace) from `timeline.json` and muxes the audio (H.264 CRF 18, AAC 384 kbps); `--portrait` draws a 1080×1920 short |
| `thumbs.mjs` | three 3840×2160 promo thumbnail concepts and the shorts' 9:16 covers |

```sh
# from the repository root; <rack> is a reason-rack-extensions checkout, <w> a work directory
python3 Tools/promo/repertoire.py <w>/out > <w>/parts.tsv    # <part>.mid, patch, tempo
g++ -std=c++17 -O3 -DNDEBUG -I <rack>/Examples/Acustra Tools/promo/SongRender.cpp \
    Source/DSP/AcustraEngine.cpp Source/DSP/AcustraPerformer.cpp -o <w>/SongRender
cd <w>
while IFS=$'\t' read mid patch tempo; do
  ./SongRender "$patch" $mid ${mid%.mid}.wav ${mid%.mid}.csv --oversampling 4 --tempo $tempo
done < parts.tsv
python3 <repo>/Tools/promo/promo_audio.py promo     # and short-minuet, short-kesh, short-butterfly
node promo.mjs out/promo/timeline.json out/promo/promo.wav out/promo/promo.mp4
node promo.mjs out/short-kesh/timeline.json out/short-kesh/short-kesh.wav out/short-kesh/short.mp4 --portrait
node promo.mjs out/promo/timeline.json out/promo/promo.wav still --still 79   # the frame thumbs.mjs uses
node thumbs.mjs thumbs still-79.png
```

`SongRender` includes the Rack adapter's `OutputRate.h`, hence the `-I`. The
`.mjs` scripts run from the work directory (copy them there) and need
`@napi-rs/canvas`, Inter in `/usr/share/fonts/opentype/inter/`, and in
`../assets/`, from `<rack>/Examples/Acustra/Design/`: `Fonts/LibreBaskerville-wght.ttf`,
`Fonts/BarlowSemiCondensed-Bold.ttf`, `Brand/Protocodus-logo.png` as `logo.png`
and `Brand/Acustra-wordmark.png` as `wordmark.png`. `thumbs.mjs` also needs
the Codex hero art `landscape.png` and `portrait.png` (project files
`acustra/promo-2026-11-11/thumbs/codex-art/`, with their prompts).
`promo_audio.py` needs NumPy, SciPy, soundfile and pyloudnorm.
