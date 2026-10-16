# Acustra realism work, 2026-10-16

Baseline: canonical `origin/main` at
`7b2b4c2659952f32560133a7eab21e48ae5779c1`. Work is on
`codex/acustra-realism-20261016`. The original source was frozen before edits
under `out/realism-20261016/baseline`; its hashes are in `baseline.json`.

This pass targets seven production changes to an already substantial physical
model. It does not count the existing dispersion, coupled body, finite pick
contact, release noise, sympathetic strings or playing-physics pack as new.
The research notes distinguish measured evidence, numerical consequences and
authored synthesis choices. Implementation is frozen in this development branch;
no release, merge, host installation or listener preference is implied.

| Candidate | Baseline gap | Evidence and scope | Offline switch |
| --- | --- | --- | --- |
| Hammer-on finger work | Contact energy is limited to a fraction of the remaining string energy, so a late hammer becomes silent. | External finger power is independent of residual string vibration; contact magnitude remains an authored mapping. | `hammerWork` |
| Pull-off pluck | Descending articulation uses the same normal-plane impact as an ascending hammer. | A fretting finger plucks the departing fret; preserve the old waves, add finite lateral work and correct travel on the longer target string. | `pullOffPluck` |
| Finger-pair timing | Two same-sample Finger notes sound exactly together, unlike the existing three/four-finger timing. | Existing GuitarSet analysis gives a timing scale, but cannot identify simultaneous intent. This remains a declared performance choice. | `fingerPairs` |
| Winding crossings | Slide roughness has no speed-dependent periodic winding structure. | Ridge rate follows distance traveled divided by winding pitch. Effective pitches and mix share are priors. | `woundRidges` |
| Directional early reflections | Both microphones receive each reflection at the same time with only scalar panning. | Image-source path lengths and first-order receiver patterns for an explicitly chosen room/microphone arrangement. | `directionalRoom` |
| Room conversion correction | The same internal room is unintentionally darker at a higher host rate. | Measured full-path loss at 96 vs 48 kHz: 1.179 dB at 8 kHz and 2.690 dB at 12 kHz. Correct conversion filters and account for wet delay. | `roomRateConversion` |
| Held-shape muting | A skipped string's damping expires on a timer while the chord is still held, allowing unwanted sympathetic ringing. | Reuse the existing measured dead-stroke loss; associate it with the physical strings of the held chord. The ownership rule is an authored interpretation of MIDI intent. | `heldShapeMute` |

An eighth candidate, `longitudinalSqueak`, implements a reduced axial-force
observer with declared boundary, damping and saddle-angle priors. It is
**disabled by default**: its contribution remains too faint to establish an
audible benefit, and its acoustic coupling is uncalibrated. It remains explicitly
available in two experimental A/B scenes. This is not counted among the seven
production changes. No additional noise gain was applied to make it pass.

## Research

- [Performer audit](research-realism-2026-10-16/performer.md): code locations,
  quiet-string baseline measurements and explicit MIDI triggers.
- [Wound-string audit](research-realism-2026-10-16/strings.md): distinct winding
  and axial mechanisms, boundaries, aliasing, lifetime and audibility limits.
- [Body/capture audit](research-realism-2026-10-16/body-capture.md): reflection
  geometry, numerical conversion error, and deferred fret-end coupling.
- [Held-shape mute audit](research-realism-2026-10-16/held-shape-mute.md): continued
  damping, physical-string ownership and the audible sympathetic-ring witness.

