# Held fretting-hand mute — 2026-10-16

The existing gapped-strum model already strikes crossed unused strings under a
light touch, but automatically removes the touch after its T60 and 80 ms even
when the chord remains held. This limitation is explicitly recorded in
[the original playing-physics analysis](../playing-physics-2026-10-13.md#2-a-strum-mutes-the-strings-it-crosses).
The reciprocal bridge can then excite the unused open strings again. The new
`heldShapeMute` comparison flag extends that existing touch's lifetime; it is
enabled by default independently of the seven earlier candidates.

## Evidence and limits

Bhanuprakash, van Walstijn and Stapleton's articulated-string model represents
finger damping as a distributed velocity loss, with its coefficient proportional
to the applied finger force: equations 9–11 give
`F_phi = psi_phi * F_phi(t) - r_phi * du/dt`,
`r_phi(x,t) = alpha_f * abs(F_phi(t)) * psi_phi(x,t)`.
Thus contact loss follows the continued hand contact, rather than the elapsed
time since a pick strike. This is a phenomenological slide-string model, not a
measurement of skipped-string chord damping.
[Primary paper, DAFx 2020](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_68.pdf).

The damping magnitude comes entirely from the existing GuitarSet analysis:
683 crossed unused strings, median strike 4.6 dB below the string's played
attacks, and approximately 14 open-string periods for 20 dB decay. The existing
43-period T60, 0.589 dead-stroke force multiplier, delay, polarization and
contact position assumptions are unchanged. See the local
[measurement report](../validation-playing-physics-20261013/muted-strokes-report.json)
and [measurement code](../../Tools/MeasureMutedStrokes.py).

Inferring continued contact from held MIDI keys is an explicit performance
prior, not measured hand tracking. We retain the touch while **any original
physical-key lifetime** of that complete shape survives, replace it on the next
complete shape, and never infer continued contact from sustain pedal alone.
The model does not claim a finger location, local collision, fret buzz or a new
force/EQ component. It also preserves the original minimum dead-stroke decay:
when keys lift early, the original T60-plus-80-ms timer still finishes. This is
a bounded correction for long-held shapes, not a full contact-detachment model.

## Ownership and audio behavior

`Performer::flushNoteGroup` attaches a six-bit cohort only to strings already
eligible for `muteStroke`: crossed gaps of a complete ordinary swept plan.
`muteStroke` validates that each bit identifies an actual held, same-channel
voice with a positive MIDI owner count. Standalone calls without an explicit
cohort retain their original transient behavior. Finger pinches, incomplete
plans, MPE member groups and controller-owned layouts acquire no inferred mute.

Each bit denotes a physical string's current key lifetime. Last-owner key-up,
string reassignment and connected refret remove that bit permanently; a later
matching note cannot rejoin it. Duplicate MIDI owners and same-note re-plucks
while still held preserve ownership. A successful new complete plan replaces
the old channel's cohorts. A repeat before a queued dead stroke simply rebinds
that contact's cohort; it does not add, restart or retime its pick.

The old timer decrements to one sample, then waits while the cohort is nonempty.
Once the final original owner leaves, the existing hand-back and release-gain
following remove the loss while preserving the remaining wave. Playing a muted
string lifts its contact. Scoped All Notes Off and All Sound Off respect channel
ownership; reset, affected MPE-layout changes and string-per-channel changes
clear applicable cohorts. Disabling the comparison flag resets the engine and
creates no cohorts thereafter.

Storage is one unsigned mask per string. Ownership changes perform bounded
six-voice scans at event time. Audio processing adds one timer predicate, with
no per-sample owner search, allocation, new random draw, filter or source.

## Validation and public witness

`Tests/HeldShapeMuteTests.cpp` covers 8, 44.1, 48, 96, 192 and 384 kHz; the
unchanged measured loss; partial/final and duplicate ownership; same-note
re-plucks; reassignment and connected refret; playing a muted target; replacement
by a second public gapped chord; pending-repeat preservation; key-up before
queued contact; pedal-only state; scoped panic, reset and layout changes;
nonexistent-owner rejection; and MPE/controller exclusions. The public event and
render loop asserts zero allocations. Panic output is exactly zero, renders
are finite, and block sizes 64 and 127 give identical public-witness samples.

The attack remains bit-identical before any old hand-back deadline. Unrelated
single notes, contiguous chords, independent Finger pinches and `mutedStrings`
disabled scores are bit-identical with this flag on/off. The original
`MutedStringsTests` fixed-retirement audit explicitly disables only this flag,
preserving the earlier T60-plus-80-ms contract as an ablation test.

Public witness: at 48 kHz, Pick, Output 1, Piezo 0.2, Room 0.1 and Release Noise
0.1, play MIDI `{40,57,64}` at 0.05 s and hold the outer keys. Rearticulate A3 at
0.85, 1.35 and 1.85 s after 50-ms key-up gaps. The allocator puts A3 on the G
string and leaves the open A string under the held shape. Over 0.7–3 s, the
common-gain candidate-minus-ablation difference is **−10.4764 dB relative to
the ablation, −40.6023 dBFS**. No loudness normalization or added source is used.
The frozen listening pack is the perceptual/preference witness; this numerical
difference alone is not a listening verdict. The preliminary timer-only probe
also showed the unused A's bridge-force envelope at 2 s falling from
`2.645e-4` to `3.780e-5` (about 16.9 dB); that is a force-envelope diagnostic,
not microphone SPL and not the final lifecycle implementation.

Standalone validation uses C++17 `-O2 -Wall -Wextra -pedantic`; normal integrated
and ASAN/UBSAN suites are recorded by the parent task after source freeze.
