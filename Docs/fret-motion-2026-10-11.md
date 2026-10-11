# Subtle sliding-finger roughness

The user requested a little fret noise during hand movement, then asked not to
overdo it. This candidate adds only a faint sliding-contact source to the four
wound strings. It uses the existing Release Noise amount; parameter IDs,
ranges, defaults, presets, layout and latency are unchanged. The previous
material/hand candidate is baseline `88b8a0f`.

A played, held string must have made its initial contact and its physical
stopping point must move. Conventional and MPE-manager wheels already represent
slides in Acustra; member wheels bend tension at a fixed fret. Plain strings,
stationary fingers, natural harmonics and idle movement produce no new source.
Hammer/pull and key-up already have contact sounds and are not duplicated.
New notes, tuning changes and explicit connected refrets prime the position
without simulating a slide from the old fret. The existing front-end master
Tune shares the conventional pitch-bend path: a live Tune change is therefore
indistinguishable from that virtual slide, a retained API limitation.

## Physical model and authored limits

Physical speaking length follows the string's existing six-millisecond retune
time scale. Sample-by-sample longitudinal speed sets the level and broad
roughness band continuously. The existing 18 mm finite-contact transition
withdraws the source smoothly at the nut. There are no velocity layers,
random noise triggers on ordinary notes, or artificial fret-number thresholds.

[Pakarinen, Penttinen and Bank (2007)](https://aaltodoc.aalto.fi/server/api/core/bitstreams/475276ab-f408-4ef4-a46f-5528bb8ca63d/content)
measured speed-dependent winding handling noise. Their measurements support a
speed-dependent texture and approximately speed-dependent RMS, but do not
supply our string set's wrap pitch or absolute fingertip force. This source
uses a broad correlated roughness band, not their periodic ridge-crossing
harmonic comb. Its representative spectral centres reuse the release-brush
progression at an authored 0.5 m/s reference. It is not a measured EJ16 model.

The nominal force RMS scale is `0.00004 * peakLevel * (amount/0.5)`, multiplied
by contact weight and `2*abs(speed)/(abs(speed)+0.25)`. That continuous speed
factor is one at 0.25 m/s and bounded below two. The level is deliberately
conservative: 8% of the existing nominal release-brush force at that reference
speed, and 8 dB below the first development render. These are authored mix
choices, not measured acoustic constants. The last stroke supplies a bounded
scale; sliding can still make a faint sound after that stroke has decayed.

Two passive RC lowpasses make the roughness band. Its exact discrete impulse
energy normalizes white-noise RMS across sample rates. A separate deterministic
random stream leaves existing pick/release variation untouched. The force uses
the existing `1/(2Z)` split and reference-rate time scaling, with a leaky
integrator. Opposite-polarity paths travel through convex fractional arrivals
into the string, bridge, body and capture. A stored source drains for at most
120 ms after motion ends; key-up stops new draws immediately and freezes its
last contact position. Already emitted packets retain their flight times
across re-plucks and refrets. Zero amount stops new source energy immediately.

Real soft-fingertip handling also excites longitudinal modes. This small
transverse approximation does not add a longitudinal mode bank or claim to
reproduce a full squeak. The intended result is a restrained brush.

## Listening and verification

The [local before/after review](http://127.0.0.1:8772/) uses identical eight-second
scores and native gain at 48 kHz, 64-frame blocks, with Release Noise at its
existing 70% plug-in/Rack default. The builder freezes both DSP trees and the
same renderer, compiler flags and hashes. There is no independent normalization,
EQ, compression, timing alignment or amplified difference track.

The slow wound-A and brief wound-G examples have before/after difference RMS
35.65 and 40.10 dB below their baseline whole-clip RMS. Overall RMS changes are
0.0013 and 0.0005 dB; peaks remain identical. These are descriptor measurements,
not a claim that the difference is inaudible or a listening preference. Four
controls match baseline raw float samples exactly: ordinary notes, plain-string
slides, zero Release Noise, and MPE member tension bends.

The dedicated test covers the independent physical-time trajectory, source
eligibility, finite-contact continuity, fractional packet geometry and polarity,
rate-normalized force, amount continuity, priming, stored-packet retention,
reset/scoped panic, Gather, duplicate controllers and host block partitioning.
At constant 0.1 m/s its force RMS is 1.248/1.235/1.226e-5 at 44.1/48/96 kHz,
against the authored expected 1.28e-5. The strict C++17 build passes without
warnings. The [evidence directory](validation-fret-motion-20261011/) retains
source hashes, render measurements and validation logs.

The complete Release build, C++17 DSP target and **81/81 CTest suites** pass
(523.25 seconds serially, including all required Python checks). SDK5 analysis,
Rack wrapper/output contracts, 15 rate/quality combinations and eight factory
patches at five rates pass against the same frozen DSP. The
[receipt](validation-fret-motion-20261011/receipt.json) records the final build
and test evidence; the [Rack report](../../Docs/FRET_MOTION_2026-10-11.md)
records target-module provenance and the paired default-quality CPU results.
These are local development checks; no new host install or listening approval
is claimed.
