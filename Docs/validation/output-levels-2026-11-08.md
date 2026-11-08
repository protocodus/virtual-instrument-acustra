# Output calibration — 2026-11-08

Local 1.9.33 review candidate; includes the 1.9.32 output calibration and the
subsequent physical axial-loop stability correction. No publication or listening preference is claimed.

## Change and physical boundary

The old memoryless safety limiter was linear through −1 dBFS and approached
0 dBFS. In the measured 1.9.31 matrix, native reconstruction reached +1.99
dBFS; the Rack converter also produced sampled peaks above 0 dBFS.

The unchanged portfolio `OutputStage.h` now follows the complete capture,
room and piezo mix. Main uses one stereo-linked VU guard and look-ahead peak
limiter; the independent direct Piezo output uses its own mono stage. Both
run continuously, including when the auxiliary output is not requested.
Invalid values are rejected before entering their state. Reset clears the
stages; suspension advances their zero-input clocks to preserve exact wake
behavior. The physical sensor's seven-sample alignment remains unchanged.
The additional output look-ahead is `4*floor(sampleRate/2000)` samples.

A +2.3 dB output-only calibration and measured factory Output settings bring
normal Main playing close to −9 VU. No string displacement, tension, bridge
load, body force, pickup sensitivity or analog circuit voltage is rescaled.
The existing physical piezo overload model remains upstream and independent.

The Rack wrapper also places a ceiling-only stage after its signed
sample-rate-conversion FIR. There is no second compressor. It reports
`49 + 8*floor(hostRate/2000)` samples on all buses and quality settings:
241 samples at 48 kHz. The independent pickup converter and guard stay warm
while its cable is absent; cable selection affects only host writing.

## Measurement protocol

Every case uses a fresh production Performer or actual Rack wrapper over the
independent HostMock, its ordinary deterministic seeds, and 64-frame blocks.
All three channels are retained as native interleaved float32, without an
extra render gain. Audio and scripts are frozen outside the checkout at
`review/acustra/output-levels/` in the task's review directory.

For each factory preset, normal playing comprises six open-string singles,
two triads (string indices 0/2/4 and 1/3/5), and a four-note chord (0/1/3/5),
each at velocities 70, 90 and 110: 27 cases. The instance settles silently
for 0.5 s; onset is at 0.02 s, note-off at 0.65 s, capture length 0.8 s.
The muted native preset uses CC2=70; its Rack patch's Bridge Hand=0.55 maps
to byte 69 in the existing controller conversion. The measured trim places
both realizations within the normal calibration tolerance.

For each case, the meter is the maximum full 300 ms sliding-window RMS,
with stereo mean power for Main. Adding 12 dB gives VU, so 0 VU means
−12 dBFS RMS. The median of the 27 case readings targets −9 ±0.75 VU.
Direct Piezo is metered independently as mono. The louder Main channel's
300 ms reading is also reported; it does not silently replace the existing
stereo-mean normal-level convention.

Hard cases include six open strings, E major, 13 repeated chords at 100 ms,
and held chords with ±12-semitone bends, both at factory settings and with
Pick/Touch=1/Pluck Position=0, plus maximum Output=+6 dB repeated chords with
the ordinary or lifted hand. Dense maximum-control VU is descriptive;
0 VU is an average-level reference, not a universal digital overload bound.

Peak reconstruction uses `scipy.signal.resample_poly(audio, 8, 1, axis=0,
window=('kaiser', 8.6))` over every channel. The worst cases are also checked
at 16× and 32×. This is a reconstructed peak estimate, not certified BS.1770.
The shared limiter's −1.5 dBFS sampled ceiling is subject to float rounding;
it does not guarantee an arbitrary continuous-time reconstruction ceiling.

## Final measured results

The final evidence contains 1,848 cases: 864 normal and 984 hard/stress.
The native 48 kHz matrix has 216 normal and 80 hard cases; Main normal
medians range from −9.437 to −8.850 VU. The actual Rack default 48 kHz 2×
medians range from −9.061 to −8.510 VU. Direct Piezo retains its physical
construction balance; native normal medians range from −12.031 to −7.775 VU.

Across the full corpus, sampled peaks reach −1.499998 dBFS (float rounding),
primary 8× reconstructed peaks reach −0.774272 dBFS, and the conservative
worst across 16×/32× and longer 64/128/256-lobe FIR estimates is −0.381901
dBFS. The actual measured margin is 0.382 dB. The 66 selected convergence
cases and exact per-preset/rate matrix are retained with the raw audio in
the external `summary.json` and `report.md`.

The initial stage alone left positive reconstructed peaks at 8/16/22.05 kHz.
Below 44.1 kHz **engine** rate, reciprocal pre/post scaling now gives the
limiter 2.5 dB additional reserve (a −4 dBFS nominal sampled ceiling). Raising
the compressor threshold by the same 2.5 dB preserves its over-threshold
transfer and quiet gain in exact arithmetic. The conservative reserve is validated on the final healthy engine. Earlier
+1.729 dBFS and +7.991 VU observations came from the superseded state and do
not alone justify this final margin. The fresh convergence corpus above
provides the measured 0.382 dB margin; no minimal-reserve claim is made.
There is no additional delay. At 44.1 kHz and above, 40 full-control files
(29,341,440 float samples) are bit-identical before and after this reserve.