The main physical references are [Bilbao et al., DAFx 2024](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_50.pdf)
for external finger power, [Perlak's Berklee technique explanation](https://online.berklee.edu/takenote/guitar-techniques-hammer-ons-pull-offs-and-slides/)
for the distinct gestures, [Pakarinen et al., JASA 2007](https://doi.org/10.1121/1.2786607)
and [Puputti et al., NIME 2008](https://www.nime.org/proceedings/2008/nime2008_049.pdf)
for wound-string contact structure, and [Allen/Berkley 1979](https://users.umiacs.umd.edu/~ramanid/cmsc828d_audio/AllenBerkley79.pdf)
and [Xu et al. 2023](https://arxiv.org/pdf/2309.03486) for receiver geometry
and directionality. These references establish mechanisms, not a measured
parameter set for this particular virtual guitar.

The [GuitarSet paper](https://archives.ismir.net/ismir2018/paper/000188.pdf)
describes the source corpus. Pair statistics used here come from the existing
repository analysis, not a new corpus measurement or a table in that paper.
The old report explicitly deferred Finger pairs because of the intent
ambiguity; that limitation is preserved rather than presented as new proof.

[Bhanuprakash et al., DAFx 2020](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_68.pdf)
supports continued contact-dependent damping. The held-key cohort is our MIDI
interpretation; that paper does not measure how long a guitarist mutes each
unused chord string. Its damping magnitude is retained from the existing
GuitarSet dead-stroke analysis.

## Listening protocol

`Tools/RenderRealismQuickWins.cpp` sends identical public Performer events to
both source snapshots. It includes complete phrases and individually enabled
candidates. It uses Rack-like control levels (Room 10%, Release Noise 10%,
Piezo Mix 20%, Output 0 dB) as its primary profile, without claiming to render
through the Rack wrapper. Room 50% scenes are separately labeled. Explicit
CC65/CC84 connected gestures exercise the canonical Performer; the current
Rack wrapper does not expose that MIDI route.

`Tools/BuildRealismQuickWinsReview.py` freezes and hashes both DSP inputs,
compiles the same renderer twice with identical flags, records exact events
and parameters, and requires candidate-all-off audio to match the baseline
WAVs byte for byte. It retains native stereo float files. Native 96 kHz scenes
are converted to 48 kHz for audition with the same offline anti-alias filter
on both sides; no waveform alignment is applied.

The blind player balances A/B identities and offers whole-clip RMS matching
and native relative levels. Both use common pair headroom and no EQ,
compression or limiting. The listener can switch at the same media time,
export notes/votes, or play a sequential A-then-B file. Browser switching may
introduce a short gap. RMS matching is not LUFS matching and can obscure an
intentional level change, so the native mode remains available.

Sixteen scenes cover complete phrases, isolated mechanisms and diagnostics.
Two late-slide scenes use the existing nominal Release Noise 50% and additionally
offer an identical 5.8–8.2 s crop with a common +30 dB gain, capped together for
headroom. These are explicitly amplified quiet-tail diagnostics, not stock
loudness or difference tracks. They preserve the prior preference for restrained
handling noise rather than changing the synthesis gain. Full native-level
versions are retained. The complete-candidate scenes leave the experimental
axial observer off, matching production defaults.

## Final listening evidence

The frozen player has 16 balanced blind pairs, native 48/96 kHz float renders,
102 PCM24 audition files and exact event/control metadata. Its pack ID is
`70adb4f47282ba8cccd109cf8f43424067010f7a36d5bde49c6806774c324098`.
All sixteen candidate-all-off renders match the frozen main WAVs byte for byte.
The 17 baseline files also match the named Git commit exactly. Candidate files
and all audition/player hashes were checked against the live frozen worktree.

| Mechanism | Final fixed-score observation | Listening interpretation |
| --- | --- | --- |
| Hammer work | 6.0–6.6 s output RMS: −74.42 → −29.60 dBFS | A deliberate late hammer now sounds a quiet held string. Force mapping remains authored. |
| Pull-off | Same late window: −74.83 → −34.60 dBFS | The departing finger supplies an independent pluck on open D. |
| Finger pairs | Difference RMS −31.89 dBFS; overall level changes only about 0.1 dB | Timing is clearly changed; waveform difference is not a realism or preference score. |
| Winding crossings | Default full-score difference −93.80 dBFS; quiet-tail diagnostic difference −48.54 dBFS after the common +30 dB | Intentionally faint in the ordinary mix. Use scene 13's diagnostic to judge the texture; no stronger default gain is claimed. |
| Early reflections | Difference −66.53 dBFS at Room 10%, −43.32 dBFS at Room 50% | Subtle at the low default; scene 9 exposes the spatial change. Room and receiver geometry are declared choices. |
| Room conversion | Full-path 96 kHz loss of 1.18 dB at 8 kHz and 2.69 dB at 12 kHz corrected to within 0.0004 dB on matched grids | A measured numerical coloration fix; scene 11 uses more room to expose it. |
| Held-shape mute | Full scene 15 difference −42.94 dBFS, 13.41 dB below baseline RMS | Removes unused-string sympathetic ringing while the original chord is held; it adds no source. |

The extra axial observer remains disabled. Its default full-score difference is
−107.61 dBFS; even the amplified quiet-tail diagnostic is only −62.87 dBFS.
The source force conversion is correct (the shipped calibration is 371.317
N/engine-unit); no missing multiplier justified a gain increase. Its modal,
saddle-angle and radiation assumptions need stronger calibration.

No listener preference or matched real-guitar improvement score is claimed.
The evidence establishes mechanisms, numerical corrections, deterministic
activation and reviewable listening examples; the blind player is provided
for the perceptual judgment.

## Validation

The final Release build and strict C++17 DSP target pass. All **108/108 CTest
suites pass** (214.74 s), with required Python checks enabled. No suite was
skipped. One existing warning remains in `Tests/OutputBusTests.cpp:89`: its
switch omits `Sustain`; the changed DSP's strict warning gate is clean.

Browser checks decoded all 68 switchable clips and 34 sequential comparisons,
verified duration/channels/finite nonzero samples, and exercised play, seek,
A/B time preservation, spotlight offset translation, one-active-player
behavior, vote/notes export and identity reveal. The 390 px layout has no
horizontal overflow. The only browser resource error was an absent favicon;
all audio assets loaded. Automated test votes are marked as fixtures, not
human feedback.

The final ASAN/UBSAN run passes **11/11 suites**, including the engine,
Performer, all four new mechanism suites, legacy muted strings, legato,
strumming, fret motion and contact travel. LeakSanitizer is unsupported on
this macOS runtime: the initial `detect_leaks=1` attempt aborted before tests;
the preserved successful runs use `detect_leaks=0` with address and undefined
behavior checks enabled. CMake regenerated the new sanitizer target during
the multi-target build; a follow-up build completed the newly registered
held-mute and muted-string targets. Neither setup failure is hidden.

The bounded native benchmark completed one fixed baseline-then-candidate run
per revision, with eight eight-second cases at 48/96 kHz and 64-frame callbacks.
Each revision processed **72,000 callbacks**, with **zero measured wall-clock
or charged-thread-CPU deadline exceedances**. Candidate median charged CPU
cost increased **7–12% at 48 kHz** and **27–32% at 96 kHz** across the four
workloads; p95 increases were 3–11% and 10–26%, respectively. Summed callback
CPU time increased 19.84%; the 96 kHz cases contribute twice as many callbacks
to that aggregate.

The largest candidate wall outlier was 899 µs (474 µs charged CPU) against a
1,333 µs deadline at 48 kHz. At 96 kHz the candidate maximum was 379 µs against
a 667 µs deadline. No run was repeated or discarded. Setup and file I/O were
excluded, timer overhead was retained, and identical event scores produced
finite audio with zero dropped events. This offline comparison does not
guarantee Reason/Recon host scheduling or performance on other machines.

Exact distributions, every callback, timer overhead, source hashes, compiler
commands and logs are retained in `out/realism-20261016/benchmark-final`.
The review bundle includes the report, research and benchmark evidence;
original validation outputs remain under `out/realism-20261016`.

These recordings use the canonical Performer, not the Rack wrapper. The
CC65/CC84 route for explicit hammer/pull gestures is not forwarded by the
current Rack wrapper. The earlier Acustra 1.5.0f1 RE package remains the
unchanged main build; this task does not claim new host playback, song
persistence, installation or portal delivery.
