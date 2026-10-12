# eastman corpus source

Work: two first-party performance masters of an Eastman E1D dreadnought
steel-string guitar (chromatic walks E2 to C6, recorded 2026-07-23).
Creator/performer: Arthur, owner of the ferrosintesis repository.

Source: <https://github.com/0x4D44/ferrosintesis/tree/810318c92e33e31b36638b0ffa7ffc834a2ae6a2/samples/acoustic-guitar-eastman-e1d>
(introduction commit; Acustra's THIRD_PARTY_NOTICES.md also checked revision
94edbcfef226986d6ac28330020bc301fa5207d9, where both Opus hashes are unchanged).

Licence: CC0 1.0 Universal, <https://creativecommons.org/publicdomain/zero/1.0/>. Exact statement in the
upstream sample README (ThirdParty/Eastman-E1D-README.md in Acustra):
"Licence | **CC0-1.0** (public-domain dedication by the repo owner)".
Each Opus file also carries the metadata tag LICENSE=CC0-1.0.

| File | Master | Bytes | SHA-256 |
|---|---|---|---|
| picked.opus | DR0000_0192.wav | 5957761 | `35e9e45b42a70f2fada2c9d93bf809d9046562fa3fa40cbac415ef75b92d926d` |
| plucked.opus | DR0000_0191.wav | 5945086 | `16f5a8c7cde555441143243f264a03eff142cb8aafb6924236a82755a0396ce0` |

Both hashes were verified against THIRD_PARTY_NOTICES.md ('Eastman E1D
steel-string recordings') before decoding.

Processing: Tools/PrepareEastmanCorpus.py (protocol in its docstring). Whole
takes are decoded once by ffmpeg to 48 kHz stereo float32; each target starts
20 ms before an isolated onset located by FitPhysicalModel.py's own onset rule
and ends at the next event, a detected mute or 4.2 s, with a 60 ms terminal
half-cosine fade and no gain change. analysis.json lists every detected
event with its measurements and rejection reason.

| Take | Split | Rows | Tuning offset (cents) | Noise floor (dBFS, 100 ms RMS p5) |
|---|---|---|---|---|
| picked | flattop-pick | 49 | +3.5 | -60.3 |
| plucked | flattop-finger-all | 55 | +3.4 | -60.8 |

No audio from this corpus is committed to the Acustra repository.
