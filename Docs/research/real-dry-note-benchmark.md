# Real dry-note benchmark

Moved out of the README on 2026-10-17 with its content unchanged.

The physical fit is compared with 76 real, dry steel-guitar note recordings:
54 training examples, 14 development-validation examples and 8 independent
flat-top examples. Until nylon strings were removed on 2026-09-29 the first
two splits also held 39 classical-guitar recordings (29 and 10), so every
dated score below and in the frozen report was taken over 83, 24 and 8 rows
and is not comparable with a steel-only reading. The benchmark measures attack, harmonics,
tuning, early pitch trajectory, decay rate, body spectrum, and dynamics. Lower
is better, but the absolute score depends on analysis settings, so only paired
comparisons over identical notes support a decision. In the frozen
2026-09-04 comparison, the fitted calibration changed the three scores by
−26.4%, −20.1%, and −13.3% against neutral calibration rendered with the same
engine and notes. These are engineering descriptors, not a listening
preference or a claim of perceptual equivalence. That dry-note comparison
predates the current force/moment body; its newer GuitarSet and AG-PT results
are reported separately below and in Known gaps.

The renderer also emits matched `*-sample-v1.json` manifests for the frozen
first-version sample player. Its scores are 1.2080, 1.4437 and 0.1286 on those
splits, but they are a pipeline control rather than a product win: that player
is replaying the same captured zones used as targets. The result measures its
attack envelope, interpolation and stereo playback path; it says nothing about
uncaptured notes, transitions or performances. Keeping this control beside the
physical result prevents the misleading claim that a source-recording replay
is an independent realism benchmark.

