# Finger seed and source geometry screens, 2026-10-02

No sound change is selected. The seed ensemble is an attribution measurement;
the Finger burst-direction and common-center candidates both fail their
declared corpus guards. These screens make no production DSP, visible-control
or listener-selected calibration changes. The companion
[machine record](realism-seed-and-geometry-2026-10-02.json) preserves protocols,
every compact statistical outcome, native observations, candidate score
residuals, source hooks, executed methods and provenance.

The reference policy is the
[current-model recording baseline](cross-model-recording-baseline-2026-10-02.md):
Original Dreadnought/Spruce and Bellido Auditorium/Mahogany, dry stereo,
unchanged shipping calibration. Pick renders the known picked bank training
and development rows; Finger renders the bank flat-top rows; Eastman retains
each row's declared Finger/Pick tool. Martin's unknown tool retains its Finger
assumption. External velocity 91 is assumed. The bank flat-top targets reuse
Eastman material, so those sets are not independent evidence. The machine
record identifies the baseline document and JSON by exact SHA256.

These are transfers between different instruments, stringing and captures.
The measured source bodies were nylon-strung and adapted for steel; the played
recordings do not validate the same instrument's source and body together.
No development/external set is presented as untouched. Descriptor improvements
and synthetic uncertainty alone cannot establish perceived realism.

## Finger soft-burst seed ensemble

The frozen width-1.0 DSP library is unchanged. A friend writes only
`voice.excitationNoiseState` immediately after the explicit fired attack;
the entire voice byte representation must equal the expected single-field
edit. The initialized waves, release slip, pluck RNG, scheduling, burst envelope
and contact-noise zeros remain unchanged. All 96 full-length stock files and
their 96 short prefixes match the retained native fixture byte-for-byte before
the 32 predeclared integer seeds are rendered.

The fixture uses the existing eight physical note/string pairs, velocities
16/112, both presets, 44.1/48/96 kHz, 256-sample blocks, 40 quiet settling blocks
and 0.4-second captures. Each seed is paired across velocities, notes, models
and rates. The same initial integer does not align continuous-time noise
across sample rates. The interrupted earlier 0.35-second, one-window draft
is retained separately; it produced no metrics or ranking used here.

Both windows were declared before the fresh run: 30–330 ms retains the
historical estimator; 80–250 ms is a separately labeled diagnostic. Each uses
its clip's trailing 1 ms RMS onset. The peak method sums H5–H12 power against
H1–H4, using the existing nominal-frequency search, symmetric Hann window and
65,536-point FFT. The second method integrates power around those tracked
peaks within ±2/window-duration Hz. Left, right, mono mean and incoherent
stereo power remain separate; integration is an acoustic spectral observation,
not modal or mechanical energy.

Each cell is the median contrast over the fixed eight notes within a seed,
then summarized over 32 seeds. The 10,000-replicate percentile bootstrap
resamples whole paired seed clusters, with analysis RNG seed 20261002. The
following table reports the historical-window mono peak means; both windows,
all channels/estimators, medians, per-note distributions and paired contrasts
remain in the machine record.

Source invariance refers to the retained screen snapshots derived from
checkpoint `38b8395be854a156a8c1603b608aad84559ab875`, whose DSP matches the earlier
baseline checkpoint. It does not assert that later independent changes to the
working engine match that snapshot.

| Preset | Rate | Stock draw | Ensemble mean [individual 95% CI], dB |
|---|---:|---:|---:|
| Original | 44.1 kHz | 9.5835 | 9.5579 [9.3885, 9.7299] |
| Original | 48 kHz | 9.0655 | 9.6106 [9.4497, 9.7751] |
| Original | 96 kHz | 10.3281 | 9.6971 [9.5506, 9.8431] |
| Bellido | 44.1 kHz | 8.5593 | 7.6549 [7.4754, 7.8465] |
| Bellido | 48 kHz | 6.9988 | 7.8613 [7.6979, 8.0291] |
| Bellido | 96 kHz | 7.9472 | 7.9983 [7.8184, 8.1767] |

The stock draw produces a larger aggregate rate spread: Original's peak means span
about 0.139 dB across rates, compared with the stock 1.263 dB; Bellido's means
span 0.343 dB, compared with 1.561 dB. Per-note distributions still differ.
A paired CI crossing zero does not establish equivalence. Intervals are
conditional on these simulated seeds and fixed notes, individual and unadjusted
for multiple comparisons. Stock percentiles describe 32 synthetic draws,
not a population of players or evidence of a defective generator.

The body contrast also depends on observation. Historical-window peak
Original-minus-Bellido ensemble means at 44.1/48/96 kHz are
1.903/1.749/1.699 dB in mono, versus 1.129/1.148/1.227 dB in incoherent stereo
power. Separate left/right differences are smaller. Mono cancellation therefore
affects the magnitude of this acoustic contrast; it does not independently
identify a body, hand or source-loss mechanism.

