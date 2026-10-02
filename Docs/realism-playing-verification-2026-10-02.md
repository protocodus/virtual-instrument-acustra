# Playing allocation verification, 2026-10-02

The accepted natural-harmonic correction protects a held string when another
eligible string is available, and repeats a released harmonic on its still
ringing string. The existing recording benchmark remains byte-identical.
Separately, a native GuitarSet annotation replay verifies the earlier sounded
attack commitment change. These observations support specific playing behavior;
they do not establish a uniform physical or perceived realism gain.

The [machine record](realism-playing-verification-2026-10-02.json) retains the
protocols, before/after observations, tests, initial failed checks, exact methods
and source/binary hashes. The natural-harmonic change is measured against
checkpoint `38b8395be854a156a8c1603b608aad84559ab875`; its modified source hash is
recorded as a pending change, rather than attributed to that checkpoint. The
GuitarSet comparison uses different frozen libraries and predates this correction.

## Natural-harmonic behavior

`chooseHarmonic` now prefers an unused string, then a string whose key is up,
before taking a held string. Within equal availability it preserves the existing
lowest-node choice and string-order tie behavior. A repeat first checks for a
released, still ringing harmonic of the requested pitch and uses that same
physical string. That repeat must still satisfy the existing 25-cent pitch
eligibility after tuning changes.

| Requested note | Lower node | Available alternative |
|---|---|---|
| D6, MIDI 86 | G string, H6 | D string, H8 |
| B6, MIDI 95 | High E string, H6 | B string, H8 |

Previously a held G or high E could be replaced by H6 while the H8 string was
released, including under the pedal. Now the released alternative receives the
new attack and the held note keeps its owners. A released harmonic repeat
previously moved to an unused string and left the original vibration alongside
it; the repeat now attacks its original string. Choosing H8 instead of H6 can
change harmonic level and timbre. No listener preference verdict is inferred.

The production edit is confined to this chooser. No control, calibration,
public API or voice layout changes. The MPE natural-harmonic fallback shares
the chooser; string-per-channel frettability and the existing held-duplicate
retuning path are outside this change.

The native proof retains 96 availability cells: two presets, three rates
(44.1/48/96 kHz), two notes and eight states. States cover both silent, held with
silent or released alternatives, reversed availability, both held, both released,
pedal-held alternatives and duplicate held ownership. Thirty-six choices change
and avoid the prior held-note loss; all other equal-availability choices remain.
Twelve additional public repeat cases verify the attacked string and unchanged
attack states on the other five strings. Six B6/Half Step Down checks reject
the unreachable pitch without a new attack. This is 108 public behavior cases
with six retuning subcases, not 192 independent failures or 114 independent notes.

The new tests against the old library produce 192 assertion failures across
48 distinct affected fixture cells: 36 availability cells and 12 repeats.
Several assertions describe each failed cell. The corrected guarded allocator
suite passes, including duplicate-owner key-up behavior. The recorded Engine,
HandAllocator, Performer and Release CTests pass 4/4 in 100.48 seconds; the
C++17 compile target also passes. The actual PluginProcessor test passes 1/1
in 13.45 seconds. An initial plugin filter matched zero tests and is preserved
as an unsuccessful test selection, rather than counted as a passing test.

The existing dense-roll tradeoff remains: 12 of 88 hand-timed cases require an
occupied-string take because no valid free string is available. Local test
timing output does not constitute a host or plugin callback performance benchmark.

## Existing recording corpus parity

The predeclared parity protocol retains both explicit presets: Original
Dreadnought/Spruce and Bellido Auditorium/Mahogany, shipping calibration and
dry StereoMic controls. Known picked bank rows use Pick; bank flat-top rows
use Finger; Eastman retains each row's tool label. Martin's unknown tool remains
assumed Finger and external velocity remains 91. Every split and row remains.

All 246 unique model audio files are byte-identical to the
[recording baseline](cross-model-recording-baseline-2026-10-02.md). All 12 full
score reports, descriptor counts and weighted residual arrays match exactly.
The baseline machine record already embeds those full residual reports and is
referenced by exact SHA256 here. This includes all six splits for both presets;
no best-preset selection or descriptor fitting occurs.

The initial metadata validator failed six external manifests solely because
their renderer hashes correctly identify the changed executable. All audio and
all score reports already passed. The final validator independently verifies
both old and new renderer binaries before excluding that expected provenance
field and output-path differences. Both validators and both reports remain.
The production source and new renderer hashes are attested rather than hidden.

These notes span MIDI 40–84. Their exact parity guards the established tone
benchmark; it does not test the new above-fretboard harmonic behavior. The
different recorded instruments, assumed external velocities and reused Eastman
flat-top targets also preclude a same-instrument or untouched-holdout claim.

## GuitarSet live-allocation attribution

This comparison isolates the earlier `109cc6` sounded-attack commitment change
already present in `38b8395`, against the merged PR12 checkpoint `a7bd971`.
It uses frozen pre-harmonic libraries and matching headers. The later harmonic
fix cannot contribute to these results.