On the engine as it stood from 2026-09-24 - the force/moment body, the
coupled-model shapes, the continuous Gaussian contact, the steel contact
transport and the Yamaha nylon calibration, the two lines of development
merged that day - the same
protocol at the shipping calibration read 6.392396 on training, 6.378758 on
development validation and 9.256751 on the flat-top rows with the benchmark's
reference Original bridge, and 6.268767, 6.172187 and 8.591335 with the Fylde
bridge the steel presets then played; with the parallel polarisation radiating
(Pluck and strings) they read 6.167890, 6.042989 and 8.559757, and 6.148927,
6.008802 and 8.232583; with a steel pluck releasing most of its energy
parallel to the top and nylon's normal-led, 6.087514, 5.938380 and
8.120314, and 6.112127, 5.928819 and 7.948071; and with the three values a
blind listener chose that day (no polarisation end correction, a longer nylon
T60, steel's wider Dreadnought), 6.145552, 6.216121 and 7.618706, and
6.180182, 6.103707 and 7.572502; with a force-limited release, 6.131424,
6.217292 and 7.622994, and 6.182335, 6.097915 and 7.556267; with steel's
body resolved over 250 ms and nylon plucked at the soundhole's edge,
5.928457, 5.989914 and 7.492669, and 5.877624, 5.806595 and 7.328740; and
with the three values a blind listener chose on 2026-09-25 (the treble-bridge
and upper-bout microphone pair, a steel Finger 149 mm from the bridge,
steel's air mode four times stronger at the bridge microphone), the refitted
plectrum and double-precision bridge modes, 6.329200, 5.994260 and 7.425652,
and 6.432400, 5.996174 and 7.181437. The latter are the base every later
paired comparison was taken against until 2026-09-27. Under Pick the archtop
rows read 6.103277 and 5.702917. With the plectrum released as the string
slides round its edge and steel's plate modes damped to the anechoic
flamencas' measured Q (2026-09-27), the Fylde reading is 6.403167, 6.019245
and 7.121675, and under Pick with the Fylde bridge 5.943771 and 5.605528
(steel alone 5.516350 and 5.511048). With the pluck and bending loss chosen
by ear on 2026-09-28 the Fylde reads 6.4472, 6.0328 and 7.2714, and under Pick
6.0173 and 5.5799. On steel's own bridge, which the steel presets played from
that day, with the plate-Q rule down to 150 Hz, the readings are 6.4040,
6.0056 and 7.2752, and under Pick 6.0182 and 5.5377. These were the base
when nylon strings and the Fylde bridge were removed on 2026-09-29; they
include the bank's classical rows, rendered then on nylon strings, and have
not been re-taken since. Every reading above is dated and none is rescaled.
The renderer renders steel at its anchor body, the default Dreadnought.

The bank's steel rows are an archtop, and its only flat-top is eight notes
of one finger-plucked take. [`BenchmarkOpenCorpora.py`](../../Tools/BenchmarkOpenCorpora.py)
scores the same descriptors against open recordings outside the bank, each
prepared by a tool that pins its source hashes and licence and writes rows in
a shared schema (no audio is committed): 49 picked and 55 finger-plucked notes
from the two full CC0 Eastman E1D dreadnought takes
([`PrepareEastmanCorpus.py`](../../Tools/PrepareEastmanCorpus.py)), 11 regions of a
CC0 Martin HD28 sample bank whose samples start at the attack and stop at their
loop. The scorer includes its cropped attack term in historical totals;
that term does not independently validate release timing
([`PrepareMartinCorpus.py`](../../Tools/PrepareMartinCorpus.py)). Its
renderer is the fit renderer's model half, byte-identical to it. With steel
on its own bridge (2026-09-28) they read 6.3767 (Eastman picked), 6.8426
(Eastman finger) and 7.0770 (Martin HD28); steel's rendering has not changed
since. None of them is fitted; a
split used to choose a calibration stops being a held-out reading. Two of the
bank's eight flat-top targets (E2, E3) start after their attack has begun,
which the corpus tool's onset rule catches and the bank's export did not.
`BenchmarkOpenCorpora.py --smoke --renderer ./build-dsp/AcustraExternalCorpusRenderer`
runs the whole pipeline on a synthetic corpus it writes itself, downloading
nothing; ctest runs it (Acustra.OpenCorporaBenchmark) beside every other
Python tool's self-test.

On 2026-10-01 the capture voicing and the Room were measured against all of
them at once - the bank's three splits, the Eastman's two takes, the Martin
and GuitarSet's six performances - at the shipping calibration and default
controls (Docs/decisions.md has the method and the held-out readings):

| | train | validation | bank flat-top | Eastman pick | Eastman finger | Martin HD28 | GuitarSet log / SC / chroma |
|---|---|---|---|---|---|---|---|
| main 4f19875 | 6.9259 | 6.6772 | 6.5531 | 6.1818 | 6.4737 | 7.0697 | 14.419 / 0.828 / 0.224 |
| voicing, Room 0 | 6.6952 | 6.5136 | 6.7439 | 5.8671 | 6.4892 | 6.5626 | 14.166 / 0.796 / 0.209 |
| voicing, Room 0.5 | 6.2551 | 6.1195 | 6.5735 | 5.6299 | 6.2282 | 6.4047 | 13.999 / 0.804 / 0.210 |
| spring-back, Room 0 | 6.7387 | 6.7221 | 6.4178 | 5.4959 | 6.2364 | 6.3510 | 14.105 / 0.793 / 0.208 |
| spring-back, Room 0.5 | 6.3401 | 6.3571 | 6.1925 | 5.1751 | 5.9352 | 6.1720 | 13.929 / 0.804 / 0.211 |

The renderers and both benchmark drivers take `--room` (default 0, the dry
engine the calibration was fitted on); the plug-in plays at 0.5. The archtop
rows were in no fit. The voicing alone loses on the two finger-plucked sets
when each is held out of its own fit (Eastman finger +1.0%, the bank's
flat-top +4.1%), and with the room the bank's flat-top is within 0.3%.
The spring-back rows add the held string's released static force
([The top's spring-back](../../README.md#the-tops-spring-back)), at the 0.8 of it a
listener chose, with the voicing refitted after the full release: every
flat-top set improves again (3.2-8.1%) and so does GuitarSet dry, while the
archtop and classical training and validation rows lose 0.6-3.9%, guitars
whose tops barely ring where a flat-top's air mode does.

Run `python3 Tools/SummarizePhysicalBenchmark.py` to print the compact split
table, historical sample-player control and five retained realism paths from
the committed
[`physical-fit-report.json`](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/research/physical-fit-report.json). The full renderer
and scoring protocol remain in [`PhysicalFitRenderer.cpp`](../../Tools/PhysicalFitRenderer.cpp)
and [`FitPhysicalModel.py`](../../Tools/FitPhysicalModel.py).

For a new engine build, `FitPhysicalModel.py candidate.json --compare baseline.json`
checks target audio SHA-256 hashes, note identities, dynamics groups and analysis
settings before reporting paired changes by descriptor.
Render manifests record the actual calibration vector and controls; model-only
re-renders update that vector. Microphone targets do not establish pickup
realism, and this corpus does not identify pick and thumb technique separately.

A separate [GuitarSet](https://zenodo.org/records/3371780) performance audit
compares 72 seconds of real microphone audio with string-aware renders of
315 annotated notes: six players, three comping and three solo takes of the
same phrase. Velocity is fixed at MIDI 91 because the annotations contain no
measured note dynamics. The first baseline averages 14.88 dB of multiscale log
spectral error, 0.903 spectral convergence and 0.312 chroma cosine distance.
These expose substantial remaining mismatch. The recording uses a different
body, microphone and room, and GuitarSet timing statistics previously informed
the engine, so this is not an untouched holdout or a perceptual ranking.
Signed octave-band diagnostics locate that mismatch: after whole-clip RMS
matching, the baseline averages 16.2 dB less energy at 80–160 Hz and 9.5 dB
less at 2560–5120 Hz, with excess energy in the intervening low-mid bands.
These are spectral differences on these recordings, not additional score terms.

[`BenchmarkPerformances.py`](../../Tools/BenchmarkPerformances.py) fixes the six
excerpts before scoring, verifies the published archive checksums, preserves
annotated timing and quiet frames, and exports paired WAVs matched by whole-clip
RMS. Its report retains input, executable and scorer hashes, individual metrics
and listening trims. The committed report's `performance_benchmark_2026_09_05`
entry retains this baseline without distributing the source recordings.
The adjacent `performance_spectral_diagnostic_2026_09_05` entry uses exactly
the same audio bytes and leaves all three baseline distances unchanged.

Until 2026-09-29 a measured Fylde steel-string bridge was a second Bridge
Model choice, which the steel presets played from 2026-09-10 to 2026-09-28.
It was removed as making little audible difference; its paired benchmark
results, provenance and exact input/model hashes stay in the report's
`measured_steel_bridge_2026_09_05` entry and the decision log. Every result
below is steel's own bridge, the one the instrument now has.

A historical audit of the now-retired magnetic model used the same six
performances, which have simultaneous magnetic recordings: a mono
mix of six Ubertar single-coil pickups on nickel-wound steel strings. Against
that reference, the Original bridge's magnetic output scores 17.591981 dB log
error, 0.788438 spectral convergence and 0.268841 chroma distance. After
RMS matching, Original averages 3.95 dB less energy at 80–160 Hz and 12.44 dB
more at 5120–10000 Hz. This identifies a substantial remaining pickup mismatch.
Cross-capture controls disagree too: the microphone model has lower log error
against the pickup recording, while the retired magnetic model has lower spectral
convergence. These descriptors therefore cannot rank pickup realism by
themselves. The committed `magnetic_performance_benchmark_2026_09_05` entry
retains both bridges then offered, the four microphone/pickup pairings and source hashes;
no parameters were fitted to these results. Coil position, field response,
loaded electronics and per-string gains are undocumented in GuitarSet.

An independent exploratory audit adds 344 recordings from Mohammed Alkooheji's
[Acoustic Guitar Notes v3](https://www.kaggle.com/datasets/mohammedalkooheji/guitar-notes-dataset):
a Walden G551E on steel and a Yamaha CM-40 on nylon; the 2026-09-05 audit
rendered both, and only its steel half describes the current instrument.
Before inspecting audio, the tool selects two recordings at every pitch from
D2 to G♯5 for each guitar and the pick/finger labels. The author warns that
technique labels vary and combines finger with thumb; capture, velocity and
fingering are undocumented. No parameters are fitted to this corpus. The report's
`independent_technique_note_audit_2026_09_05` entry retains every recording's
descriptors, availability counts and source/model hashes; these are descriptive
results on another guitar, not controlled technique validation or a realism rank.

The [AG-PT-set](https://zenodo.org/records/10159492) hammer-on audit adds a
Taylor 114CE recorded through its LR Baggs Anthem system. Its hammer-on files
have technique labels but no authored event times. The fixed first ten seconds
of the piano and forte high-E recordings contain 11 and 14 confidently inferred
upward semitone pairs when lag correlation agrees with a harmonic-aware FFT
estimate. Trimmed plateau RMS falls by a median 4.64 and 6.27 dB respectively.
These are recorded level ratios, not finger velocities or injected energy.
Requiring agreement with only the strongest FFT peak keeps just five forte
pairs and changes that median to 4.10 dB; detector selection matters.
[`BenchmarkHammerOnNotes.py`](../../Tools/BenchmarkHammerOnNotes.py) verifies the
published file hashes and retains both masks, exact measurement windows and
rejected candidates. No piano/forte-to-MIDI mapping is assumed.

An accompanying engine audit compares those same 25 inferred pairs before
and after the fretting-state correction, across 18 declared timing/velocity
assumptions and two captures (1,800 renders). Each assumption set reduces the
piezo's median absolute level-ratio difference by 0.71–2.32 dB; microphone
changes range from a 1.08 dB reduction to a 0.63 dB increase. These are
conditional comparisons: the recording's blended pickup, actual finger motion
and event times are unknown. No best setting is selected or fitted. The
`agpt_hammer_plateau_audit_2026_09_05` report entry retains the protocol,
per-setting results and source hashes.

Repeating the unchanged protocol for the subsequent action-geometry correction
increases median absolute level-ratio error in 17 of 18 microphone assumptions
and all 18 piezo assumptions. Across assumptions the change ranges from
−0.004 to +0.828 dB for microphone and +0.026 to +0.274 dB for piezo. All
900 pre-hammer prefixes remain identical. Correct fret coordinates therefore
do not establish better agreement with these recorded hammer-on levels; the
unmeasured velocity map remains a separate problem. Legato, and with it the
engine's hammer-on, was removed on 2026-09-28 at the user's request, so these
engine comparisons are history; the recording audit stands on its own.

A private listening reference compared Acustra, on both bridges it then offered, with the installed
NI Strummed Acoustic 1.1.0 library in Kontakt 7.10.6: four bars of C major at
120 BPM, eighth-note strums, stereo capture, and EQ, compression, reverb and
doubling disabled. Ten-second auditions share the same crop and match whole-file
stereo RMS to −24 dBFS. NI chooses recorded voicings, accents and microtiming;
Acustra receives explicit open-C note events. This matches harmony and nominal
rhythm, not identical performances, and establishes no product ranking.
The report's `commercial_listening_reference_2026_09_05` entry preserves the
protocol, state/input/audio hashes and repeatability evidence. Commercial audio
and opaque plugin states remain private; reproduction needs the installed library.
