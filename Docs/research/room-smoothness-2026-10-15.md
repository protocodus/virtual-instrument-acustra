# A steadier stereo room bass — 2026-10-15

The requested sound investigation screened two room refinements while keeping
the eight-line room the listener retained on 2026-10-10. A small reduction of
low-frequency wet stereo width is implemented for audition. A stronger
anti-alias filter for the internally decimated room is deferred. Neither
measurement establishes a listening preference.

## Selected: high-pass only the room's side at 150 Hz

The existing room's late impulse response has almost equal mid and side
energy in the bass: after 25 ms, L/R correlation is -0.069 over 40–120 Hz
and +0.033 over 120–250 Hz. That places diffuse low-frequency energy far
apart even though the virtual microphone pair is close together.
[Cook et al., JASA 27 (1955)](https://doi.org/10.1121/1.1908122) describe
the increased low-frequency pressure correlation of a random sound field
at closely spaced points. This supports examining low-frequency width;
it does **not** identify 150 Hz or this filter as a measured microphone
model. Those are authored mixing choices.

The candidate sends the wet side through a second-order Butterworth
high-pass at 150 Hz. The original mid is retained directly. An isolated
100/150/200 Hz screen selected the middle value for the following A/B:

| Band | Wet stereo energy change, 150 Hz | Candidate L/R correlation |
| --- | ---: | ---: |
| 40–120 Hz | -2.10 dB | 0.928 |
| 120–250 Hz | -0.37 dB | 0.500 |
| 250–500 Hz | -0.062 dB | 0.395 |
| 500–1000 Hz | -0.0034 dB | 0.345 |

These are full one-second native room impulse responses at 48 kHz,
fourth-order Butterworth analysis bands, summed L/R energy, and zero-lag
correlation. The late-only numbers above use the same analysis after
cropping the first 25 ms. They are room measurements, not reductions in
the complete instrument: the dry sound remains and the normal Room setting
places the wet signal below it. The 100 Hz alternative removes 1.74 dB in
the lowest band; 200 Hz removes 2.20 dB but reaches farther into 120–250 Hz
(-0.71 dB).

`RoomAmbience::coherentSide` runs at the host sample rate after the existing
room reconstruction. Coefficients are computed during prepare; the callback
uses one double-precision biquad. Reset clears its history, and the room
remains active while its side-filter tail exceeds the existing 1e-20 floor.
The filter reaches exact silence. It changes neither feedback losses,
reflection arrival times, diffusion nor room normalization. This also avoids
the low-frequency energy restoration and added room modes of the rejected
[two-microphone experiment](room-2026-10-10.md).

The original wet-mid arithmetic is kept at the output sum. Dedicated Mono
Capture and Width zero remain sample-exact; Room zero and both Piezo routes
are unchanged. Summing the final stereo channels externally can differ by
float rounding when the changed side is added to the dry channels. Strong
overload can additionally differ because the existing channel safety
limiters are nonlinear. The normal-level test limits the external sum
difference to 1e-7 while requiring exact equality for the actual mono route.

`PerformanceRealism::roomBassCoherence` is an offline switch, default on,
with no saved or visible parameter. Switch it off to recover the preceding
room output. Audition it at Room 0.5 and 1 with Width 1, on held bass notes,
chord releases and a complete phrase, alongside the combined candidate.

## Deferred: stronger anti-alias filtering before room decimation

The existing input uses a 9 kHz one-pole and box average before integer
decimation, followed by linear reconstruction. Single high-frequency tones
show measurable folded components:

| Host rate | Source | Folded component | Baseline component, relative to source |
| --- | ---: | ---: | ---: |
| 64,001 Hz | 18,000 Hz | 14,000.5 Hz | -21.16 dB |
| 64,001 Hz | 22,000 Hz | 10,000.5 Hz | -23.70 dB |
| 96,000 Hz | 30,000 Hz | 18,000 Hz | -31.44 dB |
| 96,000 Hz | 42,000 Hz | 6,000 Hz | -26.08 dB |
| 192,000 Hz | 52,000 Hz | 12,000 Hz | -40.10 dB |

A concrete 63-tap Kaiser FIR, beta 8, cutoff 0.44 times the room's internal
sample rate, applied before the existing input, suppresses these components
by 87–107 dB. The 8 kHz response moves less than 0.0005 dB. It also adds
31 host samples of delay and callback work, and at 96 kHz reduces the
22 kHz response by 11.5 dB. The ordinary 48 kHz path does not decimate.
No normal-guitar recording in this investigation establishes an audible
benefit, so this FIR is kept in the offline experiment only.

## Reproduction and validation

Build the current source before using the static-library probe:

```sh
cmake --build build-dsp --parallel 2 --target AcustraRoomBassCoherenceTests
ctest --test-dir build-dsp -R '^Acustra[.]RoomBassCoherence$' --output-on-failure
python Tools/MeasureRoomRefinements.py --build-dir build-dsp --output /tmp/acustra-room-review
```

The output directory must be new. It contains native float impulse responses,
the compiled probe and its source, alias CSV, measurements JSON and a manifest
with source/library/tool hashes, compiler and command. NumPy and SciPy are
required. The dedicated test checks the actual filter against an independent
Butterworth response at six sample rates, reset and finite silence, excluded
route identity, stereo bounds at full Room/Width, and block partition identity.
The focused test passed against the rebuilt production library; maximum
independent magnitude error was 8.95e-8 dB. The production impulse probe
reproduced the table above; external L/R summation differed by at most
1.86e-9 from the raw impulse's sum. Saved
[measurements](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/measurements.json),
[alias trials](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/alias-comparison.csv)
and [source/library manifest](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/manifest.json)
record that run. Native float responses and the generated binary/source are
in `/tmp/acustra-room-review-evidence` in the working environment and can be
regenerated with the command above.

### Capture calibration check

The existing capture fixture constrained the room's broadband energy to
11–14 dB below a held chord. Narrowing the wet bass intentionally reduces
that stereo energy, moving this particular chord just past the old upper
bound. Before changing the fixture, its original source was built against
the frozen baseline headers and library: the entire baseline Capture suite
passed. The current engine with only `roomBassCoherence=false` reproduces
the baseline levels below.

| Host rate | Baseline guitar above room | Current guitar above room | Wet reduction | Largest mono-sum rounding |
| --- | ---: | ---: | ---: | ---: |
| 44,100 Hz | 13.8866 dB | 14.0359 dB | 0.149325 dB | 1.49e-8 |
| 48,000 Hz | 13.9071 dB | 14.0513 dB | 0.144161 dB | 1.12e-8 |
| 96,000 Hz | 13.9188 dB | 14.0630 dB | 0.144268 dB | 1.49e-8 |

The calibration fixture retains its original 11–14 dB and cross-rate
bounds with the refinement switched off. A separate check requires the
enabled refinement to remove 0.10–0.25 dB of wet energy on this fixed chord,
preserve the mono sum within 1e-7, and maintain the original cross-rate
stability. This explicitly tests the intended change without broadening
the historical calibration limit or restoring the removed bass width.
`Acustra.Capture` passed in 56.66 seconds after that fixture distinction;
no production DSP or rendered A/B audio changed. The saved
[baseline capture log](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/capture-frozen-baseline.log),
[current room log](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/capture-current-room.log),
[refinement-off control](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/capture-current-refinement-off.log)
and [hash manifest](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/sound-refinements-2026-10-15/room/capture-calibration-manifest.json)
record the comparison.

The main sound-refinement report records the final integrated build, broader
tests and A/B artifacts.
