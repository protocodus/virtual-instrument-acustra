# Body, bridge and capture research — 2026-10-16

Baseline inspected: `7b2b4c2659952f32560133a7eab21e48ae5779c1` in `/Users/vojta/Dev/acustra-realism-20261016`. This is a research/audit note; no DSP changes or listening claims are recorded here. Line numbers refer to that baseline.

## Recommendation

Two bounded candidates merit implementation trials: **a geometrically coherent early-reflection capture**, and **correction of the room's sample-rate-conversion coloration**. The first changes the modeled recording; the second fixes a demonstrable numerical discrepancy. A neck/fret structural port is a real omission, but the available evidence does not justify inventing its parameters for this quick-win pass.

The existing implementation already has measured bridge mobility, shared bridge/radiation poles, heave/rocking force-pair inputs, sympathetic strings, low-mode plate/hole spatial projection, a component-level piezo chain, and a centered room bass field. These are not new proposals. `MeasuredBridgeData.h` also explicitly declares its flamenco-to-steel adaptation and its unmeasured collocation assumption; replacing it with another uncalibrated generic bridge is not an improvement.

## 1. Directional room capture with physical left/right arrival times

**Gap in this code.** The source is currently the close-microphone pair's coherent mid, after capture voicing: `AcustraEngine.cpp:12557` sends `0.5 * (reference * dryLeft + reference * dryRight)`. `RoomTap` at line 12638 contains one delay and two scalar gains. `RoomAmbience::step` at line 12907 reads the same delayed sample for both microphones. Thus the early field has amplitude panning but no receiver-specific path time, and a component canceled in the close pair's mid cannot excite the room. This is separate from the existing direct capture projection and low-frequency wet-side coherence.

**Primary evidence.** Allen and Berkley's image-source construction computes a separate path length from each image to each receiver, with inverse-distance gain and propagation time; it is not a pan law. [1979 paper, author copy](https://jontalle.web.engr.illinois.edu/Public/AllenBerkley79.pdf), [accessible university mirror](https://users.umiacs.umd.edu/~ramanid/cmsc828d_audio/AllenBerkley79.pdf), DOI [10.1121/1.382599](https://doi.org/10.1121/1.382599). Xu et al. explain why omnidirectional point-source/receiver ISM is insufficient, give the free-space kernel in Eq. 3 and a first-order directional pattern in Eq. 9, and test source/receiver extensions against FEM. [2023 author manuscript](https://arxiv.org/pdf/2309.03486). Their transducer experiments are not measurements of Acustra's guitar.

**Bounded design proposal (our inference).** Keep the existing room dimensions, ten reflection identities, late FDN and room send law. Generate each reflection's L/R delay from one declared source/receiver geometry, rather than moving taps by ear:

```text
r[j,c] = length(receiver[c] - mirroredSource[j])
delay[j,c] = (r[j,c] - directReferenceDistance) / 343
gain[j,c] = wallPressureProduct[j] * Dsource[j] * Dreceiver[j,c] / r[j,c]
DfirstOrder(theta) = a + (1-a)*cos(theta), 0 <= a <= 1
```

The existing room comment declares a 5.2 x 4.1 x 2.7 m room, a 0.95 m guitar height and microphones 0.32 m forward. A receiver baseline must be explicitly chosen from the existing capture geometry or documented as a prior; it is not supplied by the ten pan gains. The low-mode projection generator already uses mirrored x positions (`Tools/GenerateStudioCapture.py:103`, nominal ±0.10 m neck and ±0.15 m off-hole positions). A 0.20 m baseline bounds receiver delay difference by 0.20/343 = 0.583 ms. It should vary with image direction and vanish for symmetric central paths.

Do not label a frequency-independent cardioid as a measured full-band guitar radiation pattern. For a small first pass, receiver directivity and physical arrival time are defensible; source directivity may use only the low-mode plate/hole decomposition already supported by the model. A dual-input mid/side room excitation is a reasonable experiment for the lost-side issue, but is a model prior rather than a measured far-field reconstruction. Its total energy must be normalized and the existing low-frequency coherence preserved. Do not simply boost the room or add an arbitrary brightness shelf.

