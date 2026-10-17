# Release-noise character: read-only source findings

Reviewed 2026-10-11 after the new-instance Release Noise default changed from
70% to 10%. The user reports remaining resonances and release noise that can
sound too loud and stereotypical; other aspects sound acceptable to them.

This is a source audit, **not a controlled listening result or a measured
attribution of that report**. No audio was rendered, tests run, or production
code changed for this audit. The possible audible effects below are inferences
from confirmed implementation choices. They do not establish a new DSP defect.

## Source snapshot

- Canonical checkout: `/Users/vojta/Dev/virtual-instrument-acustra-model-audit-20261011`.
- Canonical commit: `11eafbefb3a7438f3012533c1ad8e45a065d480a`.
- Rack checkout: `/Users/vojta/Dev/reason-rack-extension/acustra-selectors-20261011`.
- Rack commit: `ac22b95d592af03cb50fc7ebcd67d12f6c276e86`.
- Rack `Examples/Acustra/Instrument` commit:
  `11eafbefb3a7438f3012533c1ad8e45a065d480a`.
- The runtime files below match their commits; pending documentation work is
  outside this audit. All four Rack `Instrument/Source/DSP/AcustraEngine.*`
  and `AcustraPerformer.*` files were also checked against the canonical files
  and have identical SHA-256 hashes.
- Git tracks this directory as **`Source/DSP`**, with uppercase `DSP`.

Canonical files (SHA-256):

| Runtime source | SHA-256 |
| --- | --- |
| `Source/DSP/AcustraEngine.cpp` | `57fa5116658c3e55fcb8aa07af793dba4ff06b4addc6cc253ebde13f946d62a1` |
| `Source/DSP/AcustraEngine.h` | `5f1ccc4258254e9b00f6785ec4e2a12ea6fb32f350f23e07ab22c02da2303f6c` |
| `Source/DSP/AcustraPerformer.cpp` | `19f1d13e9f7514a0faf6edc59f5f9f599cc1392dc12eb7dfa9f350aa08025011` |
| `Source/DSP/AcustraPerformer.h` | `34f9caa77a9700e627e6c57d7ce7bfc6ea77fd58188f91017672c2458e33a21f` |
| `Source/PluginProcessor.cpp` | `0caf760c652186110f17ea9f1f23ff9c29678f901b0a6e48a5c252f406910ef8` |

Rack wrapper files (SHA-256):

| Runtime source | SHA-256 |
| --- | --- |
| `Examples/Acustra/Acustra.cpp` | `579592afdefb558fc763aee611fc9b58c84e10d3e6d1aa75b200521990a516b9` |
| `Examples/Acustra/Acustra.h` | `2ae833e8fea0e36390c3669bcb9fd739b30fc590e899b2c9043b7ad186419f92` |

## Confirmed behavior and possible audible consequences

