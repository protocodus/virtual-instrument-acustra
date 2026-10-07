# Local hand contact during note endings

The release hand now damps a point on each of a string's two travelling-wave
planes. Partials with a node at that point survive better than partials that
move there. This changes the colour of a note ending while retaining the
existing scalar release T60, release-velocity map and return-to-open deadline.
It introduces no noise source, random draw, attack delay or new performance
control. The setup-only `PerformanceRealism::gestureDamping` switch isolates
this mechanism for comparisons.

Contact begins only in the existing physical release and vacated-string
paths. Ordinary key-up still has the inclusive global 1/32-note join window;
a repeated stroke inside it cancels release without changing its sound.
Held notes and pedal-held notes acquire no contact. Pedal-up retains its
existing immediate damping rule. Panic clears contact state with the sound.
A fresh note lifts the contact; a different note first retains the old contact
with the copied old-wave tail. A quiet bridge-force observer does not discard
non-negligible stored contact energy.

The contact is a passive relaxing two-port. Let `u` be the difference of the
two incoming displacement-wave increments, `s` the internal state,
`c = -exp(-1 / relaxationSamples)` and `B = sqrt(r * (1-c*c))`. Its outgoing
velocity difference and next state are given by the symmetric matrix
`[[1-r+r*c, B], [B, -c]]`. Its eigenvalues are `1` and `-r-(1-r)*c`, both
bounded in magnitude by one. Thus directional wave-velocity energy plus
`0.5*s*s` cannot increase at this local, fixed-geometry contact. Its unity DC
transfer lets the contact's displacement offset relax to zero instead of
pinning a deformation that would be released as a late pluck. This local
proof does not establish global passivity of a moving fractional-delay
string or of every hand-lift/reconfiguration boundary.

The final hand lift also matters: another sounding string can continue to
drive this contact through the bridge, leaving a nonzero displacement offset
at the return-to-open deadline. Clearing that offset instantly made a late
tick. During the final 6 ms, the hand now withdraws with the cubic smoothstep
`f = x*x*(3-2*x)`, where `x` reaches zero on the last sample before the
unchanged deadline. Both `B` and the displacement coefficient `K` are scaled
by `f`; the offset is exactly zero when contact state is cleared. The earlier
release damping, ownership, join window and deadline stay unchanged.

For each fixed `f`, the scattering matrix still has the passive form above
with strength `r*f*f`. Changing `K*f` contributes additional physical hand
motion: `h = (K_previous-K_current)*s_previous` to the first outgoing velocity
and `-h` to the second. Its energy contribution is
`2*h*(passive_first-passive_second) + 2*h*h`, bounded by the corresponding
absolute-value expression. Smoothstep bounds the coefficient step by
`1.5*K/withdrawalSamples`, apart from float rounding. Tests account for this
work explicitly; they do not label the moving contact globally passive.

An independent 19,200-case grid covers 8–384 kHz, strengths 0.001–0.22,
the waveguide's low-frequency floor through 12 kHz below Nyquist, 16 phases,
and continuing, frozen and stopped inputs. Every final displacement offset
is exactly zero. For the sampled frequencies at and above 73.4 Hz, outgoing
velocity energy plus remaining contact storage does not exceed incoming
energy plus initial storage. Lower bends are supported and require a narrower
claim: the grid reaches 1.280 times that final-interval budget at 41.2 Hz,
1.326 at 36.7 Hz, 1.554 at 20.6 Hz and 1.595 at the 44.1 kHz waveguide's
5.385 Hz floor. These are local residual-interval stress results, not whole-note
gain figures. The explicit work accounting remains valid; the moving hand
is bounded in this grid but is not globally passive.

The physical assumptions are authored: a fretting pad contacts 18 mm inside
the old speaking length; an open string uses its existing picking position;
the soft contact relaxes in 6 ms. Strength is bounded at 0.22 and derives from
the existing requested hand loss, with a weaker open-string contact. The
contact position is rounded to the integer waveguide grid and frozen at
landing, including in a retained tail. These choices have not been fitted to
release recordings. The available sustained-note corpora cannot validate
this release gesture or establish a listening preference.

The pad uses the actual physical speaking length under conventional and MPE
manager slides, including slides beyond the decay table's 0–20-fret clamp.
MPE member tension bends keep their existing fixed length. The integrated
geometry regression covers all three gestures, both ends of that table and
both wave planes at 44.1/48/96 kHz.

The C++17 build and Rack flags `-Wall -Wextra -Werror` pass. Expanded
`Acustra.ReleaseRealism` checks local velocity/storage energy, modal nodes
and antinodes over several phases, exact held/join/pedal audio, release
boundaries, active late same-pitch reattack headroom, tail-state copies,
quiet stored energy and panic. The unchanged `Release`, `ReleaseJoin` and
`RepeatedPluckRealism` suites pass. In the existing return-to-open test under
other held strings, the final integrated high-frequency step is
23.337/21.714/21.367 dB above the local floor at 44.1/48/96 kHz, below its
unchanged 25 dB guard. Existing two-octave
slide and natural-harmonic release guards also pass.

With all mechanisms combined before the later retuning change, the 96 kHz
return-to-open guard exposed the abrupt lift at 25.230 dB. Continuous withdrawal
reduces it to 22.548 dB without changing the 25 dB limit. All 48 combinations
of the four mechanisms at 44.1/48/96 kHz pass. In the same native-level
1.4-second held-chord fixture, the whole-phrase peak stays exactly unchanged
(0.2695–0.2815); the before/after difference peak is 0.000074–0.000081 and its
RMS is 77.4–79.0 dB below the phrase RMS. The 33.5 ms hand-back window's RMS
changes by less than 0.0008 dB. These figures use identical source/calibration
except for the withdrawal call; they are signal checks, not listening claims.
The expanded release test checks the exact deadline at three release
velocities and rates. Removing only the withdrawal call fails all nine
deadline/offset regressions. The four focused release suites and C++17 build
pass. The final integrated results above include the subsequent retuning fix;
the complete 68-suite DSP/tool run and both native host suites also pass.

The original development audit, before the withdrawal and retuning fixes,
used 24 isolated audio pairs from one frozen binary, 48 kHz stereo,
Original/Dreadnought/Spruce/Stereo, Room 0, release noise 0, explicit string
channels and a common linear audition gain of two. Other realism switches
are disabled. Long holds are byte-identical throughout; all 16 joined tremolo
attacks and their final join endpoint are byte-identical. In the fretted
release's 25–125 ms window, the 2–12 kHz versus 80–800 Hz balance falls about
7.8–10.6 dB while total RMS changes only -0.13 to +0.005 dB. These are signal
descriptors, not a claim that the candidate has been preferred in listening.
Raw float files, WAVs, the audit harness, analysis script and source/binary
hashes are retained in `build-gesture-release-oct07` with
`audio-comparison.html` and `audio-comparison.json`.
These measurements describe that development version. The final controlled
seven-variant comparison is documented in the
[integrated report](natural-performance-2026-10-07.md).

The inactive path adds one false contact check per physical string (and an
existing active tail's check). Active contacts touch two grid cells and one
small state per plane, with constant work per sample. Exponential and square
root calculations occur only at release setup. There are no audio-thread
allocations or per-mode reconfigurations. The integrated report records the
final quiet-window CPU comparison, including existing deadline failures.