The six tracks were fixed before outcomes: `00/02/04_BN1-129-Eb_comp` and
`01/03/05_BN1-129-Eb_solo`, each from frame zero through the first 12 seconds.
GuitarSet v1.1.0 annotations pass through the unchanged
`BenchmarkPerformances.py::events_from_jams` helper. Endpoints round to 48 kHz
samples, offsets cap at the next onset on that annotated string, key-ups precede
key-downs at a shared frame and annotated-string order breaks ties. Pitch rounds
to MIDI, velocity is fixed at 91 and channels collapse to channel 1. Fractional
bends, pedal and expression are omitted; confidence is not velocity.

The read-only native probe calls the ordinary engine note API and processes
exact intervening samples in blocks of at most 64. It adds no Gather Chords,
automatic strum or lookahead. Each preset/version processes the same 617 events:
315 note-ons and 302 note-offs. Six tracks × two presets × two versions give
24 replays. Before/after snapshots record physical assignments, key ownership,
frets, onset and attack-only RNG state. A prior held pitch other than the incoming
pitch is collateral if it changes string, receives another attack or disappears.
Explicit same-key repeats are counted separately.

| Preset/version | Explicit attacks at supplied frames | Collateral remaps | Collateral extra attacks | Held-note drops |
|---|---:|---:|---:|---:|
| Original, PR12 | 315 | 2 | 2 | 0 |
| Original, attack commitment | 315 | 0 | 0 | 0 |
| Bellido, PR12 | 315 | 2 | 2 | 0 |
| Bellido, attack commitment | 315 | 0 | 0 | 0 |

The two affected events occur in `00_comp` and `04_comp`, one in each per preset.
All input attacks still fire immediately at their supplied frames. That is
verified through exact process progression and attack-only RNG changes, rather
than an inferred audio onset. No held-note drop occurs in this small set; the
existing 12/88 dense-roll tradeoff remains. The probe does not download or score
microphone audio, judge actual played strings, or exercise the full performer
and host pipeline.

The channel collapse retains one physical-unison confound: MIDI 55 at frame
343007 in `02_comp` overlaps another annotated string's MIDI 55. Both versions
perform one explicit same-key repluck with duplicate ownership. It is excluded
from collateral counts, not removed from input. Four simultaneous two-note
groups occur in `04_comp`; no three-note group occurs. Repeated annotated
string/pitch onsets remain separate from already-held native key repeats.

An exploratory inspection after outcomes identifies a material hand-shape cost:

| Track/frame | PR12 held frets | Attack-commitment held frets | Positive-fret span, before/after |
|---|---|---|---:|
| `00_comp`, 459647 | G4 on B8; C5 on high E8 | G4 on high E3; C5 on B13 | 0 / 10 |
| `04_comp`, 357373 | G♯2 on low E4; C4 on G5; D♯4 on B4 | G♯2 on low E4; C4 on B1; D♯4 on G8 | 1 / 7 |

Both current spans exceed the ordinary allocator's four-fret stretch. This
max-minus-min positive-fret observation does not certify complete four-finger
feasibility, but prevents interpreting fewer collateral attacks as uniformly
more physical. Prior sounded assignments remain committed without lookahead.
The post-outcome inspection does not add or change an acceptance gate.

JAMS onset/offsets estimate transcription timing rather than finger contact.
Common-channel MIDI removes the annotated physical-string identity. Existing
GuitarSet statistics informed the shared stroke model, so this is not an
untouched corpus. Six fixed phrase excerpts do not establish full-corpus
playing accuracy, a Finger dynamics law or an overall realism rating.

## Evidence and reproduction

The machine record embeds compact protocols, native before/after reports,
owner interpretation and exploratory contexts, all test/build logs, initial
failed validation and corrected validation, exact helper sources and production
patches. Annotation/archive, audio and binary hashes are retained without
committing the archive, audio or full raw trace files. The six compact converted
event inputs preserve the supplied timing and source labels; all 24 complete
trace hashes remain. Existing baseline documents are referenced by exact hash.

The public harmonic audition is retained at
`build-realism/harmonic-listening/review.md`, with its compact provenance and
readback verification in the machine record. It supplies matched examples,
not a listener verdict or a gain-normalized score.
No host performance assessment or broad realism conclusion follows from these
checks.

Reproduction requires the recorded frozen source/library/header pairs and
compiler flags. Current working source cannot substitute for a historical
baseline. Use fresh evidence directories to preserve the initial failed checks
and native observations. The embedded read-only validator checks the compact
record and available original paths without rendering or refitting.

Packaging validates 641 retained artifact hashes, including both copies of all
246 corpus audio files, 24 native GuitarSet traces and the audition evidence.
The embedded validator checks 24 parsed reports, 23 exact text inputs, 12 helper
sources and 12 full-score references to the committed baseline.

```sh
python3 -c 'import json,pathlib,sys; d=json.loads(pathlib.Path(sys.argv[1]).read_text()); exec(d["execution_and_reproduction"]["embedded_input_validation_program"])' Docs/realism-playing-verification-2026-10-02.json
```
