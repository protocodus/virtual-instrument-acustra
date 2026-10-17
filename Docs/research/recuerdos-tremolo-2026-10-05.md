# Recuerdos: one continuing tremolo string — 2026-10-05

This report preserves the continuing-string implementation and its original
comparison. Ordinary Note-off's immediate damping described here was later
superseded by the [global 1/32-note release window](release-join-2026-10-05.md).
The continuing-string contact and authored Recuerdos holds remain in use.

The user reported a robotic, plucky tremolo and asked whether another hit
should merge with the already vibrating string. It should: contact changes
that string's motion before releasing it again. The preceding implementation
started a fresh string and carried the old wave on a separate damped bridge
port. Its earlier local-displacement correction improved that approximation
but did not make it one physical string.

## Continuing-string contact

An elapsed same-pitch/channel normal-string re-pluck now retains the old
waveguide's period, write position, phase and intrinsic filter memories. An
ideal stopped contact constrains its displacement and one-sample cyclic
motion at the new fractional pluck point in both polarisations. The two
constraints use the sampled cyclic slope-energy inner product; the minimum
correction preserves the mean and cannot increase that sampled norm.

The existing calibrated release is aligned to the retained period and added
to that wave. Both planes use one new-force gain. If their combined new slope
energy is `E` and old/new slope cross-product is `C`, the gain is

```
g = 1                                  if C <= 0
g = E / (C + sqrt(C*C + E*E))            otherwise
```

Thus the merged sampled norm does not exceed the conditioned old norm plus
the nominal new release's norm. Negative interference remains cancellation;
the engine does not amplify the force to recover a prescribed output level.
The same gain reaches the new release rise, burst and contact-noise source.
This bounds an authored stroke's sampled work, not a measured hand-force
trajectory or the entire coupled instrument's stored energy.

Already emitted string-contact packets retain their future arrival samples
in a fixed-capacity queue and enter the continuing string's existing bridge
port. Rapid third and later strokes retain that queue. A different-pitch
plucked refret still captures the old-pitch tail and its queued arrivals; it
does not use this same-pitch merge. Natural harmonics and duplicate note-ons
before any audio sample retain their existing paths. Fresh attacks retain
their calibrated initialization. No heap allocation occurs in this contact.

This remains an ideal instantaneous hold/release approximation. It does not
solve the moving finger, nail collision or complete static loaded string/body
state. Old airborne-click flight still follows the existing initializer.
The sampled energy guard excludes filter storage and launched packets.

## The melody must stay fretted

The generated score has 289 notes over 36 beats at 74 BPM, including 217 short
melody strokes. Its short written lengths previously sent key-ups before
71 thumb slots, 55 of them followed by the same melody pitch. Each slot was
about 101 ms. In Acustra, Note-off begins hand damping: the nominal fretted
T60 is 160 ms, whereas an open note uses 1.25 seconds. CC64 postpones this
damping until pedal-up. A notation duration therefore cannot be used as a
request to leave a fretted string alone.

`Tools/TremoloPerformance.h` derives performance holds from the generated
score, keeping each melody stroke held until the next melody attack. The
final melody releases at the original end of the piece. This changes 73
hold lengths, including tiny rounding gaps around the ornament, and leaves
every pitch, attack time, velocity and accompaniment duration untouched.
Balanced off-before-on events avoid an intervening sample of damping.
The generated source score itself is unchanged. Picking, touch, pluck point,
tempo, room setting and random variation remain the same.

## Verification and comparison

The repeated-pluck suite checks one continuing port, retained phase and
filter memories, both contact constraints, the joint two-plane work budget,
long phase-locked repeats, scheduled/immediate equivalence, block-cut
independence, and actual 12/33 Hz tremolo at 44.1–192 kHz. Contact-travel tests
check exact arrival times through repeats and genuine refrets. The Recuerdos
articulation suite checks the written score is preserved, the melody stays
held through a real thumb slot, all note owners release, and rendering is
identical across 17- and 256-sample blocks.

Ten affected DSP suites and both demo checks passed. The VST3 and standalone
builds passed, and the standalone opened its rendered interface under Xvfb.
The native processor-test executable also built; that suite was not rerun.
Legacy old-tail fixtures now use actual changed-pitch refrets, retaining
their original thresholds. The age-control fixture retains its E3 destination;
its baseline and candidate results are identical. A separate same-pitch test
guards retained wave/filter history, energy continuity and adjacent-sample
control transients.

An independent probe covered 7,680 rank-one/rank-two contact geometries at
44.1–192 kHz, with no sampled slope-energy increases. Contact displacement
and ideal cyclic-motion residual RMS were `2.27e-8` and `1.58e-7` respectively.
Thirty-two actual repeats allocated no heap memory; on this cloud machine
their note-on callbacks averaged about 60 microseconds, with a 292-microsecond
maximum. An extreme transport fixture drained 9,451 future frames without
overflow. These are bounded-fixture checks, not a host-wide real-time guarantee.
The fixed main/tail queues add about 1.5 MiB per engine; its measured size is
4,163,280 bytes.

The 128-stroke phase-locked/detuned cases settle: late/early sampled string
energy ratios span 0.936–1.074 and radiated RMS ratios span 0.978–1.033. Short
low-E 33 Hz window-energy ratios still reach 15–17 times an isolated stroke.
Those windows include accumulated vibration and do not prove radiated
passivity; the contact work claim remains restricted to the sampled string.

The listening package separates two changes in a 2-by-2 comparison:

| Take | DSP | Recuerdos holds |
| --- | --- | --- |
| A | Prior contact-only working tree | Original written lengths |
| B | Prior contact-only working tree | Continuous melody |
| C | Continuing-string merge | Original written lengths |
| D | Continuing-string merge | Continuous melody |

Each has the full passage and its first eight seconds, both dry and with
Room at 50%. All comparison files share one package-wide gain, with no
independent loudness normalization. Frozen inputs, library/source hashes,
raw renders and score-difference receipts live in
`/workspace/.cloud-setup/acustra-recuerdos/`. The standard ten demo WAVs use
their established separate whole-file peak normalization for distribution.
No listening preference or measured improvement in naturalness is inferred
from the numerical guards.

The production Recuerdos WAV is byte-identical to take D after applying the
standard renderer's peak normalization and PCM16 conversion. Take A's raw
render likewise reproduces the prior production WAV exactly, which calibrates
that conversion. The first attack is unchanged; the first merge difference
arrives seven samples after the second melody attack. The complete D passage
is about 0.85 dB quieter in dry RMS than A at the common listening gain;
that level change is not a preference score.
