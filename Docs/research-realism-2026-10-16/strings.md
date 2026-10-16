# Wound-string handling research — 2026-10-16

Status: two experimental candidates implemented after the parent resumed the realism task. Audited baseline: Acustra `7b2b4c2659952f32560133a7eab21e48ae5779c1` (origin/main, 1.5.0). The source/test checkpoint is ready for the parent's integrated validation. **Neither candidate is claimed here to be a proven audible improvement at the default setting. The parent has disabled `longitudinalSqueak` by default: it remains an explicit experimental ablation pending credible coupling and listening evidence.** These changes are separate from the prior Rack Extension build.

## Implementation checkpoint and observed limits

Implemented the independent offline switches `woundRidges` and `longitudinalSqueak` in `renderFretMotion`, `Voice::FretMotion`, and, after explicit scope extension, a separate axial-force observation path. The parent owns flag definitions and build integration. Existing FretMotion tests passed unchanged. New `Tests/WoundSlideTests.cpp` passed a direct C++17 `-O2 -Wall -Wextra -Wshadow -Werror` build/run. It covers separate spectral witnesses, low-rate alias suppression, force RMS across rates, mode position/tension behavior, source termination/zero amount, exact host-partition determinism, stationary/plain-string no-ops, fractional axial travel at 8 kHz/48 kHz/maximum Fs, pending-packet retention, and scoped panic.

Actual ridge design: four `1/k` harmonics, 10% stationary force-variance share, a 1 ms amplitude-following pole, and per-harmonic smooth fading from `.25 Fs` to `.40 Fs`, returning faded energy to the original broadband component. An eleventh-order range-reduced sine plus a harmonic recurrence avoids audio-rate transcendental calls. The waveform approximation's absolute error is below `6e-8`; this is alias suppression to finite numerical accuracy, not a claim of mathematically perfect bandlimiting under arbitrary modulation.

Actual longitudinal design, revised force-path version: three cached coupled-form oscillators, full 0.648 m axial length, EA/mass guidance, position-dependent signed excitation weights, and authored 35 ms T60. Paper Eq. 4 gives the bridge-force impulse coefficient `EA/(mu*L^2) * k/f_k = 4*f_1`; the discrete input coefficient is therefore `4*f_1/Fs`. Each new source sample is weighted by `sin(k*pi*x/L)`, while previously stored modal energy keeps its own amplitude. This replaces the first prototype's arbitrary normalized-EQ scale. No force-level increase was made to the existing broadband source.

A dedicated 512-float per-voice queue carries that force to the saddle after `x/c_L`, with convex fractional taps. Starting/refretting a contact preserves already emitted packets; panic/reset clears them. The truncated modal sum cannot reproduce the exact pre-arrival cancellation of an infinite axial model; explicit travel is a declared causal approximation. At 768 kHz the slowest axial speed needs fewer than 260 samples. Idle-state handling includes the queue.

The saddle's downward-force projection is `sin(12 degrees)*deltaT`, using an **authored rear break angle and an equal-tension assumption around the saddle**. [Kemp, The acoustical behavior of a bass guitar bridge with no saddles, PLOS ONE (2023)](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0292515) explains the relationship between break angle and saddle contact force; it does not measure this guitar's angle or prove equal dynamic tension across its frictional saddle. The projected force and its across-saddle moment feed the existing body observation and string-weighted piezo force before capture, room, gain and limiting. No raw post-capture audio, new acoustic gain, unmeasured neck-axis rocking transfer, or axial feedback into the transverse bridge was added.

With no input each modal state contracts by its fixed radius `r`. For bounded force `Fmax`, each state norm is bounded by `(4*f_1/Fs)*Fmax/(1-r)`; all three modes and the finite projected sum remain bounded. This is an external-contact-force observer with authored damping, **not a passivity proof for a complete coupled axial/body system**. It represents more of the paper's mechanism but still lacks calibrated axial radiation and absolute hand-force evidence.

Observed revised spectral/rate results: low-E axial fundamental 1480.8 Hz; isolated modal/adjacent-band power ratio 14.01. Transverse default-amount force RMS with both flags at 44.1/48/96 kHz was `1.80945e-6 / 1.81165e-6 / 1.77353e-6`; axial force is now observed separately. An impossible 12 kHz ridge at an 8 kHz engine rate emitted zero periodic force. The emitted second harmonic of a 2.8 kHz ridge was suppressed at the same low rate.

