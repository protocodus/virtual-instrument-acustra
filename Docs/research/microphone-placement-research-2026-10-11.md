# Microphone placement and the reported bass boom

Follow-up: the user subsequently approved the balanced combination. See the
[implemented blend and native A/B](studio-capture-implementation-2026-10-11.md). The evidence below records the earlier research stage.

Research requested before selecting a correction. The reported Reason notes
are Parlor B1 (123.47 Hz), Auditorium A1 (110 Hz), and Jumbo E1–F1
(82.41–87.31 Hz). No new microphone correction is accepted or released.

The subsequent [five-variant capture experiment](studio-capture-variants-2026-10-11.md)
includes 3,000 candidate renders, attack/held resonance checks and gain-matched
listening comparisons. None yet demonstrates a consistent improvement without
relevant tradeoffs; production capture, body physics and piezo remain unchanged.

## Professional recording practice

Placement is an early tonal decision. LEWITT recommends starting about
25–30 cm from the neck/body joint, varying position and angle, and avoiding
a microphone directly in front of the soundhole when it overwhelms the
recording with bass. Its coincident XY example uses closely adjacent
capsules at 90 degrees, roughly the same distance from the neck/body joint.
These are starting points, not a universal optimum for every guitar.
[Manufacturer application guide](https://www.lewitt-audio.com/blog/how-to-record-acoustic-guitar-at-home-0).

Mike Senior's recording investigation describes a second useful approach:
a microphone in front of the body but displaced vertically from the
soundhole. It retains body character while reducing the hole's dominance.
He recommends obtaining a useful single-microphone balance before adding
another microphone. The instrument, player and recording context determine
the position.
[Sound On Sound recording investigation](https://www.soundonsound.com/techniques/how-record-great-acoustic-guitar-sound).

Proximity effect and soundhole boom are different. DPA's measurements show
that bass balance also depends on the instrument's spatial radiation:
the guitar surface below the bridge behaves approximately as an extended
source at short distances. Pressure omnis have no capsule proximity
effect; directional pressure-gradient microphones can add distance- and
angle-dependent bass emphasis.
[DPA measured explanation](https://www.dpamicrophones.com/mic-university/background-knowledge/proximity-effect-in-microphones-explained/).

EQ is still legitimate recording practice. SCHOEPS explicitly describes
bass cuts for compensating pressure-gradient proximity, including in its
special close-pickup capsules. This does not prescribe a particular cut for
ACUSTRA's A0 modes.
[SCHOEPS technical guidance](https://schoeps.de/en/knowledge/knowledge-base/technical-basics/proximity-effect.html).

## What ACUSTRA currently represents

The source archive uses three quarter-inch pressure microphones 10 cm above
the top. Two bridge positions are 20 cm apart; a third is 20 cm toward the
neck from the treble-side microphone. The current stereo pair uses the
treble bridge and upper-bout responses; Mono uses the upper-bout response.
See [the source notice](../../THIRD_PARTY_NOTICES.md#robert-mores-guitar-measurement-archive),
the 2026-09-01 placement record in [decisions](../decisions.md), and
[the capture-observation audit](capture-observation-2026-10-10.md).

These are fixed measured force-to-pressure responses from a nylon-strung
flamenca, subsequently morphed for the steel-string constructions. They are
not a positioned cardioid or an independently measured 12th-fret studio
capture. Relative modal phases are retained. The runtime has no microphone
position, distance, axis, diaphragm-gradient or capsule-proximity model.

The authored x4 low-mode sensitivity is applied only to the left bridge
microphone. Removing it cannot correct Mono, and a previous replacement
reopened a deep Parlor cancellation notch. A scalar residue change is not
equivalent to physically moving the microphone.

## Measurement and interpretation

On the latest baseline, Spruce A0 is approximately 121.49 Hz (Parlor),
106.12 Hz (Auditorium) and 84.42 Hz (Jumbo). The reported notes are therefore
near real modeled body peaks. Matched native renders show dominant low
attack energy in both microphone routes, while those notes can decay
quickly through the coupled instrument. This does not establish excessive
physical sustain or a defect in the damping coefficients.

An experimental post-sum -6 dB/Q5 bell following each affected body's A0
reduced the reported fundamentals by about 5.4–6.2 dB. All 204 Piezo and
Dreadnought control clips remained byte-identical across the 408-case
comparison. However, the unadjusted phrase levels failed the existing
1 LU construction policy (Stereo down to -2.93 LU, Mono to -2.58 LU).
That experiment demonstrates an observation-only tonal change, not a
root-cause repair or listening acceptance. The source change and provisional
tests were archived and removed from the working engine. Measurements and
reproduction scripts remain in
[the diagnostic evidence](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/microphone-boom-2026-10-11/comparison-summary.json).

## Direction for the next correction

Investigate a balanced recording observation before changing physical
resonances. A useful capture target is a neck/body-joint microphone around
25–30 cm away, with the soundhole off its principal axis; a body-front
position displaced above the soundhole is an alternative. Compare mono
first, then validate the stereo sum and individual channels.

This is a proposed recording target, not a response recoverable uniquely
from the three existing measurement points. A credible implementation needs
additional spatial transfer measurements or a validated radiation model
that predicts complex pressure at new positions. A directional capsule also
needs pressure-gradient information. Neumann's low-frequency research models
front and internally filtered rear pressure explicitly;
[DAGA 2024 paper](https://pub.dega-akustik.de/DAGA_2024/files/upload/paper/351.pdf).

Do not rename an arbitrary shelf or modal gain as microphone distance.
First test any actual observer inconsistency that can be established from
the available data. Keep the current mechanics and piezo while evaluating
capture changes, unless independent evidence identifies a mechanical error.
Use remaining EQ as an explicit recording choice, with level-matched
auditions and appropriate construction/rate/transition checks.
