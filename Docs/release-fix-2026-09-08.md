# High-velocity note-release correction

Ordinary MIDI note-off now damps the existing fretted/open note at every
release velocity. It does not inject fretting energy or retune a fretted note
to the open string. Active finger lifts and pull-offs remain available when
CC68 legato is explicitly enabled.

The previous wrapper mapped MIDI note-off velocities above 64 to `fingerLift`.
`beginRelease` then unconditionally interpreted a positive lift as releasing
the fret's stored displacement into the open string. This is a new physical
excitation, so the reported second-stroke sound was expected from that mapping.
The correction gates that articulation at note-off and again at pedal-up.
Leaving CC68 mode clears any stored lift, including when the switch is enabled
again before the pedal is released.

## Measured reproduction

Baseline: worktree snapshot at the beginning of the 2026-09-08 task, based on
commit `45ca107bc3a0e3bfc951e4f8db2fa95b76325d47`.
Protocol: default Dreadnought/Spruce/Stereo Mic/Finger, separate steel and nylon
engines, 48 kHz, 127-frame blocks, G2/MIDI 43 at 0.2 s with note-on velocity
127, note-off at 1.0 s, render through 1.8 s. Compare release velocity 127
against the plain/default damped release. No output normalization or clipping
is added to the measurement.

| Metric: fast release / ordinary release | Before: steel | Before: nylon | After: both |
| --- | ---: | ---: | ---: |
| Stereo-channel peak in first 50 ms after note-off | 2.41539 | 23.3852 | 1.0 |
| Stereo energy 300–800 ms after note-off | 4.88067 | 77.6882 | 1.0 |
| Complete float stereo waves identical | No | No | Yes |

The ordinary-release and intentional CC68-pull-off PCM16 comparison WAVs
are byte-identical before and after for both materials. Only the ordinary
fast-release WAV changes. These measurements demonstrate removal of the
unrequested excitation, not a perceptual realism score or a measurement of
real-finger damping time.

## Reproduction and regression coverage

Build `AcustraReleaseTests`, then run `ctest --test-dir build-dsp -R
'^Acustra.Release$' --output-on-failure`. It compares complete stereo waves
for 144 combinations: steel/nylon, four shapes, 44.1/48/96 kHz, note-on
velocities 0.95/1.0, and fretted notes 43/60/76. It also verifies sustain/CC68
transitions, note ownership after damping, and the first 5 ms against an
otherwise identical held-note reference.

`build-dsp/AcustraReleaseTests --render DIRECTORY` exports ordinary release,
fast release and explicit CC68 pull-off comparisons for both materials and
prints the ratios above. Output is stereo PCM16 at 48 kHz with the engine's
native relative level. The engine suite retains the active lift geometry,
energy, pitch and hammer-on/pull-off tests with CC68 explicitly enabled.
The plugin wrapper test exercises plain note-off, MIDI note-off velocities
64 and 127, and velocity-zero note-on, plus explicit CC68 with velocity 127.

The articulation demonstration keeps CC68 enabled for its active lifts.
The README documents the revised default and explicit-articulation behavior.

Final verification: `Acustra.Engine` passed in 192.35 s and `Acustra.Release`
passed in 67.10 s on the shared development host. The worst maximum-velocity
release/held 5-ms stereo peak ratio was 0.91999 (gate 1.05). The integrated
plugin wrapper suite also passed with the revised CC68 opt-in test.
