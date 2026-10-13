# Audible engine opportunities — 2026-10-13

Discovery against the clean canonical `d01b477` engine used by Acustra Rack
1.3.1f1 found two reproducible defects and one inexpensive sound option with
tradeoffs. All experiments are isolated. This report does not promote a new
engine revision or claim a listener preference.

| Priority | Opportunity | Audible consequence | Scope and confidence |
| --- | --- | --- | --- |
| 1 | Respect a new chord's planned fingering inside the release-join window | Restore a new stroke that can otherwise be lost during ordinary chord changes | Small conditional change; public Performer reproduction and negative controls pass |
| 2 | Give each new release contact its own travel geometry | Correct the colour/timing of fast open-to-fretted note endings | Confirmed dependency defect; subtler at 10% Release Noise; old arrivals need careful preservation |
| 3 | Optional polarity for the piezo blended into microphones | Recover notes made thin by sensor cancellation | Cheap signal operation, but other notes can get quieter; an audition option, not a universal default fix |

## 1. A common chord tone loses its new stroke

An ordinary B minor → E change with a 20 ms key-up gap at 120 BPM is
inside the existing 62.5 ms release-join window. The new E chord's planner
puts B3 (MIDI 59) on the open B string. However,
`AcustraEngine::noteOn` at the join early return (around line 9122) takes
the earlier B3 on the G string before consuming that plan. The subsequent
G♯3, correctly planned on the G string, steals it. B3's new key is still
held, but `heldString(59)` returns −1 and its new stroke is lost. The old B3
tail and other notes' partials can still sound; the entire B3 frequency does
not disappear.

The prototype checks the same-sample planned string before taking the join
shortcut. A repeat without a plan, or with the same planned string, retains
the existing physical join. A different valid planned string goes through
the normal planned-note path. This restores all six E notes at both 48 and
96 kHz and keeps the ordinary playable open-E grip. The previous G-string
normal-plane delay buffer is copied exactly into the retained tail.

All 14 × 14 familiar chord transitions have their new pitches in the
candidate. The baseline loses the new B3 in four B minor transitions: to E,
G, B7 and E minor. A separate deterministic 2,000-transition allocation
probe finds no missing new held pitch with the candidate. Whole four-second
stereo renders remain bit-identical for three negative controls: a single
E3 repeat without a chord plan, E → A minor with a compatible plan, and
B minor → E with an 80 ms gap outside the window.

The changed four-second B minor → E render has difference RMS 0.002753 FS
and maximum difference 0.04071 FS at 96 kHz. This is a missing-stroke repair,
not a new EQ or randomization law. The added work is a bounded lookup at
note onset, not an audio-rate operation; CPU was not timed.

Evidence and the small proposed patch:

- [Before / prototype, sequential audio](../build-audible-discovery-20261013/auditions/01-chord-join-AthenB.wav)
- [Candidate patch](../build-audible-discovery-20261013/performance/candidate.patch)
- [Candidate provenance and allocation results](../build-audible-discovery-20261013/performance/candidate-receipt.json)
- [Public Performer render receipt](../build-audible-discovery-20261013/performance/performer-render-receipt.json)

## 2. Fresh release noise can retain an old contact position

`startReleaseNoise` only initializes `releaseNoiseTravel` when that transport
is inactive (around line 10509). A re-pluck stops the preceding noise drive
while its filters and emitted arrivals run out. If the next key-up happens
before they become exactly inactive, the new contact receives fresh level,
colour and duration but keeps the old contact's travel delays.

The public Note On/Off witness plays open low E, then its twelfth fret on
the same string. Both ordinary release-join delays run normally. At the
second physical release, approximately 0.39251 seconds, Pick's old direct
and reflected integer taps are 19 / 1143 samples; the new fretted contact
needs approximately 273 / 307 at 96 kHz. A residual around 1e−39 keeps the
old transport active. Its tiny amplitude is not the problem: it locks the
subsequent fresh noise to the wrong contact geometry.

The diagnostic reference copies and drains the old transport with zero
new input, preserving its pending arrivals in the existing arrival queue,
while the normal public release initializes fresh geometry. At Release
Noise 10%, its first 70 ms Main difference is 33–38 dB below Main RMS across
Finger, Pick and Thumb; maximum differences are 0.00123–0.00144 FS. At 50%,
the difference is 19–24 dB below Main. This supports a contact-colour
comparison; general audibility under a full mix and listener preference
are not established.

