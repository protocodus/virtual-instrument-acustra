# Responsive dynamics, 22 October 2026

Version 1.6.3 ports the dynamics change the owner requested on 13 October
(unmerged branch `codex/acustra-rack-polish-20261012`, commits `25bdbb3` and
`ba84562`) onto the 1.6.2 engine. The owner asked for less level contrast
between velocities and for the dramatic accents on repeated strums to go;
on 22 October they asked for the branch's useful parts to be taken. This
replaces the wider keyboard response requested on 10 October
([decisions](../decisions.md), 2026-10-10).

## What changed

- **Force law.** `initialisePluck` takes playing force as `v` instead of
  `v²`. Half velocity now supplies half the full stroke's force rather than
  a quarter. Full velocity is unchanged.
- **Release.** Raising soft-note force raises its release speed. The pick
  edge and Finger's effective radius are scaled by `1/v`, so the release
  speed-to-edge ratio, and with it the soft-to-hard articulation, stays
  where it was. Finger's radius follows `v^0.233125` and its contact time
  still follows `v^-0.766875`.
- **Repeat residual.** The same-note repeat fade compares commanded forces
  on the new linear law.
- **Hammer-ons and pull-offs.** Their external-work contact force follows the
  pluck's law (`0.24 v` instead of `0.24 v²`), so they keep their balance
  against a plucked note at every velocity. The Oct 13 branch predates this
  code; this is the one addition to it.
- **Strums.** A strum member's force draw was a uniform ±7.74 dB, matching
  GuitarSet's 4.47 dB standard deviation of a repeated string's level. That
  corpus carries the players' own accents, which a fixed MIDI velocity does
  not ask for. The draw is now a ±1 dB residual, normalised to unit linear
  mean. The measured stroke-direction tilt, muted-stroke and edge-string
  terms are untouched.
- **Plug-in Room.** New plug-in instances start at 10% Room instead of 50%,
  matching the Rack device. Saved values keep their level.

All of these are authored keyboard responses, not measured laws of hand
force, contact size or strum level. No compressor, level follower or
per-note normalisation is involved.

## Measured effect

Probe: factory preset 1 (Dreadnought Steel) through `acustra::Performer`,
48 kHz, 256-frame blocks, 96 BPM, the same MIDI and seeds for both engines.
A is canonical `820e59c` (1.6.2); B is the 1.6.3 source. Levels are RMS of
both channels; the renderer was off-tree.

| D3, first 300 ms | A (dBFS) | B (dBFS) | B - A |
| --- | --- | --- | --- |
| velocity 24 | -49.8 | -35.3 | +14.5 dB |
| velocity 48 | -38.1 | -30.0 | +8.1 dB |
| velocity 72 | -31.6 | -27.1 | +4.6 dB |
| velocity 96 | -27.0 | -24.9 | +2.1 dB |
| velocity 120 | -23.2 | -23.0 | +0.1 dB |

The spread from velocity 24 to 120 falls from 26.6 to 12.3 dB.

Eight E-major eighth-note strums, first 150 ms of each stroke:

| Strums | A mean | A std | A largest jump | B mean | B std | B largest jump |
| --- | --- | --- | --- | --- | --- | --- |
| velocity 80 | -18.7 dBFS | 1.48 dB | 4.1 dB | -16.2 dBFS | 1.12 dB | 3.1 dB |
| velocity 50 | -25.7 dBFS | 1.39 dB | 3.6 dB | -19.9 dBFS | 1.08 dB | 2.6 dB |

Much of the remaining stroke-to-stroke variation is stroke direction and
strings still ringing, which stay by design. Whole-take peaks were -6.2
dBFS (A) and -4.2 dBFS (B); full-velocity peaks do not change.

The before/after WAVs are one-time review evidence, kept in the project
files (`acustra/previews/2026-10-22-dynamics/`) rather than in Git. They
are not level-matched, because level contrast is what changed. No listening
preference is claimed beyond the owner's request.

## Tests

The branch's test changes were carried over: the bounded velocity and
release tests follow the linear law, the repeated-strum test bounds a
fixed-velocity string's peak-level spread between 0.1 and 1.5 dB instead of
matching GuitarSet's 4.47 dB, and the plug-in parameter contract expects
the 10% Room default.

Three fixtures written after 13 October encoded the quadratic law and were
diagnosed against 1.6.2 before changing:

- `PerformerContact` checked that halving velocity quarters the legato
  contact's amplitude (energy 1/16). It now checks the linear law (1/4).
- `Engine` (steady pitch) plucked at velocity 0.72. Under the linear law
  that is a stronger stroke, whose tension modulation sharpened MIDI 52 by
  0.17 cents, past the 1.5-cent limit (1.6.2: 1.435). At 0.5184 = 0.72²,
  the original stroke force, every note is within 0.03 cents of 1.6.2.
- `ChorusRegression` brackets upper-partial energy at velocity 0.6 around
  references observed under the quadratic law. Every partial rose by
  1.66-1.68x, the force ratio 0.6/0.36, so the spectral shape is unchanged;
  the references now carry that ratio and the ±6 dB bracket is unchanged.