Both-off parity was independently checked by replacing only `renderFretMotion` in an otherwise identical current-source snapshot with the original HEAD implementation, compiling both snapshots, and comparing 8 seconds of stereo float output containing late slides. The 3,072,000 output bytes were identical. Both SHA-256 values: `da6fb47986d83bec9aa589a4b88551e092d36224b19a9f09fb93645fe668b9c9`. This validates this region's both-off baseline; it does not claim that unrelated candidates elsewhere match upstream with their flags enabled.

The first short musical witness (bare engine capture defaults, output gain `.42`, no piezo blend/room, ReleaseNoise `.1`) produced differences of −71.76 dB for ridges and −79.14 dB for axial relative to the full phrase. Therefore a second matched late-slide probe used the parent's review controls: output gain 1, piezo mix `.2`, Room `.1`; A-string note 52 at time `.2 s`, velocity 96/127, continuously held; a 0→4 semitone slide at 6–6.5 s then 4→0 at 6.5–7 s, with 10 ms updates. Measurements below cover 6–7.5 s; there is no individual-file normalization or difference amplification.

| ReleaseNoise | Difference | Difference RMS dBFS | Difference relative to old late audio |
|---|---|---:|---:|
| .1 | Existing broadband motion vs zero noise | −85.45 | −18.79 dB |
| .1 | Ridge candidate vs old broadband | −94.49 | −27.83 dB |
| .1 | Axial candidate vs old broadband | −105.91 | −39.25 dB |
| .1 | Both candidates vs old broadband | −94.20 | −27.55 dB |
| .5 | Existing broadband motion vs zero noise | −71.47 | −6.00 dB |
| .5 | Ridge candidate vs old broadband | −80.51 | −15.04 dB |
| .5 | Axial candidate vs old broadband | −91.93 | −26.46 dB |
| .5 | Both candidates vs old broadband | −80.22 | −14.75 dB |

Preserved revised raw float files, same-gain float WAVs, source snapshots, parity binaries, renderer, manifest and CSV: `/tmp/acustra-wound-late-slide-v2/`. The first prototype remains under `/tmp/acustra-wound-late-slide/`; its axial change was only −113.71/−99.72 dBFS at .1/.5. The parent should copy desired evidence into its final artifact bundle before temporary-file cleanup.

**Listening recommendation:** retain the `.1` matched pairs and label the `.5` pair as the existing nominal-level diagnostic. Ridge structure may be easier to identify in the quiet nominal-level tail; that does not prove default audibility. The revised axial path improved its late-slide contribution by about 7.8 dB but the difference remains too faint to count as an achieved audible improvement from these results. The force scale is an authored engine-unit prior, whose physical conversion and measured range are documented below; calling the prior itself “micro-newton” without that conversion was imprecise. The papers do not provide an absolute calibration that justifies raising it merely to pass an audition. Keep the axial mechanism disabled by default as a documented experiment, and do not count it among achieved audible wins. No listening claim was inferred from a nonzero numeric difference.

## Force-unit and excitation audit

The runtime conversion is `NewtonsPerUnit = steelDisplacementScaleMetres * 48000`. The `PhysicalCalibration` member fallback is `.0061 m`, which would give **292.8 N per engine force unit**. However, the shipped `fittedPhysicalCalibration` aggregate explicitly overrides it with `.00773577847 m` (`Source/DSP/FittedPhysicalData.h:165`), and the engine initializes from that fitted aggregate. A fresh runtime `getPiezoNewtonsPerUnit()` measurement therefore gives **371.317371726 N/unit**. Do not substitute the member fallback for the actual fitted calibration. The axial transfer's input and output use the same engine-force units, so its dimensionless `4*f1/Fs` coefficient requires no additional 292.8 or 371.317 multiplier.

