# Axial loss passivity — 2026-11-08

Local 1.9.33 review candidate. Frozen shipping 25c3d64 and output candidate
365d86e both exhibit the original defect; this is independent of the new
output guard, which initially made the failure less visible at its boundary.

## Verified failure and physical boundary

At 11025 Hz, Open G, string index 3, the stored one-pole loss was
`b=0.8797449469566345`, `a=0.12429735064506531`. Its pole is stable, but its
whole-band gain is `b/(1-|a|)=1.00461606186`. The complete delayed ring has
real pole 1.001014326, doubling amplitude every approximately 0.062 seconds.
The fit's first two mode attenuations do not imply passivity elsewhere.

The exact full-control stress uses the fresh production Performer, 64-frame
blocks, Open G, Stereo Mic, Room/Piezo Mix/Width 1, Output +6 dB, Pick, Touch 1,
position 0 and 13 velocity-127 open chords from 0.02 to 1.22 s. Note-offs
remain at 1.45 s; only capture length extends from 1.6 to 10 s. The original
1.6-second raw prefix is byte-identical to the earlier +7.991 VU case.
Main's meter is the louder channel's maximum full 300 ms RMS plus 12 dB.

Original axial force grows while the mechanical bridge remains bounded and
decays. Capture and body poles and the room's feedback contraction bound
remain below one, but the increasing axial input eventually contaminates
those states with NaNs at frame 89279. Frozen shipping reproduces that same
frame; its observed force is larger by the already-corrected Pick unit factor.

## Correction and approximation

For `H(z)=b/(1-a z^-1)`, the exact unit-circle maximum is
`|b|/(1-|a|)`. Both the double fit and actual stored float coefficients are
checked before accepting it. A nonpassive fit uses the already existing
pole-free loss `a=0`, `b=sqrt(r1)`, where `r1=exp(-2*pi*eta)`. No new physical
constant, force gain or mechanical port is introduced. Authored positive
losses stay dissipative; intentional zero loss remains exactly lossless.

The accepted filter is selected before computing loss phase, integer delay
and the Thiran fraction, so a discarded filter's phase is not retained.
First-mode attenuation stays correct. In the fallback region, second-mode
power attenuation is `r1` rather than `r1*r1`: half the target logarithmic
loss per traversal. This does not imply an exact twofold modal T60 because
the complete delay/allpass phase also contributes to the actual modal poles.
Previously passive fits retain their coefficients. The condition depends on
longitudinal frequency divided by rate, so no universal unchanged-high-rate
claim is made.

## Validation

The stored-float sweep and ten-second tests fail against the pre-fix library
and pass with the correction. The native sweep covers 14256 configured
designs over tunings, all strings/frets, construction/loss-age endpoints,
master tuning and manager/member bend edges. Its requested 768 kHz entry
clamps to the native 384 kHz maximum; actual extended-rate validation is
recorded separately. The largest accepted whole-band gain is approximately
0.99958. First-mode loss is independently checked where representable.

The native sweep checks phase in 10,043 interior designs; the independently
compiled extended-rate sweep checks 9,981 and actually prepares at 768 kHz.
Both contain 14,256 configured designs and reach maximum phase error
0.0275021 rad against the unchanged 0.035 rad bound. The extended first-mode
magnitude error is at most 2.19448e-6 relative, below the independently derived
5.03634e-6 float-coefficient/period rounding bound. Whole-band passivity uses
the actual stored coefficients without a numerical tolerance. Active bend
state is asserted explicitly, and an intentional eta=0 fixture stays lossless.

These phase checks deliberately exclude existing delay/fraction boundaries;
passive and finite does not mean correctly tuned there. A concrete supported
12 kHz case, G string at fret 11, requests an axial fundamental of 4815.72 Hz,
below the 6 kHz Nyquist limit. Its minimum integer delay of one and limited
Thiran coefficient -0.272727 close the first ring phase at 6 kHz instead.
At actual 768 kHz, an extreme ordinary-channel -96-semitone slide on open
high E with +100-cent master tuning requests 262.135 Hz: its desired round
trip is 2929.79 samples, beyond the 1021-sample ring target capacity. The
rendered integer delay 1020 and Thiran coefficient 0 instead close near
728.971 Hz (+178%). Requests already above Nyquist are also excluded from
frequency-accuracy claims. The extended diagnostic counts 1,728 above-Nyquist
requests, 2,427 further cases outside its conservative f<0.24*fs phase band,
2,278 fraction-bound, 62 capacity-bound and 1,120 minimum-delay-bound cases;
these categories overlap. Some conservative exclusions are already accurate
(e.g. 8 kHz open D: 2079.084 Hz requested, 2078.998 Hz closure). This repair does
not redesign integer-delay selection, fractional-delay handover or capacity.
Those are unresolved modal-frequency accuracy limits, not passivity failures.

The repaired exact ten-second case has no nonfinite inspected internal
state. Body state maximum is 4.136 instead of 3.65e36; capture maximum is
0.01250 instead of 4.70e33. Main/Piezo RMS over 9–10 s is approximately
−158 to −160 dBFS, over 143 dB below the initial capture. State inspections
are at block boundaries, not every internal sample. The completed low-rate matrix includes 320 adverse 10-second captures with
unchanged note-offs and internal axial/body/capture/room state checks. All
remain finite. The 119 registered unit/audio checks pass in 618.53 s; refined
active-bend, zero-loss and actual 768 kHz tests subsequently pass in native and
Clang 19 C++17 extended builds. Only a reserve-rationale comment changed in
production after the full run; rebuilt engine object bytes remain identical.
All 1,096 higher-rate corpus files (324,345,600 float samples) freshly rerender
byte-identically to the pre-passivity candidate. This is a bounded fixture
claim, not a claim that every high-rate note is unchanged. Final exclusive
EnginePerformance 0.121246× and worst-preset 0.174333507× pass their existing
0.25×/0.28× gates; detailed callback deadline limits are in the output report.

Source, independent loop-root derivation, raw before/after tails and exact
controls are retained off-tree under `review/acustra/output-levels/worst-state/`.
The separate `review/acustra/axial-loss-passivity/` listening pair isolates
only this fallback on frozen shipping. Output calibration and the earlier
unit/source/load A/B pairs remain frozen. No listening verdict is claimed.