All 3,072 interventions pass the full-voice guard; all 3,168 analyzed files are
finite. Validation also checks the 96 full controls, for 3,264 finite saved
control/prefix/seed files. It recomputes all 96 cells and 112 paired cell
contrasts, including bootstrap intervals. All 304,128 partial observations
remain primary. There are five weak peak observations, 15 weak integrated
observations and five search-boundary flags, which can overlap. All 20 weak
paired observations remain retained; the largest admitted-only sensitivity
change is 0.244201 dB. The local interharmonic floor can contain deterministic
body or idle-string leakage and does not identify pure noise.

This measurement changes no width-screen classification. The historical
8–10 dB aspiration came from picked-bank velocity layers; current Finger
recordings do not identify a Finger velocity law. The previous failed width
gates remain failed. Neither the ensemble nor its alternate window authorizes
removing, reorienting or retuning the listener-selected burst.

## Finger burst direction

The offline candidate replaces Finger's fixed normal/parallel burst gains
0.76/0.51 with `sqrt(0.8377) * sqrt(polarisationMix)` and
`sqrt(0.8377) * sqrt(1 - polarisationMix)`. The projection is stored when the
attack actually fires and copied to retained contact travel. Pick/Thumb keep
their legacy injection statements. Source profiles, widths, slips, noise,
velocity amplitude law, contact timing, bridge/body coefficients and controls
remain unchanged. The burst following the string's selected stroke direction
is an unmeasured hypothesis; it could represent a separate contact mechanism.

Protocol v1 is preserved verbatim and has no internal UTC timestamp. Protocol
v2 was recorded at **04:10:30.700215 UTC**, before the first configured baseline
build at **04:14:19.504464 UTC**. Before candidate data, v2 separated the
picked-reference 8–10 dB aspiration from an identified Finger criterion.
Both v1 and v2 classifications are retained and fail; the earlier width
classification is unchanged. Baseline aspiration failures are disclosed.

The bass attack gate retains all ten Eastman Finger MIDI 40–45 rows. Its paired
own-onset 0–15 ms descriptor divides Hann 2–12 kHz power by 80 Hz–12 kHz power.
Median absolute reference error improves from 20.2478 to 18.0368 dB on Original
and from 25.6213 to 23.7818 dB on Bellido. These rows explicitly identify Finger,
but velocity is assumed. Martin's cropped, unknown-tool attacks are not primary
evidence. The native velocity estimator is separately the unchanged H5–H12
energy-sum contrast over 30–330 ms; the matched audit's mean-peak balance is a
different, labeled descriptor without an explicit noise-floor admission.

The corpus rule permits no individual total or body regression above 1% on
either preset. All target-owned residual counts remain unchanged.

| Preset | Split | Total change | Body change | Guard |
|---|---|---:|---:|---|
| Original | Picked bank training | 0.0000% | 0.0000% | pass |
| Original | Picked bank development | 0.0000% | 0.0000% | pass |
| Original | Bank flat-top | +1.0189% | +1.1360% | **fail** |
| Original | Eastman Pick | 0.0000% | 0.0000% | pass |
| Original | Eastman Finger | +0.3151% | +0.5599% | pass |
| Original | Martin, assumed Finger | +2.2094% | +3.1841% | **fail** |
| Bellido | Picked bank training | 0.0000% | 0.0000% | pass |
| Bellido | Picked bank development | 0.0000% | 0.0000% | pass |
| Bellido | Bank flat-top | +0.8960% | +0.0980% | pass |
| Bellido | Eastman Pick | 0.0000% | 0.0000% | pass |
| Bellido | Eastman Finger | −0.4801% | −0.4597% | pass |
| Bellido | Martin, assumed Finger | +1.4759% | +1.7519% | **fail** |

All 246 baseline corpus files and 96 native Finger files match exactly. All
144 candidate Pick files and 96 Thumb probes remain exact. The 192 native
source comparisons preserve all recorded nonprojection fields and wave-history
hashes. Maximum squared gain-vector norm error is 7.8164×10⁻⁸; maximum normal
direction-share error is 3.2300×10⁻⁸. Euclidean coefficient norm does not imply
equal injected work, microphone power or perceived loudness.

Finite-output, unchanged first-nonzero, ≤1 ms own-onset and ≤3-cent settled/early
pitch guards pass. Six actual same-string repeated attacks per condition
verify that the retained burst copies its original projection; all rotated
cases have a different new projection. The selected existing digital phase
and port-passivity regression passes both conditions. These bounded checks
do not certify every transient or a complete capture-energy/passivity model.
The fixed-draw direction screen is rejected without using the seed ensemble
to alter its gates or select a direction.

## Common pluck center

The initial source snapshot contains the authored centers `p−0.006` and
`p+0.009`; later comments explicitly use them to decorrelate the two combs.
No separate documented verdict selects their separation. The listener-selected
period correction and position law remain authoritative. Missing documentation
does not authorize a shipping change.

The ignored candidate uses `p` for both transverse planes and all three picking
tools. Gaussian width, slip, position law, take jitter, burst/contact noise,
polarization mix, every calibrated value and RNG draw order stay fixed. The
normal static release force is naturally recomputed from the same changed
geometry, with the listener-selected 0.8 springback share preserved and no gain
compensation. Its body-term changes cannot be attributed solely to comb
alignment. The complete protocol was saved before builds or metrics.

