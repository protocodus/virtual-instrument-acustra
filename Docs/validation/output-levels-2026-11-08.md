# Output calibration — 2026-11-08

Local 1.9.32 review candidate. No publication or listening preference is claimed.

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
transfer and quiet gain in exact arithmetic. The reserve derives from the
measured +1.729 dBFS long-kernel peak plus a design allowance, rounded upward
to 0.1 dB; the final measured margin above is the result, not the allowance.
There is no additional delay. At 44.1 kHz and above, 40 full-control files
(29,341,440 float samples) are bit-identical before and after this reserve.

Dense low-rate maximum-control louder-channel VU reaches +7.991 VU while
retaining peak headroom. This average-level reference is reported explicitly;
it is not a digital clipping criterion. Physical piezo circuit overload,
where present, remains an intentional upstream voltage-domain phenomenon.

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
(116 unit, three audio) in 626.97 seconds. Source and every test executable
were hashed before/after this run and remained unchanged. The older first
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

Final performance gates await a coordinated exclusive CPU window. Native
JUCE plug-in formats and their processor tests are not built here: JUCE and
the required ALSA/Xinerama/Xrandr/Xcursor development packages are absent.
The genuine Jukebox SDK is also absent; HostMock tests are not SDK validation.
The Rack panel's historical pinned-render receipt remains unchanged; its
bounded version-only artwork provenance and missing pinned-font limitation
are documented separately. No release, upload or push is claimed.