This is the next bounded correction after the chord fix, but it is not a
safe one-line reset. Resetting the active transport would discard earlier
wave packets. A production change must freeze each new contact's delays
while preserving earlier arrivals, with tests for overlapping contacts,
reset/copy, block partitioning and different rates. No production patch or
CPU result is claimed for this diagnostic reference.

- [10% Release Noise: current / fresh geometry](../build-audible-discovery-20261013/auditions/02-release-default-AthenB.wav)
- [50% diagnostic: current / fresh geometry](../build-audible-discovery-20261013/auditions/05-release-diagnostic-AthenB.wav)
- [Full release audit and implementation tradeoffs](../build-audible-discovery-20261013/engine/findings.md)
- [Public-event score, controls and source/audio hashes](../build-audible-discovery-20261013/engine/public-manifest.json)

## 3. A selectable piezo polarity can fill some thin notes

The current 20% piezo blend can oppose the microphone response. This was
already an open question in the October 12 decision log; it was remeasured
on `d01b477` rather than assumed unchanged after the tuning repair.

At 96 kHz engine rate, with Room 10%, the following mid-signal note levels
are relative to their measured neighbouring-note reference:

| Case | Microphones alone | Current +20% piezo | Reversed 20% piezo |
| --- | ---: | ---: | ---: |
| Parlor F♯5 | −7.09 dB | −13.38 dB | −1.93 dB |
| Jumbo C5 | −1.69 dB | −7.02 dB | +2.49 dB |

Reversal raises the actual rendered note levels by 9.30 and 6.35 dB
respectively; changes in the neighbouring reference explain why the
relative deviations move farther. Stereo-energy measurements also show the
holes filling. Direct runtime witnesses validate the microphone-plus-piezo
reconstruction within 1.49e−8 FS, with an identical separate Piezo signal
and no safety-limiter activity.

There is a real cost: Parlor E5 moves from +0.83 to −5.09 dB relative to its
neighbours, and Jumbo D♯5 from +0.19 to −3.74 dB. Therefore an automatic
shape-dependent sign flip is not recommended. A selectable polarity for
the blended piezo is the small useful option to audition; preserve the
existing default. Its gain should transition smoothly, and the separate
Piezo output and Piezo-only Capture should retain their contracts. UI and
saved-state integration still need design and validation if implemented.

- [Parlor current / reversed blend, including the tradeoff notes](../build-audible-discovery-20261013/auditions/03-piezo-parlor-AthenB.wav)
- [Jumbo current / reversed blend, including the tradeoff notes](../build-audible-discovery-20261013/auditions/04-piezo-jumbo-AthenB.wav)
- [Detailed capture findings](../build-audible-discovery-20261013/capture/FINDINGS.md)
- [Measured diagnostic](../build-audible-discovery-20261013/capture/report.json)
- [Audition provenance](../build-audible-discovery-20261013/capture/audition/manifest.json)

## Scope of the evidence

The [listening page](../build-audible-discovery-20261013/auditions/index.html)
and [audio manifest](../build-audible-discovery-20261013/auditions/manifest.json)
contain five paired comparisons. The playback copies use one shared
+6.0206 dB gain for every case and variant, PCM24 stereo at 96 kHz, with no
independent normalization, EQ, resampling or limiting. Sequential files
play A/current first, 0.5 seconds of silence, then B/prototype or option.
Original floats and unity-gain files are preserved beside the probes.

The chord and release cases use native engine Output 0.42; capture cases
use Rack's 0 dB Output (1.0). They share Room 0.1, Piezo Mix 0.2, Release
Noise 0.1 except the explicitly marked 50% diagnostic, and Detune off.
Compare A with B within each case. A 96 kHz engine render corresponds to
the model's rate at 48 kHz/2×, but does not include the Rack rate adapter
or establish a host playback result.

The isolated code compiled as C++17. No new full suite, SDK package,
installation, listening verdict or CPU benchmark is claimed. The native
listening page received static file/audio validation only: the in-app
browser disallows local-file URLs. Discovery artifacts include matching
frozen source and headers, renderers, event scores, patches and hashes.
The canonical engine and installed 1.3.1f1 remain unchanged. A production
implementation should start with the chord fix, then the contact-history
correction; audition polarity as an optional choice.