1. **Many releases share one gesture shape.** At a fully fretted stop, all
   picking styles use the same nominal 1.4 kHz touch corner, 10 ms fall and
   full touch/brush levels. Open-string behavior does distinguish Pick,
   Finger and Thumb. Colour, decay, level and position receive modest random
   variation. The touch rise is always 1.5 ms; the nominal wound brush rises
   in 5 ms and falls in about 16 ms. Its four per-string centres are 2.0,
   2.4, 2.9 and 3.5 kHz, with Q=2.5 and modest centre variation. All release
   sources use the same normal/parallel polarization ratio. This supports a
   recognizable recurring gesture character, despite different noise samples.
   See [gesture choices](../../../../Source/DSP/AcustraEngine.cpp#L10132),
   [envelopes and filters](../../../../Source/DSP/AcustraEngine.cpp#L10165), and
   [polarization](../../../../Source/DSP/AcustraEngine.cpp#L10232).

2. **Rack cannot vary this gesture through release velocity.** The wrapper
   receives no release velocity and calls the Performer's default note-off
   velocity of 64. That maps to firmness 1.0 and nominal brush speed for every
   ordinary Rack key-up. Native MIDI can supply an explicit release velocity;
   missing or nominal velocity uses the same fallback. The mapping from
   firmness to lateral brush speed is itself an authored assumption, not an
   observed finger trajectory. See [Rack mapping](/Users/vojta/Dev/reason-rack-extension/acustra-selectors-20261011/Examples/Acustra/Acustra.cpp:247),
   [Rack note-off](/Users/vojta/Dev/reason-rack-extension/acustra-selectors-20261011/Examples/Acustra/Acustra.cpp:353),
   [Performer default](../../../../Source/DSP/AcustraPerformer.h#L162), and
   [firmness and brush speed](../../../../Source/DSP/AcustraEngine.cpp#L10094).

3. **The wound brush remembers the preceding stroke's peak.** Touch amplitude
   follows the current smoothed string reaction force; brush amplitude follows
   `peakLevel`, the maximum of that follower since the preceding pluck. A
   long-held note can therefore have a quiet remaining string but a brush
   still scaled by its earlier peak. This can plausibly make the brush stand
   out at the end of a note. It is deliberate external-hand excitation, so
   replacing the peak with current string level would not by itself establish
   greater physical accuracy: a hand can also excite a quiet string. See
   [source scaling](../../../../Source/DSP/AcustraEngine.cpp#L10147) and
   [reaction follower](../../../../Source/DSP/AcustraEngine.cpp#L10573).

4. **An actual damping release prescribes a brush without requiring observed
   lateral slip.** With Release Noise above zero, `beginRelease` requests the
   authored touch/brush source, and wound strings receive its brush component.
   This follows sustain/join handling; it does not mean every incoming MIDI
   Note Off immediately makes a noise. The passive damping contact has a
   separate authored contact model. The noise is transported through the
   physical string and body, so resonances can colour it; it is not an added
   recording at the output. See [release scheduling](../../../../Source/DSP/AcustraEngine.cpp#L7847),
   [damping contact](../../../../Source/DSP/AcustraEngine.cpp#L7876), and
   [noise transport](../../../../Source/DSP/AcustraEngine.cpp#L10406).

5. **There is no fixed audio sample replayed at each release.** Each string has
   a separate advancing release-noise random stream. `prepare` restarts the
   deterministic seeds, whereas a normal reset/panic does not. An identical
   prepared performance is reproducible, but successive key-ups within it do
   not reuse identical random waveforms. Increasing random variation alone
   would not address the common gesture assumptions above. See
   [seed lifecycle](../../../../Source/DSP/AcustraEngine.cpp#L2869) and
   [noise synthesis](../../../../Source/DSP/AcustraEngine.cpp#L10362).

6. **Sliding noise already has more motion context.** Wound-string sliding
   noise requires a held, played string and actual movement of its physical
   speaking-length stop. Its colour and bounded force follow sliding speed;
   key-up stops new roughness and leaves the short stored tail. It is a reduced
   transverse brush, with authored spectral/force scales rather than measured
   winding pitch and finger force. It is therefore a weaker explanation for
   stereotypical stationary key-ups than the separate key-up source. See
   [motion eligibility](../../../../Source/DSP/AcustraEngine.cpp#L10247) and
   [speed-dependent source](../../../../Source/DSP/AcustraEngine.cpp#L10275).

The new 10% default changes source amplitude, not this gesture character.
For otherwise identical state, the source scale at 10% is one seventh of its
70% value (about -16.9 dB). This is a source-law comparison, not a guaranteed
change in total output level. Saved explicit values remain intact, and the
bare DSP default remains zero; see the separate default-change validation.

## Bounded next investigation

First isolate touch-only and brush-only versions of the **current** model in
short- and long-held-note A/Bs, using the reported constructions and pitches.
That would distinguish a contact landing problem from a winding-brush problem
without guessing from the mixed sound. This audit did not perform that test.

The most coherent subsequent design direction is to distinguish passive
pad/palm landing from active winding slip: relate the landing to the existing
contact trajectory and local string state, and use available actual fret motion
or explicit release information to constrain brush duration and spectrum.
Rack still supplies no direct lateral-slip or hand-force measurement, so an
unobserved key-up brush would remain a conservative authored fallback, not a
fully identified physical gesture. Avoid arbitrary extra randomization and
retain the existing bounded source energy, transport and zero-control behavior.
No such design or tuning change is adopted by this report.