**Witnesses and acceptance.** Render identical seeded picked notes, high-position arpeggios and hard strums at Room 0%, the Rack default 10%, and a separate 50% witness. At 0%, require an exact no-op. Extract isolated wet impulses and verify each early arrival against generated geometric delay, correct L/R sign and ordering, finite output, and unchanged late decay. Measure early-window interchannel cross-correlation, mono fold-down and energy, then compare loudness-matched complete phrases. At 10%, expect a subtle change; 50% demonstrates the mechanism but cannot establish default audibility. The current send law makes Room 10% **23.21 dB below Room 50%** for the same input (`20*1.66*log10(0.1/0.5)`). Report both separately; never infer an audible default improvement from the 50% example alone.

## 2. Correct high-rate room coloration caused by low-order resampling

**Concrete discrepancy.** `RoomAmbience::prepare`, lines 12724–12726, selects `M=ceil(hostRate/64000)` and internal rate `hostRate/M`. The host-rate input takes a 9 kHz one-pole (`12815`), M samples are averaged, and the output is linearly interpolated (`12970–12996`). At host 48 and 96 kHz the internal room is exactly 48 kHz, so its taps, FDN and internal normalization are identical. The higher-rate room nevertheless loses additional high-frequency energy in its conversion path. `prepare`'s normalization at `12824–12868` calls `step` directly and does not include the input filter, block average or interpolation.

This is **not** the already-fixed FDN absorption-rate bug: `Tests/RoomAbsorptionTests.cpp` checks the actual decay implied by the per-line filters at DC and 8 kHz across rates. It never checks the full host-input/host-output transfer.

**Analytical evidence derived directly from this code.** For a sinusoid below the internal Nyquist frequency, define host angular frequency `w=2*pi*f/Fs`, `a=1-exp(-2*pi*9000/Fs)`, and `B=sin(M*w/2)/(M*sin(w/2))`. The input one-pole magnitude is `P=a/abs(1-(1-a)*exp(-j*w))`. The moving-average downsampler contributes B and the principal image of linear reconstruction contributes B². Hence the principal room-path magnitude outside the unchanged internal room is `P*B³`. This isolates conversion coloration; it is not a measured listening result.

| Frequency | Host 96 kHz relative to host 48 kHz, same 48 kHz internal room |
| --- | ---: |
| 1 kHz | -0.018 dB |
| 4 kHz | -0.292 dB |
| 8 kHz | -1.179 dB |
| 12 kHz | -2.690 dB |
| 16 kHz | -4.881 dB |

The internal early 6.5 kHz rolloff and source spectrum reduce the absolute energy of the upper witnesses; these figures must not be reported as equal-sized changes to the complete guitar output. The discontinuous factor change at 64000/64001 Hz also changes this conversion response even though the intended acoustic room barely changes.

**Bounded correction.** Replace the block-average/linear conversion with a short fixed-storage, unity-DC bandlimited interpolation/decimation design; precompute coefficients at prepare time. Preserve the chosen room's acoustic bandwidth, and keep its filter/reference behavior consistent between host rates. A proper fractional-delay interpolator is preferable to compensating the observed loss with an arbitrary EQ. Laakso et al. is the relevant original filter-design paper: [Aalto publication record](https://research.aalto.fi/en/publications/splitting-the-unit-delay-tools-for-fractional-delay-filter-design/), DOI [10.1109/79.482137](https://doi.org/10.1109/79.482137). Full text was not accessible through the browser in this audit; the response derivation above comes from the source code, not an attributed equation from that paper.

An even smaller experiment can improve just reconstruction first, but must report the residual downsampling and one-pole discrepancy rather than claiming full rate invariance. Do not weaken the antialias path for speed. Include any extra group delay in early-arrival compensation; changing dry-path latency is unnecessary.

**Witnesses and acceptance.** Through a friend-access probe, run the real `RoomAmbience::process` with long sinusoids at 48/96 kHz and verify the table before changing anything. Then require agreed passband error (suggested trial target ≤0.25 dB through 8 kHz and ≤0.5 dB through 12 kHz), image/alias rejection, preserved DC gain, finite tails and eventual exact silence. Also compare 44.1/88.2, 64/64.001, 176.4/192/384 kHz and low supported rates. At equal internal rates compare complex transfer after accounting for declared latency; otherwise compare smoothed wet spectra and physical decay. Make matched musical A/B at 96 kHz at Room 10% and 50%. It produces no 48 kHz A/B benefit if the 48 kHz path is deliberately retained, and must not be counted as a default 48 kHz tonal enhancement.

## 3. Neck/fret endpoint structural coupling — real omission, defer

