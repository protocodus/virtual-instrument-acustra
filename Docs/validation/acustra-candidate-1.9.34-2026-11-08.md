# Acustra 1.9.34 candidate validation

The 2026-11-08 review exercised Acustra Rack Extension 1.9.34d3 with the
instrument source at `0aa63301dfeb3e7c021988261cef5b49cf1fb293`. The subsequent
instrument commit sets the shared project version to 1.9.34 and records this
validation. The tested DSP and performer files are byte-identical.

## Function and sound

The native ARM64 Jukebox SDK 5.0 build and static analyzer passed. The native
engine contracts, Rack wrapper and output-rate contracts, and the six
synthesized guitar gestures passed. Tests covered velocity response, repeated
and CV triggers, event timing and Reset Audio. All eight Rack factory patches
rendered finite and returned to silence at host rates from 44.1 to 192 kHz.
The Rack checks used the wrapper and its host test harnesses. Live Reason
behavior and timing still need verification.

At a 96 kHz engine rate, the high B and E sweep across frets 9, 13, 16 and 20
reduced the median error above the 12th partial from 8.83 to 0.26 cents through
17 kHz. At 17–20 kHz the median fell from 48.32 to 3.68 cents. The largest
remaining error at that rate was about 50 cents near 19.9 kHz. The target
uses the same physical string stiffness. The phase fit also redistributes
delay inside the waveguide, which changes some attack harmonics. The
measurements establish how closely the partials follow the model. Actual
pluck realism and listener preference still need listening evidence.
[Model limits](../../README.md#known-gaps)
· [Guitar gestures](../guide/user-guide.md)
· [Verification scope](verification.md)

## Callback timing

At 48 kHz with 64-frame callbacks and default 2× quality, compared with
1.9.34d2, mean calling-thread CPU increased by about 1.1% on the warm
chord, strum and pick traces and by 5.2% on the cold high-pick and bend
traces. Each cold run had one calling-thread deadline miss on its first
note. The worst first callback increased from 2.03 to 2.61 ms against a
1.333 ms deadline.

In the warm traces both builds stayed within the calling-thread CPU budget.
The d3 trace had 19 wall-clock misses versus one for d2 across 13,500 callbacks
per build. Another instrument benchmark was active. Scheduling or blocking
could explain the gap between wall time and thread CPU, and these traces
cannot establish the cause. Timing under quiet conditions and in live Reason
still needs verification.
