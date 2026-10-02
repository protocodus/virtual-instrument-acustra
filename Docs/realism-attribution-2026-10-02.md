# Preserve live-chord attacks and trace low ringing, 2026-10-02

Baseline: `a7bd971f59cdb2397331521d0e6b5705808ea530` (merged PR #12).
The user approved the recommended follow-up. No visible controls are added.

## Live chords commit attacks when they fire

An exposed C4–E4–G4 roll, one key every 10 ms, formerly moved C4 from the
B string to G5 when G4 arrived and plucked C4 a second time. The engine now
records whether a note's first attack has fired. Forming-chord searches fix
that note to its current string; unfired attacks can still move with their
remaining delay and duplicate owners. A sounded note waiting for an explicit
repeat also retains its preceding wave and scheduled repeat. The original
note-on latency is unchanged.

The distinction needs its own private state. `pluckDelay` describes the next
scheduled attack, and `repluckPending` can also describe two owners of a
first attack that has not sounded. Neither identifies whether the note has
already sounded. Tests observe the existing release-only random generator,
rather than asserting that the new state agrees with itself.

This corrects extra attacks, with a material allocation tradeoff. Without
future pitches, C4 on B1 and E4 on the open high E leave G4 on G12: a wider
fingering than one hand reaches. Dense rolls can occupy every string that
can fret the incoming high note. In 12 of 88 hand-timed stroke fixtures,
the incoming note takes an earlier note's string; all twelve have no valid
free string within frets 0–20. The earlier wave takes the normal retained
re-pluck tail. The tests require preservation whenever a valid free string
exists, rather than silently accepting avoidable steals.

The affected downstrokes are B2–G3–C4–E4–B4, B2–G3–B3–G4–B4 and
B2–G3–C4–G4–C5 at 5, 10, 15 and 20 ms spacings. Complete same-sample plans
retain every note in these shapes. Existing Gather Chords can plan the
entire shape before its first attack, with its existing 30 ms latency.
No extra voice or implicit legato is introduced to conceal this constraint.

The regressions cover three sample rates, blocks of 1, 64 and 4096 samples,
original first onset, no regenerated earlier releases, moved pending due times,
duplicate ownership and already sounded notes waiting for a repeat. Revised
tests fail 65 HandAllocator and 18 Performer assertions on the frozen
baseline, establishing that they detect the old behaviour.

## The 165 Hz feature includes strings and body response

`Tools/TraceRinging.cpp` exports per-string quarter-point motion and each
low body mode's radiated contribution. Eighteen dry 1.5 s captures cover
Finger/Pick MIDI 64, 67 and 72, velocity 91, default Original Dreadnought
in Spruce. Each uses Standard, public Drop D, or Standard with additional
passive low-E loss (T60 0.15 s, round-trip multiplier about 0.572). All six
reciprocal string ports remain connected. Body coefficients remain identical.
Observing the states leaves every audio float bit-identical to a second
unobserved engine.

The low-E quarter-point second partial moves from about 164.76 Hz to
146.52 Hz under Drop D. Its new octave overlaps D3, so per-string taps
identify the moving string; microphone difference alone remains a change
in the coupled load. Frequency-grid decimals in the JSON exceed the
finite-window resolving power and should not be read as that precision.

Additional low-E loss suppresses its local octave by 54.76–54.91 dB, but
reduces microphone projections near that octave by much less:

| Projection window | Microphone reduction across six notes/techniques |
|---|---:|
| 40–400 ms | 1.05–1.28 dB |
| 120–550 ms | 4.09–6.32 dB |
| 250–750 ms | 10.72–11.89 dB |

Later engine ringing therefore includes idle-low-E vibration. Earlier
radiation contains other body/transient contributions after that wave is
strongly damped. The 158.63 Hz T1 and 162.93 Hz joint poles have free T60
about 0.198 and 0.231 s. Their radiation, and a substantial contribution
from the 204.27 Hz mode, all reflect the force from the entire junction;
modal contributions can cancel. They do not identify which string supplied
that force. The work checks are stationary bridge diagnostics, not a full
string/hand/body energy proof.

Seven clean Eastman clips cover Pick 64 (two repeats), 67 and 72, and
Finger 64 (two repeats) and 67. There is no accepted Finger 72 reference.
The three Finger clips' reference-minus-model 165-region phasor levels,
normalized to each clip's first 100 ms broadband RMS, are median +19.81,
+27.54 and +21.02 dB in these windows. This specific finite-window metric
can include nearby transient leakage; it differs from the previous pooled
open-tone +4.65 dB measurement. Apparent Finger decay also changes with the
window: early T60 spans 0.68–1.87 s, middle is about 0.82 s, and later is
0.97–1.08 s, with modest low-band variance explained. All 54 model component
fits in these captures hit a parameter boundary. These fits do not establish
a structural Q.

`Tools/AnalyzeRingingTrace.py` distinguishes the 149–183 Hz component fit
from the shifted Drop D octave and uses separate per-string spectral
searches. Its negative tests reject silence, lone open-tone and air nuisances.
The broad microphone peak in the model is often D3 rather than 165 Hz.
Neither a global sympathy gain nor a body-Q/gain change is supported by
these traces. A real Standard/Drop D comparison with consistent idle-string
hand contact would identify the recording's string/body mixture more clearly.

The first custom-tuning attempt was rejected: it did not reproduce the
engine's existing public Drop D response exactly. Its helper and measurements
are excluded from the final tool and conclusions. The final perturbation
uses the existing public tuning before `prepare`, without intercepting
control updates. Failed/intermediate evidence is retained separately locally.

## What spatial palm damping still needs

The available Eastman pick/finger walks and Martin sustain samples have no
contact-position, width or pressure annotations. Eastman's 17 detected
damping endpoints cannot distinguish palm contact from fret release, and its
clean-target protocol excludes those endpoints. These recordings do not
calibrate a spatial CC2 absorber.

The current string loop stores a collapsed round trip. Its local tap is an
observer; ContactTravel injects transient excitation. A spatial absorber
needs a two-sided scattering port with passive fractional transport, both
polarizations and retained-tail state. The authored release-noise position
near the saddle is not a measured palm geometry. The bridge work ledger is
not a complete string/hand/body energy proof.

A future contact experiment should use existing CC2 to control nonnegative
resistance at a fixed saddle distance. Its gates are whole-wave identity at
zero pressure, nonnegative absorbed work including filter/transport storage
during contact and lift, node-dependent partial damping, continuous tails,
and controlled muted/unmuted recordings. The primary
[DAFx24 contact example](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_50.pdf)
illustrates why an explicit energy balance is needed. This follow-up retains
the existing CC2 law while identifying those requirements.

## Listening

`Tools/RenderRealismReview.cpp` writes six native-level, five-second stereo
PCM16 cases at 48 kHz. The first four cases from PR #12 are byte-identical
between baseline and this candidate. The fifth exposes the three-note roll;
the sixth exposes the occupied-string tradeoff in B2–G3–C4–E4–B4. Both new
rolls are dry, at velocity 100 with 10 ms key spacing and Gather Chords off.
Audio is ignored locally; measurements and file hashes are retained in the
accompanying JSON.

## Validation and callback cost

The Release build passes all 47 native CTests in a serial run (201.54 s),
the strict C++17 DSP target, and the JUCE processor test (1/1, 14.45 s).
After adding summary reproduction to the analyzer, its test and the
self-test registry pass again (2/2). No production body coefficients,
UI parameters or existing CC2 law changed. The processor build uses the
verified JUCE 8.0.14 cache; this is not a Reason host qualification.

The first parallel run failed the Engine and OutputBuses timing gates
while other work was running, and the preliminary custom-retune probe
failed its invariant. Those failures are retained. The corrected probe
passes in the final run; the quiet serial timing checks report engine
realtime ratio 0.0332 and Main/Piezo costs 53.20/52.99 microseconds. The
slightly negative Piezo increment is timing noise, not a negative cost.

The paired callback diagnostic links the frozen baseline and current DSP
in one executable, with matched headers, compiler flags and named models.
Its 48 cases span Original/Bellido 1978, 48/96 kHz, 32/64/128 frames and
four events: six-string initial attack, six-string repeat, eighth harmonic,
and G4 joining a C4/E4 roll at 20 ms. Each has 16 warmup pairs and 256
measured pairs with alternating engine order. Timing includes noteOn and
the following process block; preparation, pre-roll and checks are outside it.

Paired median current/baseline ratios range from 0.508–0.737 for the
rolled join, reflecting the earlier attacks no longer being regenerated.
The three unchanged events span 0.987–1.015 and retain identical output
hashes. Their paired p95 ratios range 1.056–1.637, showing the noise in
individual pair timings; ratios of separate p95 costs are a different
statistic, and neither is a confidence interval.

Across 12,288 timed callbacks per engine, the largest current p95 occupies
66.70% of its native block deadline. There are three baseline and two
current deadline overruns. The largest current outlier is 2312 microseconds
in a 48 kHz/32-frame harmonic callback (3.468 times its deadline). All raw
observations and outliers are retained. These native measurements exclude
JUCE and host overhead and do not establish a zero-miss audio guarantee.

## Reproduction and retained evidence

The accompanying [JSON report](realism-attribution-2026-10-02.json) retains
the complete ringing analysis, summary definitions, capture manifest,
source/binary hashes, all callback samples, test outcomes and listening hashes.
The original analyzer measurement hash is distinguished from the later
summary generator hash; summary generation repeats no audio reads or fits.

Build and run the native checks with a Python that has NumPy and SciPy:

```sh
cmake -S . -B build-realism/next-current -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_UNIVERSAL=OFF -DACUSTRA_REQUIRE_PYTHON_TESTS=ON -DPython3_EXECUTABLE=/Library/Frameworks/Python.framework/Versions/3.11/bin/python3
cmake --build build-realism/next-current -j 4
ctest --test-dir build-realism/next-current --output-on-failure -j 1
```

For new traces, choose an output directory that does not already exist:

```sh
build-realism/next-current/AcustraTraceRinging --out build-realism/ringing-new/captures
OPENBLAS_NUM_THREADS=1 python3.11 Tools/AnalyzeRingingTrace.py --manifest build-realism/ringing-new/captures/manifest.json --eastman-rows build-realism/corpora/eastman/rows.json --probe build-realism/next-current/AcustraTraceRinging --output build-realism/ringing-new/analysis.json
```

Reference analysis requires the locally prepared Eastman corpus. It is not
needed for either tool's self-test. To reproduce just the published summary
from the retained JSON without the recordings:

```sh
python3.11 -c 'import json; p="Docs/realism-attribution-2026-10-02.json"; json.dump(json.load(open(p))["ringing_analysis"], open("/tmp/acustra-ringing-report.json", "w"))'
python3.11 Tools/AnalyzeRingingTrace.py --summary-from-report /tmp/acustra-ringing-report.json --output /tmp/acustra-ringing-summary.json
```

The callback compiler commands are recorded in the JSON and the adapter
instructions in `Tools/CallbackBenchmark.cpp`. Repeat measurements with
the same frozen baseline and current sources, keeping all observations.

The continued [two-model recording baseline](cross-model-recording-baseline-2026-10-02.md)
adds explicit wood selection to the offline renderers and evaluates the
Original and Bellido presets separately. That changes benchmark coverage;
it does not change production DSP or the ringing measurements above.