`finishVoice`, `AcustraEngine.cpp:11156–11169`, folds a rigid nut inversion into the full-round-trip line and subtracts only the bridge displacement. `renderBody` at line 11609 receives bridge force and moment, not a fret reaction. There is therefore no frequency-dependent structural admittance or radiating path at the stopped endpoint.

Boullosa measured fret admittance and found that the fretted endpoint can supply energy to the body, particularly in some frequency bands at high frets: [2001 publisher abstract](https://www.catgutacoustical.org/journal/nov01.htm), [2003 author-institution record](https://produccion.siia.unam.mx/Publicaciones/ProdCientif/PublicacionFrw.aspx?id=362310&scopus=0). Woodhouse explicitly discusses this extension in his synthesis work; [author-hosted 2004 paper](https://euphonics.org/wp-content/uploads/2022/03/Guitar_I.pdf). These sources support the missing mechanism, not a universal numerical neck response for a steel-string guitar.

A physical implementation would require a passive additional port `Yff(s)=sum b_k^2*s/(s^2+2*d_k*s+w_k^2)` and, preferably, bridge/fret cross admittance and a radiation observer from the same modes. The necessary mode frequencies, residues and damping for this guitar are not identified by the current bridge-only dataset. Adding an arbitrary resonant EQ or body impulse on every fretted note would fabricate a measurement and could double-count energy. Do not implement it merely to reach a feature count. A future witness should compare the same pitch played on different strings/frets and inspect endpoint power, decay and body transients, with measured high-fret transfer data.

## Ideas explicitly rejected as duplicates or unsupported quick fixes

- Replacing measured body modes, adding generic bridge loss, directional low-mode projection or wet bass narrowing: already present.
- An unconditional extra 0.3/343 second microphone delay: the measured force-pair residues retain raw phase (`MeasuredBodyData.h:5`, `GenerateBodyForcePair.py:10`); adding a bulk delay could double-count measured propagation. Only the auxiliary low-mode projection removes a common normal-distance reference, explicitly documented in `GenerateStudioCapture.py:76–87`.
- Random body poles, per-note room variation, invented piezo resonances or constant broad EQ: no primary evidence or matched residual supports these.

## Handoff

Recommend trial 1 as one spatial-capture improvement and trial 2 as a separate numerical correction. Keep baseline and individual-feature switches for deterministic ablation. Do not count the unimplemented fret path or a change audible only at exaggerated Room as a proven improvement to ordinary use.


## Implemented room candidates and measured witnesses (2026-10-16)

Both room candidates are implemented behind independent `PerformanceRealism`
flags: `directionalRoom` and `roomRateConversion`. The engine passes both at
prepare and re-prepares the room when setup-time realism flags change. The
private room helper's default arguments retain the legacy configuration for
existing isolated tests. Both engine flags default on; Room 0 remains exactly
the same dry guitar with the same seven-sample output latency.

### Geometry and receiver directivity

The ten existing surface identities now share one explicit rectangular room:
5.2 × 4.1 × 2.7 m; source (2.3, 1.5, 0.95) m; left receiver
(2.2, 1.82, 0.95) m; right receiver (2.4, 1.82, 0.95) m. Both parallel cardioids
face −Y. This **20 cm cardioid pair is a declared model assumption**, not a
measurement of the existing guitar or its source radiation. The input remains
one coherent microphone-mid signal. Guitar heave/rock radiation is not
reconstructed from that already mixed observation.

For each image and receiver, the path uses excess delay `(r − r_direct)/343`,
pressure spreading `r_direct/r`, the existing surface reflection coefficients,
and receiver sensitivity `0.5*(1+cos(theta))`, divided by direct sensitivity.
This implements the receiver portion of Xu et al. Eq. 9 and Allen–Berkley path
geometry. Four-point Lagrange interpolation resolves the fractional arrivals;
its small upper-band interpolation error is not an additional measured guitar
radiation feature. The opposite side walls now reach opposite receivers first.
The rear-wall arrival is strongly attenuated by the cardioid's rear null.

Measured directly from isolated `step()` reflection impulse first moments:

| Witness | Measured |
| --- | ---: |
| Floor excess travel time | 4.64750 ms |
| Left-wall right-minus-left arrival | 0.581684 ms |
| Right-wall left-minus-right arrival | 0.582205 ms |
| Maximum physically allowed 20 cm ITD | 0.583090 ms |

The accepted FDN line lengths, absorption design, Hadamard mixing, input signs,
Schroeder allpasses and decay targets are unchanged. The existing preparation
normalisation still sets late energy to 1.5 times early energy for the same
1 kHz-weighted input and normalises their sum. It is recomputed for the new
receiver geometry rather than adding an arbitrary tone curve or level boost.

### High-rate conversion: full-path defect and correction

The pre-change full `RoomAmbience::process` path was driven by steady tones
(amplitude 0.125; two seconds settling plus one second complex measurement).
At 48 and 96 kHz both FDNs run at exactly 48 kHz, so this separates conversion
colour from delay-grid or absorption changes. The measured 96/48 wet ratios
were −0.018225 dB at 1 kHz, −0.292349 dB at 4 kHz, −1.17902 dB at 8 kHz,
−2.69012 dB at 12 kHz and −4.88049 dB at 16 kHz. These are measured wet-path
errors, not a claimed change in the full dry-plus-room guitar. See
`room-full-wet-baseline.txt`.

The corrected branch replaces the block average and linear reconstruction with
an anti-alias decimator plus polyphase anti-image interpolator. Each uses a
Blackman-windowed sinc of order `64*M`, cutoff `0.4375*internal_rate` and a
passband through `0.375*internal_rate`; M is the whole rate-reduction factor.
The original 9 kHz room pole moves onto the internal grid. It is not discarded,
boosted or weakened to make an alias test pass. The FIR pair's combined fixed
group delay is exactly **64 internal samples** and is subtracted from every
early reflection and from the 8 ms late input delay. The dry path is untouched.

As with any compensated linear-phase band-limiting kernel, a small symmetric
pre-ring exists around a reflection's physical centre; finite support begins
before that centre. The physical timing claim is the transfer's group delay
and the reflection centre, not the first nonzero floating-point sample. At
48/96 kHz the combined support extends ±1.333 ms around that centre. There is
ample excess path delay to keep all storage delays positive. At rates with
M=1 the conversion flag preserves the existing arithmetic exactly.

Measured complete stereo wet-path results (`RoomCaptureTests`, including the
FDN, input colour, early filtering, and both conversion filters):

| Witness | Result |
| --- | ---: |
| Legacy 96/48 kHz wet ratio at 12 kHz | −2.69012 dB |
| Corrected worst magnitude difference, 48/96 and 64/384 kHz, both geometries, DC–16 kHz | 0.000392354 dB |
| Corrected worst phase difference for those complete paths | 2.09551e−7 rad |
| Worst measured folded alias, 96/384 kHz | −83.7681 dB |
| Worst measured reconstructed image, 96/384 kHz | −116.992 dB |
| Worst whole anti-alias stopband sweep through 768 kHz | −75.2882 dB |

These measurements substantially exceed the test's 0.01 dB/0.001 rad passband
bounds, 70 dB complete alias/image bounds and 74 dB filter stopband bound.
No test threshold was relaxed. Details are in `room-capture-validation.txt`
and per-frequency corrected measurements in `room-full-wet-corrected.txt`.

### Regression and listening scope

- `RoomCaptureTests.cpp` additionally covers all 17 rates from 8 through 768 kHz,
  including the 64000/64001 boundary; finite output, eventual exact silence,
  deterministic reset, fractional-arrival DC gain, geometry bounds, late
  flag changes, and exact dry output for all four flag combinations at Room 0.
- Existing `RoomAbsorptionTests` and `RoomBassCoherenceTests` pass unchanged.
  Absorption still matches its reference with maximum relative DC/HF errors
  2.2947e−7 / 1.7381e−7 across 128 line/rate cases.
- Both room flags off were compared byte-for-byte against the original HEAD
  implementation (`7b2b4c2659952f32560133a7eab21e48ae5779c1`) compiled with
  the same C++17/O2 compiler. The comparison covered one second at each of
  8000, 44100, 48000, 64001, 96000, 192000 and 384000 Hz, using the same
  1024-sample deterministic broadband drive followed by silence. Both streams
  have SHA-256 `e8edd8d9f97c96affe642b42d11585fd44451209bde83d618fcd44a1cb86e21b`.
- The meaningful high-rate listening witness must be rendered at **96 kHz**
  and then converted identically to 48 kHz for convenient playback. Rendering
  only at 48 kHz cannot reveal `roomRateConversion`.
- Listen separately at Rack default Room 10% and diagnostic Room 50%. The
  former send is 23.2058 dB quieter; the numerical improvement does not by
  itself establish that either change is audible at the default mix. Keep
  native relative levels and one common playback gain across each A/B.
