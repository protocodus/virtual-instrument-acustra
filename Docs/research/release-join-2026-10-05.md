# Global 1/32-note release window — 2026-10-05

The user requested a musical gap in which neighboring tremolo strokes join,
then explicitly chose 1/32 everywhere. This rule applies to every ordinary
Note-off rather than requiring a detected tremolo run.

## MIDI and physical release

Key ownership and key-down state end at Note-off. The string retains its held
physics for another 1/32 note, including at the deadline. An elapsed
same-pitch/channel stroke assigned to that same physical string cancels its
pending hand damping and release touch, then uses the existing continuing
waveguide contact. Different pitches retain their refret/tail behavior.
A stroke after the deadline can still merge with whatever vibration remains,
but does not erase damping that already happened.

If no new stroke arrives, the damping hand starts after the window. Its
existing velocity-dependent loss and release-noise law then apply. Every
unrepeated note consequently rings longer too. This is a performance rule,
not a measurement of a guitarist's finger-contact time. No attack is delayed
to discover the next note.

The duration is one eighth of a quarter-note beat: `7.5 / BPM` seconds.
At 120 BPM it is 62.5 ms; at Recuerdos's 74 BPM it is about 101.35 ms.
The engine starts at 120 BPM and falls back there for unavailable, nonfinite
or nonpositive tempo. Every finite positive tempo is accepted; very long
windows saturate safely. The deadline rounds up to the next sample, keeping
an authored endpoint eligible with less than one sample of quantization.
Tiny floating-point residues near an integral sample are removed before that
rounding. Live tempo changes consume elapsed beats at the preceding tempo
and recalculate the remaining duration at the new tempo.

Notes released under CC64 retain existing sustain behavior. Pedal-up starts
their damping directly; a pedal pressed after key-up does not catch a pending
ordinary release. Explicit All Notes Off uses existing sustain-aware release directly;
All Sound Off and panic clear sound immediately. A strummed note whose key
is released before the pick arrives loses ownership immediately, still gets
its scheduled pluck, and begins its ordinary grace at that pluck. Explicit
pedal-up before that pluck instead requests direct damping there.

## Tempo sources

The native plug-in reads BPM once per process callback from the host
playhead. A valid BPM applies even when transport is stopped. Missing or
invalid BPM falls back to 120. Preparation does not query the playhead.

The performer accepts tempo events at their actual sample offset, including
with Gather enabled. Existing delayed MIDI stays in its queue; tempo is not
delayed by Gather. Splitting a block renders only its prefix and advances the
clock by that prefix, preserving scheduled events in the suffix.

Demo score rendering supplies the piece's BPM. Offline repertoire files can
include global `tempo <seconds> <BPM>` events; the generator emits the actual
tempo through phrase breaths and final holds. Tempo is applied to every part
before same-sample key events. Older files remain valid at 120 BPM, and tempo
metadata changes neither their authored notes nor their render duration.

The Reason wrapper subscribes to `/transport/tempo`, reconstructs the start
of each host batch from property diffs, and replays changes at their sample
offsets before MIDI/CV at the same sample. Its six canonical engine source
files are synchronized with this implementation. Wrapper SDK build coverage
and the existing dirty-submodule release guard are reported separately.

## Verification and listening

The focused release suite checks held-audio identity through the grace,
inside/endpoint/outside joins, musical retiming, fallback and extreme BPM,
MIDI ownership, different strings/channels, sustain, explicit controllers,
queued strums and block splits with Gather. Existing release-realism tests
retain their signal thresholds and now measure from actual damping start.
Native mock-playhead tests cover host BPM and fallback, stopped transport,
live retiming, exact note timing and unchanged reported MIDI latency.

Fresh listening comparisons use identical MIDI and a common gain within
each before/after pair, including positive note gaps and Recuerdos. The ten
standard demos retain their established PCM16 stereo 44.1 kHz format and
whole-file peak normalization. These checks establish behavior and audio
provenance; they do not establish a user listening preference.

Sixteen targeted DSP, demo and repertoire suites passed, along with the
native host-tempo release suite. The final queued-pedal matrix covers 48
velocity cases and compares exact state and stereo audio with nominal pedal
release; the performer also checks a complete six-string delayed stroke.
VST3 and standalone builds passed, and the standalone opened its rendered
1128 × 1014 interface under Xvfb. The broader native processor suite was not
rerun.

At the release-window validation checkpoint, the Reason mirror passed the same focused suite, C++17 syntax checks,
and six-file SHA256 parity. Extension validator checks 1–10 passed; its
existing release guard rejects the uncommitted Instrument submodule, whose
HEAD and parent gitlink remain unchanged. Genuine `Jukebox.h` is unavailable,
so SDK-dependent wrapper tests remain uncompiled and unrun.

The refreshed ten-demo archive is `acustra-demos-release-join.zip`, with its
external manifest and source/library/renderer hashes. It preserves all ten
established frame counts: 5,721,600 frames total, or about 129.74 seconds.
Every file is PCM16 stereo at 44.1 kHz with a whole-file peak of −3 dBFS.
Earlier demo archives and comparison packs are preserved.

`release-join-listening-pack.zip` contains 56 verified MP3s and an audition
index: Finger, Pick and Thumb at 120/74 BPM with a positive 25 ms gap, the
inclusive boundary, its next sample, and an audible outside-window control;
Recuerdos uses its unchanged sustained score in Dry and Room 50%, full-length
and first-eight-second clips. Every track uses gain 2.0, with no independent
normalization. All pairs preserve exact authored events and pre-release raw
attack samples; normalized candidate Recuerdos matches production demo 09
byte-for-byte. Its full-pass RMS changes are +0.059 dB Dry and +0.066 dB Room,
which describe level rather than a naturalness judgment. Raw float audio,
harness, hashes and analysis receipts are retained alongside the pack.