All 246 frozen baseline corpus files match cached audio exactly before candidate
scoring. The same per-split, per-preset 1% total/body rule rejects seven of the
twelve comparisons. Row labels, controls, calibration, every term count and all
eligible dynamic-group velocity counts are unchanged.

| Preset | Split | Total change | Body change | Guard |
|---|---|---:|---:|---|
| Original | Picked bank training | +2.7086% | +1.2077% | **fail** |
| Original | Picked bank development | +3.0452% | +0.6057% | **fail** |
| Original | Bank flat-top | −2.1093% | −3.5666% | pass |
| Original | Eastman Pick | +2.2035% | −3.6519% | **fail** |
| Original | Eastman Finger | −0.6695% | −5.0656% | pass |
| Original | Martin, assumed Finger | +5.9263% | +4.3884% | **fail** |
| Bellido | Picked bank training | +1.8166% | +1.9259% | **fail** |
| Bellido | Picked bank development | +2.2625% | +2.6716% | **fail** |
| Bellido | Bank flat-top | −2.6152% | −3.0867% | pass |
| Bellido | Eastman Pick | +0.8254% | −2.8102% | pass |
| Bellido | Eastman Finger | −1.7786% | −3.6644% | pass |
| Bellido | Martin, assumed Finger | +3.4425% | +3.6705% | **fail** |

An isolated friend observes unit-frame rest histories at `p=.2/.25`, before
body or audio processing. An integer written-line period removes delay/filter
coordinate ambiguity. Pick release velocity share is set to zero equally only
in this isolation fixture. Both planes and all tools recover the expected
H5/H4 geometric comb zeros; the authored-center negative control fills them.
This tests source geometry, not full dispersive-waveguide modal initial states,
finite fingertip force centroids or microphone nulls. None of the recording
rows measures contact position or direction.

All 288 acoustic native cases per condition cover the eight notes, both
velocities, three tools, three rates and both presets. They pass finite-output,
read-only observer byte parity, unchanged first-nonzero, onset/pitch and
identical RNG-draw guards. Maximum fitted onset change is 0.03125 ms; maximum
settled median pitch change is 1.222821 cents; early-relative change is
1.358119 cents. The 96 baseline Finger files also match the retained historical
fixture exactly.

Six positive-impedance reciprocal ports remain present. Actual active body and
heave/rock bridge coefficients, residues, mode indices and flags stay stable
and bit-identical between conditions. The 18 explicit Original bridge/radiation
twins are read back through their respective bilinear/impulse-invariant maps at
three rates: 54 observations, maximum difference 0.000243 cents and relative
Q difference 0.00012385. Different discretizations do not require equal complex
digital roots, and no assertion that every bridge mode has a radiation twin is
made. These observations do not constitute a full string/hand/body energy
certification. No additional broad certification or timing test follows the
failed corpus screen.

## Retained methods and evidence

The companion JSON follows the existing report convention: `report_inputs`
carry original paths, SHA256 and parsed data; `laboratory_tools` carry exact
source text and its SHA256; `offline_hooks` contain minimal experiment patches.
Both direction protocols, the seed and center protocols, complete compact
summaries, validations, native/source observations, command/source/binary
provenance and all 24 candidate corpus score reports, including weighted
residuals, are embedded. Existing baseline reports are referenced by document
hash, with experiment parity proofs retained. No audio, bulk sample-bank data
or raw partial JSONL is committed; their hashes/counts and regeneration methods
remain available.

Seed `.executed.py` files preserve the actual drivers/analyzer/validator; their
original entry points lived one directory above the output folder. The portable
direction safety runner differs from its retained executed runner only in
where it reads the archived body-test source; both are preserved and labeled.
Center `run.py`, `run_probes.py`, `analyze_native.py` and `finalize.py` preserve
native build/render, source tests, analysis and lightweight packaging methods.
Fresh reproduction directories must preserve frozen sources and pinned corpus
inputs and must not overwrite existing evidence. The machine record retains
exact executed commands, compiler flags, protocol ordering and a read-only
embedded-input hash-validation program.

Documentation packaging validates 298 recorded SHA256 entries against retained
artifacts, frozen source copies and the frozen Git checkpoint. The embedded
validator checks all 53 parsed report inputs, 25 source texts, three patches
and 24 candidate residual reports (198,396 residuals). The earlier seed validator's
3,264 finite saved files and 3,072 seed audio/guard hash checks are retained
as historical execution; packaging does not rerender or refit them.

```sh
python3 -c 'import json,pathlib,sys; d=json.loads(pathlib.Path(sys.argv[1]).read_text()); exec(d["execution_and_reproduction"]["embedded_input_validation_program"])' Docs/realism-seed-and-geometry-2026-10-02.json
```

No listener-selected sound is changed and neither candidate is promoted.
Same-instrument recordings with known Finger dynamics and contact geometry
would provide stronger identification than another global source or body
retune against these transfer benchmarks.
