# Picking and body-shape character follow-up

The listener requested more obvious differences between the existing picking
styles and body shapes. This change keeps the same controls and parameter IDs.
It strengthens the authored picking profiles and fixes a missing connection:
Shape previously changed microphone radiation but left the mechanical bridge,
and therefore the Piezo capture, identical.

## Picking

Finger retains its calibrated contact. Pick's bridge-distance ratio changes
from 0.55 to 0.40 and its contact-width ratio from 0.50 to 0.35. Thumb's distance
ratio changes from 1.60 to 1.95. Its Gaussian contact width becomes
`sqrt((2*a)^2 + 2.5^2)`, where `a` is the existing velocity-dependent aperture
in reference samples. Adding Gaussian variances retains a soft-pad contribution
at high velocity without clamping away Touch response. All styles still use
the existing sample-rate and register conversion, displacement law and noise.
These values are authored character choices, not measured contact dimensions.

The picking-only comparison preserves all 144 Finger renders byte for byte.
At MIDI velocity 127, the existing normalized attack-band comparison gives:

| Pair | Steel before → after | Nylon before → after |
| --- | ---: | ---: |
| Finger / Pick | 3.612 → 4.370 dB | 3.658 → 5.347 dB |
| Finger / Thumb | 3.411 → 4.031 dB | 3.252 → 4.150 dB |
| Pick / Thumb | 5.349 → 7.075 dB | 6.303 → 9.012 dB |

These are median spectral differences over 18 pitches, not volume changes or
perceptual thresholds. The independent isolated-note comparison weights bands
by signal power and uses stereo power rather than a potentially cancelling
mono sum. Its median attack contrasts at velocity 127 are:

| Pair | Steel before → after | Nylon before → after |
| --- | ---: | ---: |
| Finger / Pick | 2.001 → 2.489 dB | 1.227 → 1.985 dB |
| Finger / Thumb | 1.274 → 1.676 dB | 1.413 → 2.184 dB |

Pick/Finger attack-centroid ratios rise from 1.431 to 1.641 on steel and
1.294 to 1.539 on nylon. Thumb/Finger ratios fall from 0.829 to 0.796 and
0.896 to 0.883 respectively. This supports the intended sharper Pick and
rounder Thumb direction under the normal panel controls.

The existing recorded pick/finger contrast comparison is mixed: at velocity
127 its error improves from 8.158 to 8.003 dB on steel and worsens from
4.147 to 4.427 dB on nylon. The archive does not document velocity or contact
position and merges finger/thumb labels. Stronger separation is the demonstrated
result; a universal realism improvement is not. The earlier
[recording protocol and source limits](picking-styles-2026-09-08.md) still apply.

Those tables isolate the picking edit. In the final combined Auditorium build,
the new body load also changes spectral weighting: steel Finger/Pick weighted
attack contrast is 2.001 → 1.956 dB, while its centroid ratio rises from 1.431
to 1.562. Nylon Finger/Pick contrast is 1.227 → 2.747 dB. Thus even stronger
tonal direction does not imply every difference metric increases. The companion
JSON retains both the picking-only and combined results.

Explicit MPE position continues to override the style's hand-position ratio.
At fixed MPE position and maximum steel velocity, Finger and Pick remain very
similar: their contact apertures are both narrow. The weighted comparison finds
only 0.015 dB median attack contrast there. Position clamps at extreme settings
also limit separation. No artificial click or gain change is added to mask this.

## Body shape

Shape now changes the mechanical bridge's modal frequencies and damping, using
the existing shape ratios. The same transformed modes feed bridge processing
and string phase compensation. Each positive-semidefinite heave/rock residue
matrix is retained, so a fixed shape preserves the passive modal construction.
The existing high-frequency conductance contribution remains unchanged.

Original's bridge reference remains Dreadnought, preserving the calibrated
default. Its radiation reference remains Auditorium: this legacy offset is
explicit, not a claim that the two responses describe newly measured geometry.
Each named guitar retains its measured response at its native shape. No shape
table expansion, wood retuning or additional gain fit is introduced.

Changing Shape reconfigures the body load and updates active and idle string
delay targets. It preserves note ownership and retained repluck tails, and uses
the existing bridge transition handling. Wood continues to affect radiation.
The variations are authored constructions around existing measurements, not
recordings of four rebuilt versions of each guitar.

Previously, all 48 Piezo shape comparisons (four notes, six pairs, two materials)
were byte-identical. None are identical in the final build. Median weighted
sustain contrast for adjacent shapes rises from zero to 1.103–1.854 dB on
steel Piezo and 0.648–0.884 dB on nylon Piezo. Adjacent microphone sustain
contrasts also increase, although the extreme Parlor/Jumbo sustain comparison
does not improve in every metric. The separate
[body qualification](body-shape-coupling-2026-09-09.md) records the phase,
passivity, tail ownership and switching checks.

The fixed training-only recording comparison at Auditorium is mixed. The same
nine pitches, two recordings per pitch, velocity 91, Finger and existing scorer
give Yamaha nylon 7.932492 → 7.806703 (1.59% lower error), and Walden steel
9.149220 → 9.616855 (5.11% higher error). No parameters were fitted, and no
development or protected recordings were used to select these changes. The
stronger construction response should be judged as a control-character change;
it is not a demonstrated across-the-board recording-match improvement.

## Edge checks

An independent initialized-state probe compares 25,920 notes per picking build:
MIDI 40–84, eight velocities including 1 and 127, 44.1/48/96 kHz, and panel or
explicit MPE position endpoints. All states are finite and nonzero; all 8,640
Finger comparisons and all explicit MPE positions are unchanged. The largest
first-harmonic reduction is 0.362 dB. These are initialized fretted-note checks,
not microphone-output or upper-natural-harmonic guarantees. A broader Thumb
contact suppresses high natural harmonics more strongly, as expected from its
Gaussian modal envelope.

The steeper bridgeward steel Pick also affects the existing slope-dependent
initial pitch transient: the largest extra rise in that probe is 5.126 cents,
within the unchanged 20-cent cap. Nylon's pitch law is unchanged. No new pitch
effect or independent technique gain is introduced.

## Final validation and audition

Engine, Release, ContactTravel, Capture, GuitarModels, BodyShape and the plugin
processor tests pass. The initial Engine run exposed eight obsolete assertions
that assumed Shape could only change radiation. Wood still has exact pickup
invariance, and radiation-fade scheduling now uses wood changes at fixed shape;
the new independent BodyShape suite covers mechanical changes and retained
tails. The final Engine rerun passes with those expectations updated.

All 16 tested Original/Dreadnought/Finger outputs and all 32 named-guitar native
outputs are byte-identical to baseline. The independent renderer matches all
24 checked ordinary final-renderer cases exactly. The standalone was rebuilt
from the qualified engine and opened for audition.

Matched-volume clips are available locally under `Docs/audio/control-character/`
(not committed): `picking-steel.wav`, `picking-nylon.wav`, `shape-steel.wav`, and
`shape-nylon.wav`. Picking order is Finger, Pick, Thumb; shape order is Parlor,
Auditorium, Dreadnought, Jumbo. Each option plays MIDI 40, 55, 64 and 74 with the
same velocity and per-note stereo RMS. The [audit tools](../Tools/ControlCharacterAudit/README.md)
reproduce the isolated comparisons.