`initialisePluck` seeds `voice.level` to at least `.02 * MIDI_velocity` and copies it into `peakLevel`; `finishVoice` retains the maximum smoothed local reaction-force magnitude. The motion source then uses `.00004 * peakLevel * (ReleaseNoise/.5) * contact * speedLevel`. Thus a sustained quiet string can still scrape, but its new left-hand source remains dependent on the earlier right-hand stroke. That dependence is an authored normalization, not a measured friction law. Removing it would require a separately justified source scale; the cited papers do not supply one. No such DSP change was made.

A fresh sample-by-sample trace of the same A-string late-slide score (6–7.5 s measurement, ridges off, axial explicitly on) gives the following RMS values with the shipped fitted calibration. The velocity-96 `peakLevel` before the slide is `.01511811` engine units, or 5.61362 N in the engine's force convention. The observed slide's peak smoothed speed is `.40855 m/s`; its 10 ms controller steps are not a constant-speed laboratory fixture.

| MIDI velocity | ReleaseNoise | Input force RMS | Axial force RMS before saddle projection | Projected axial force RMS |
|---:|---:|---:|---:|---:|
| 32 | .1 | 10.11 µN | 25.89 µN | 5.38 µN |
| 96 | .1 | 30.33 µN | 77.68 µN | 16.15 µN |
| 127 | .1 | 40.13 µN | 102.77 µN | 21.37 µN |
| 32 | .5 | 50.55 µN | 129.47 µN | 26.92 µN |
| 96 | .5 | 151.66 µN | 388.41 µN | 80.76 µN |
| 127 | .5 | 200.63 µN | 513.84 µN | 106.83 µN |

These are simulated dynamic source/observer forces, not measured finger normal loads or calibrated acoustic levels. At velocity 96 the input force peaks at 163.40/817.00 µN for .1/.5; axial force peaks before projection at 394.16/1970.81 µN. In contrast, the isolated spectral test deliberately sets `peakLevel=.4`, so its approximately `1.81e-6` engine-unit RMS corresponds to **0.673 mN**, not 1.81 µN. Raw trace source, executable and CSV are preserved in `/tmp/acustra-wound-force-units/`.

The hypothesis that the RC source starves every axial mode was also checked analytically. At 48 kHz, A-string speed .05 m/s gives ridge harmonics only through 960 Hz, below its 1684 Hz first axial mode. But 90% of the force variance remains broadband. The exact normalized two-RC PSD at axial modes 1/2/3 is **+4.66/−1.05/−4.44 dB** relative to equal-RMS white noise; at .1 m/s the values are **+6.73/+1.62/−1.64 dB**. An independently zero-mean, exponentially gated white-noise pulse train has broadly white ensemble PSD: simply replacing the RC force with it at equal RMS would reduce excitation of the dominant first axial mode. A more detailed periodic-impact model could alter transients and upper partials, but this check found no missing spectral multiplier that justifies a production correction. V2 DSP is preserved.

## Absolute-calibration evidence and decision