The original 11025 Hz Open G max-control case reached +7.991 VU and then
exposed a runaway axial observer during its 10-second extension. The passive
loss repair removes that physical defect; it is not hidden by stronger output
limiting. Fresh low-rate maximum-control louder-channel VU is −0.0473.
Across the complete healthy corpus the dense-stress maximum is +1.0372 VU
on the louder Main channel, +0.9763 stereo-mean and +0.2143 on direct Piezo.
These descriptive average levels are not digital clipping criteria. Physical
piezo circuit overload remains an intentional upstream voltage-domain effect.

All 752 low-rate cases were freshly rendered, including 320 adverse cases
extended to 10 seconds with unchanged releases and internal state monitoring.
The 1,096 higher-rate captures (324,345,600 float samples) were also freshly
rendered and are byte-identical to their pre-passivity-fix counterparts.
The worst long-kernel peak was independently recomputed from the current
80,000-frame 8 kHz Mahogany Thumb full-mix capture, at frame 1233.5/channel 1.
Its original 1.6-second prefix is byte-identical because this particular
geometry already used passive scalar loss; this explains why the exact
−0.381901 dB maximum is unchanged despite the repaired 11025 Hz runaway.
The current raw-file SHA and reconstruction are in
`fresh-convergence-provenance.json`, not inferred from superseded captures.
See [axial stability](axial-loss-passivity-2026-11-08.md) for the independent
unstable-pole proof,10-second decay and unresolved delay tuning boundaries.

## Verification and limits

The new OutputLevels suite independently meters all 296 native cases and
checks finite, bounded, allocation-free output. OutputStage tests verify
below-knee delayed bit transparency, stereo linkage, reset determinism,
rate-dependent latency, reciprocal reserve/VU-transfer equivalence, adversarial
peak control through 768 kHz, and exact
mechanical observer agreement at different output gains. Existing capture,
Performer, optional-bus, physical-circuit and wrapper contracts remain in
force. Routing-algebra fixtures run below the new compressor knee and account
for the added output delay rather than widening their numerical tolerances.

LowRateOutputState adds 45 factory/construction cases at 8/16/22.05/32/44.1
kHz, proving stable capture poles and room absorption, finite internal state
and delay buffers, and sustained exact pure-Piezo Main/direct equality under
full Room/Width/Mix. Acustra's capture sections top out at 1400 Hz and do not
share the ukulele's high-frequency radiation shelves.

An adversarial ±100 input reveals approximately 21 float ULPs of sampled
ceiling overshoot from the shared gain representation `1-(1-hold)`. Its new
test uses an input-scaled error bound derived from subtraction, mean-cast and
multiply rounding, with the double running-sum error bounded separately.
The shared master is unchanged; this tiny numerical effect is not the
measured musical overload that motivated the fix.

Final GCC Release validation passes all 119 registered unit/audio tests
(116 unit, three audio) in618.53 seconds. Source and every test executable
were hashed before/after this run and remained unchanged. Refined active-bend,
zero-loss and actual 768 kHz first-mode/phase checks passed afterward, including
a derived float-rounding bound for magnitude (passivity itself has no
numerical tolerance). A later production comment update rebuilds to an
identical engine object, so the full-run DSP behavior is still final. The older first
118-test run is retained as diagnostic history; its three failures were
obsolete delay/linear-routing test oracles, corrected without loosening their
numeric tolerances. A subsequent three-tool check covers the wording-only
removal of obsolete post-limiter linearity claims.

The canonical C++17 target builds. Clang 19 C++17 extended-rate objects pass
`-O2 -Wall -Wextra -Werror`. The actual Reason adapter over the independent
HostMock passes the full wrapper contract, all eight factory patches at all
five declared host rates, and the separate OutputRate tests. The legacy
all-patch two-second hard-strum window remains unchanged; it is distinct
from the independently measured normal-playing calibration above.

Final exclusive performance gates pass: six-string engine 0.121246× realtime
against 0.25×; worst factory median 0.174333507× against 0.28×. The bounded
shipping-versus-final 64-frame callback comparison measures +17.5% to +95.6%
median cost depending on rate/scenario. At 96 kHz attack design exceeds the
667 µs block deadline: final Finger 763 µs/Pick 1006 µs versus shipping 581/812 µs.
Return/held medians stay below deadline, but OS tails and a 2.01× worst preset
block mean these average gates do not establish worst-case realtime.
The exact flags, baseline objects, alternating samples and scenario table
are retained in `review/acustra/output-levels/cpu-final/`.

Native
JUCE plug-in formats and their processor tests are not built here: JUCE and
the required ALSA/Xinerama/Xrandr/Xcursor development packages are absent.
The genuine Jukebox SDK is also absent; HostMock tests are not SDK validation.
The Rack panel's historical pinned-render receipt remains unchanged; its
bounded version-only artwork provenance and missing pinned-font limitation
are documented separately. No release, upload or push is claimed.
