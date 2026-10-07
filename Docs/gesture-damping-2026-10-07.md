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

The physical assumptions are authored: a fretting pad contacts 18 mm inside
the old speaking length; an open string uses its existing picking position;
the soft contact relaxes in 6 ms. Strength is bounded at 0.22 and derives from
the existing requested hand loss, with a weaker open-string contact. The
contact position is rounded to the integer waveguide grid and frozen at
landing, including in a retained tail. These choices have not been fitted to
release recordings. The available sustained-note corpora cannot validate
this release gesture or establish a listening preference.

The C++17 build and Rack flags `-Wall -Wextra -Werror` pass. Expanded
`Acustra.ReleaseRealism` checks local velocity/storage energy, modal nodes
and antinodes over several phases, exact held/join/pedal audio, release
boundaries, active late same-pitch reattack headroom, tail-state copies,
quiet stored energy and panic. The unchanged `Release`, `ReleaseJoin` and
`RepeatedPluckRealism` suites pass. In the existing return-to-open test under
other held strings, the high-frequency step is 18.8–19.7 dB above the local
floor at 44.1/48/96 kHz, below its unchanged 25 dB guard. Existing two-octave
slide and natural-harmonic release guards also pass.

The isolated 24-pair audio audit uses one frozen binary, 48 kHz stereo,
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

The inactive path adds one false contact check per physical string (and an
existing active tail's check). Active contacts touch two grid cells and one
small state per plane, with constant work per sample. Exponential and square
root calculations occur only at release setup. There are no audio-thread
allocations or per-mode reconfigurations. A whole-engine CPU percentage must
be measured on the integrated implementation under a quiet benchmark window.