[JASA 2007](https://aaltodoc.aalto.fi/server/api/core/bitstreams/475276ab-f408-4ef4-a46f-5528bb8ca63d/content), section 4.2/Figure 2, reports 20 fingernail slides over 23 cm and normalizes recorded RMS to 0–1. No force sensor, controlled normal load, or calibrated SPL is reported. The individual slip pulse in its force-train model is not assigned a measured Newton scale. [Pakarinen's thesis, page 65](https://aaltodoc.aalto.fi/server/api/core/bitstreams/71b28ad0-ed91-4abd-9a9e-02fc79f4b7ef/content) explicitly distinguishes this velocity-to-sound study from friction studies of velocity versus force.

The [NIME 2008 contact generator](https://www.nime.org/proceedings/2008/nime2008_049.pdf) uses short exponentially decaying noise bursts at winding crossings, with separate harmonic shaping and longitudinal spectral filtering. Motion drives contact while right-hand plucks enter separately. The longer [Virtual Slide Guitar paper, pp. 47–50](https://aaltodoc.aalto.fi/server/api/core/bitstreams/b890243c-fd3e-435c-a755-b98bd860507a/content) chooses its velocity gain empirically and includes an independent user gain. These sources support separating left-hand contact from right-hand plucks, but do not determine a replacement absolute force. [Evangelista's AES 2012 model](https://liu.diva-portal.org/smash/get/diva2:535809/FULLTEXT01.pdf) supplies an explicit longitudinal friction/contact formulation for a rigid bottleneck, not calibrated soft-fingertip coefficients.

The existing gain also reflects an explicit user preference. `Docs/fret-motion-2026-10-11.md` records the request for “a little fret noise” and “not to overdo it”; `.00004` was intentionally about 8 dB below the first render and 8% of the nominal release-brush scale. The README's roughly dozen usable GuitarSet mono slides show confounded broadband excess, not an isolated wrap-force calibration. Preserve that prior. **Recommend `longitudinalSqueak=false` by default pending calibrated coupling and an audible, useful matched witness.** A common +30 dB crop of both .5 late-tail files is a legitimate labeled diagnostic; it does not establish audibility at unaltered playback or license a shipped gain increase.

The remaining sections preserve the initial research/design proposal; the checkpoint above states which details were actually implemented and validated.

## Existing model and actual gaps

`Source/DSP/AcustraEngine.cpp:10948–11061`, `renderFretMotion`, already detects continuous held-finger movement on the four wound strings, derives physical speed from smoothed speaking length, scales a broad random band by speed and ReleaseNoise, and launches finite-travel transverse packets. It does not run for ordinary fixed-fret MPE tension bends, vibrato, plain strings, harmonics, first contact, or hammer/pull articulation. Preserve those distinctions.

The source explicitly acknowledges at 11056–11058 that it is not a longitudinal squeak model. It has neither periodic winding structure nor longitudinal modal resonances. Its two RC filters move a noise band's center with speed; that is not the same as either missing mechanism.

Other possible guitar improvements are already substantially implemented: compliant finite contact, directional excitation, geometric tension/attack-pitch motion, two string polarizations, physical loss/dispersion, coupled bridge, release contact, and variation in performance. Do not count another general “pick noise”, “inharmonicity”, or “humanization” patch as a new mechanism without narrower evidence.

## Primary evidence

Pakarinen, Penttinen and Bank, **Analysis of handling noises on wound strings**, JASA 122(6), EL197–202 (2007), DOI [10.1121/1.2786607](https://doi.org/10.1121/1.2786607), [author-repository paper](https://aaltodoc.aalto.fi/server/api/core/bitstreams/475276ab-f408-4ef4-a46f-5528bb8ca63d/content). Measurements distinguish speed-dependent winding harmonics from longitudinal resonances. Harmonic frequency and noise RMS approximately follow sliding speed. Longitudinal frequencies depend on length and axial wave speed, not tension; excitation position changes modal weights. Fingertips suppress transverse motion strongly. Capo experiments shift longitudinal resonances with the constrained length. These findings support two distinguishable mechanisms, but do not calibrate this product's particular strings, finger, radiation transfer, or absolute noise gain.

Puputti, Pakarinen and Välimäki, **Slide Guitar Synthesizer with Gestural Control**, NIME 2008, pp. 49–52, [primary proceedings PDF](https://www.nime.org/proceedings/2008/nime2008_049.pdf), section 2.1 and figure 2. The implementation separates a speed-driven noise-pulse train/resonant harmonic structure from a static fourth-order filter representing longitudinal modes and general spectral shape, with a balance between them. It supports an economical contact-color model; it does not validate Acustra's numerical parameters. A bounded harmonic oscillator bank is our proposed anti-aliasing alternative to its nonlinear waveshaper.

## Proposal A: winding-ridge structure

Keep the existing contact test, speed envelope, random stream, physical force scale and packet routing. Add a quiet, quasi-periodic component to the moving-contact force, with its phase driven by traveled distance:

```
v = Fs * (length[n] - length[n-1])
f_ridge = abs(v) / windingPitch
phase[n] = frac(phase[n-1] + abs(length[n] - length[n-1]) / windingPitch)
p[n] = sum(k=1..4, a_k * fade(k*f_ridge/Fs) * sin(2*pi*k*phase[n]))
```

The absolute-distance phase is a deliberate direction-symmetric synthesis choice. Real surface retracing need not produce identical microscopic impulses. Keep phase between uninterrupted slides; do not reset on every velocity update. Use a shared interpolated sine table, or oscillator recurrences, to avoid audio-rate transcendental calls. A four-harmonic maximum bounds cost; no unfiltered impulses or nonlinear waveshaper.

Initial winding-pitch proxies can retain the current spectral anchors: `0.5 / {2000,2400,2900,3500}` metres = `{0.2500,0.2083,0.1724,0.1429}` mm. These are **authored synthesis pitches**, not measured EJ16 wrap diameters. Do not infer winding pitch from the existing effective bending-core diameters.

Keep the old broadband component. Normalize `p` by its known sum-of-squared harmonic weights and mix an initial **10% force-energy share**, e.g. `sqrt(.9)*F_old + sqrt(.1)*F_periodic`, before the existing leaky force-to-wave integration. The percentage is a conservative listening starting point, not a paper fit. Exact normalization should include the existing shaping path if applied to the periodic branch. Preserve the current overall RMS reference (`.00004 * peakLevel * (ReleaseNoise/.5) * contact * speedLevel`), including the current bounded speed law. The paper's approximately linear speed observation does not require replacing that already-shipped protective law.

Use a smooth per-harmonic taper, unity below `.30 Fs` and zero at `.45 Fs`. Crucially, do not renormalize a fully faded fundamental back to full level: smoothly hand its energy back to the existing noise branch. Phase must remain finite even for large one-sample geometry changes. Frequency clamping alone would generate an artificial stationary whistle. Abrupt changes in phase increment can still create FM sidebands: retain the physical motion smoothing and exercise fast-step fixtures at the lowest supported Fs.

**Independent witness:** a 0.3–0.5 second, uniform physical slide at 0.10 and 0.20 m/s on low E, same random seed and ReleaseNoise `.1`. Analyze the isolated emitted force after settling. The structured ridge frequency should double; its harmonics should follow speed, unlike the old diffuse spectral centroid. Then render an actual fretted bass-string slide in a quiet phrase at the shipped `.1` setting. Listen for a faint textured zip localized to finger movement. Reject a persistent tonal whistle or a larger blanket hiss. Preserve the original loudness; do not make the demonstration audible by individually boosting the changed file.

## Proposal B: longitudinal contact resonances

Add an independently enabled fixed-size resonant branch driven by the contact source. With A disabled it must work from the original broadband force; with A enabled it can receive the combined force. Keep it a **physically informed contact-color model**, not a claim that the existing transverse bridge now solves axial string motion.

For the ideal axial string, a useful design reference is:

```
c_L = sqrt(EA / mu)
f_k = k*c_L/(2*L_axial)
positionWeight_k = sin(k*pi*x_contact/L_axial)
```

Existing material data provide a reproducible proxy: `stringAxialRigidity` at cpp:941 gives EA, while cpp:922 derives mass per length from published standard-tuning tension and a 0.648 m scale. Do not use bend-modulated tension or current transverse pitch to derive axial frequency. Using those existing proxies and the full 0.648 m length gives approximate fundamentals `{1481,1684,2079,2551}` Hz for E/A/D/G. These are predictions from effective core sizes, **not measured longitudinal resonances**; the low-E/A/D core sizes themselves are bending fits.

Boundary choice needs an explicit approximation. A moving soft pad need not clamp axial vibration like a capo. For the minimal static-color branch use the fixed nut-to-saddle length, with moving contact position controlling weights; do not automatically replace `L_axial` with every smoothed transverse speaking length. An actual capo/axial-clamp extension would need its own effective length. Document this choice and avoid claiming to reproduce the paper's capo experiment with an API that has no such boundary. The existing 18 mm finite contact offset can locate the source approximately, with position clamped within the axial string.

Two or three modal oscillators per wound voice suffice initially. A stable coupled form is convenient:

```
p += g * contactForce
q_new = r * (cos(w)*q + sin(w)*p)
p_new = r * (cos(w)*p - sin(w)*q)
w = 2*pi*f_k/Fs
r = exp(-ln(1000)/(T60*Fs))
```

With zero input, the state norm contracts by r regardless of frequency; changing output position weights does not pump internal state. Cache sine, cosine, decay and gain at preparation or material/boundary change. The proposed fixed axial frequencies require no per-sample coefficient design. Compute the few sine position weights at a fixed physical-time control cadence and interpolate, or use the shared lookup table.

An authored initial decay sweep of 15/30/45 ms and a small normalized branch share can establish whether it is useful without making a ringing metallic artifact. Use analytic or offline impulse-energy normalization per mode and account for modal correlation; do not assume `sqrt(1-r*r)` alone gives exact output RMS. Retain the current total force budget by reallocating, initially at most 10–15% of its energy, rather than adding unchecked resonator gain. This is a proposed audition range, not a measured damping fit.

Route the bounded color into the existing local contact-force/arrival path to retain finite travel, capture, room, limiter, and all sensor routes. That route approximates acoustic coupling and can add transverse coloration. A full longitudinal bridge port would require new axial impedance and radiation evidence; do not silently invent one or mix raw audio after the capture chain. If the modal signature disappears through the existing routing, report that limitation before increasing gain or claiming a complete physical model.

**Independent witness:** with winding structure off, compare isolated contact spectra at two speeds and several pad positions. Axial peak frequencies should stay fixed when speed or a fixed-fret tension bend changes; modal weights should vary with position. A direct source fixture at half/third-length should suppress the appropriate ideal-mode weights. Render a quiet bass-string slide at ReleaseNoise `.1`: listen for a brief, subdued string-specific squeak within the roughness, not a bell tail. An isolated branch can diagnose the mechanism, but only the unchanged-level full musical render establishes that the default improvement is audible and useful.

## Lifecycle, parity and validation requirements

- Add two independent offline flags, with the parent coordinating API/CMake names. Both off must execute the original arithmetic and random draws unchanged; compare sample bits with the audited baseline. No reseeding or extra consumption of `fretMotionNoiseState` is needed for the periodic or linear modal branches.
- Extend `Voice::FretMotion` (header:1203), existing `primeFretMotion` (cpp:10942), and reset paths. `primeFretMotion` is deliberately called for a new/retuned/refretted contact and connected hammer/pull (cpp:6006,9527). A same-pitch continuing repluck keeps source history; already launched packets retain their original arrival times.
- Existing `ReleaseNoise=0` semantics stop future force immediately without recalling in-flight packets. Clear the new source states there too. Key-up stops fresh RNG/oscillation excitation, holds the last contact position and drains the source through the existing bounded 120 ms tail. Set remaining modal states to exact zero at termination. Do not let a resonator keep an otherwise silent engine active indefinitely.
- Keep no-contact, plain-string, harmonic, fixed-fret MPE bend/pressure/vibrato, first-note and stationary cases unchanged. Panic, reset, preparation/sample-rate change and MPE-layout reset must clear new state. Existing packets and the retained old-note tail must neither duplicate nor vanish on refret.
- Extend `Tests/FretMotionTests.cpp`, whose fixtures already cover physical speed, block invariance, source RMS, direction symmetry, boundaries, packet travel, zero amount, key-up, refret and panic. Preserve their intent. Add isolated spectral witnesses and explicit A-only/B-only/both/both-off renders. Test low supported Fs, 44.1/48/96 kHz, fast slides, speed reversals, and near-zero motion; assert finite output and alias suppression.
- Fixed-size state only; no render allocations, locks, strings, per-sample trigonometric/exponential calls, or unbounded loops. Measure the default 2x/Room `.1` workload after implementation; do not call a concurrent-machine timing result an absolute performance qualification.
- Preserve raw matched listening renders, seeds, event times and parameter manifests. Main listening pairs use the same master gain and ReleaseNoise `.1`; optional diagnostic boosted differences are clearly separate. If the effect is merely measurable but not audible/useful there, state that rather than counting it as an achieved realism improvement.

## Deferred alternative

True fret collision/contact friction is another gap but not the minimal next change. [Evangelista, Physical Model of the String-Fret Interaction, DAFx 2011](https://www.dafx.de/paper-archive/2011/Papers/96_e.pdf) models contact-node constraints and tangential friction; [Bilbao and Torin, Numerical Simulation of String/Barrier Collisions, DAFx 2014](https://www.dafx.de/paper-archive/2014/dafx14_stefan_bilbao_numerical_simulation_of_s.pdf) addresses energy-stable collision simulation. Applying those to this collapsed waveguide would require a larger stability/geometry design and calibrated action/friction data. Do not substitute a generic buzz generator under their names.
