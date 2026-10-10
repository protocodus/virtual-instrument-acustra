# Bridge-hand damping follows the vibrating string's pitch

The existing bridge-hand model specifies damping in time: its fundamental
and upper-frequency decay rates are linked by a fixed 0.62 T60 ratio. A
waveguide implements this as a loss on each round trip. Before this change,
the high shelf used the original MIDI frequency even after the vibrating
string had been slid or bent. Raising the note an octave applied that loss
twice as often, making the hand artificially dull the raised string faster.

The shelf now uses the same current frequency as the loop, including slides,
member-channel tension bends, vibrato and attack settling. A retained wave
uses its captured frequency when CC2 changes, even if the new note has since
moved elsewhere. Its original frequency and intrinsic loss remain frozen.
No control, parameter ID, calibration value, hand-pressure map or release
window changes. With no bridge-hand pressure, the audio path is unchanged.

This repairs a physical/time-discretization inconsistency. It does not
calibrate the hand itself against new recordings or establish a listening
preference; the available dry-note corpora contain no matched muted notes.

## Regression evidence

Baseline: `3d37a38bfd6ff349b4fe4e6daac09ab20aa2d260`.
The new `Acustra.PalmMutePitch` test fails against that source and passes
against the working-tree change. Tests cover 44.1, 48 and 96 kHz, low/middle/
high strings, two ages and two pressures, positive/negative slides, MPE
bends, attack settling, and a retained wave after the main voice changes
pitch. Complete string-filter transfers are compared at partials 1, 4 and 12.

| Invariant | Baseline | Corrected |
| --- | ---: | ---: |
| Maximum mismatch in added hand decay, slide versus the equivalent physical fret (324 comparisons) | 51.0249 dB/s | 0.00194243 dB/s |
| Maximum high-shelf hand-rate change when pitch moves | 127.101% | 0.000480667% |
| Maximum retained/held intrinsic-and-hand decay difference | 0.0000442318 dB/s | 0.0000442318 dB/s |

The last comparison excludes the tail's separate provisional 10 ms damping;
it verifies the live CC2 shelf and intrinsic loss on the retained branch.
Both old branches shared the same pitch mistake, so their mutual agreement
alone would not catch it. The physical-fret and time-scale checks do.

The Release build passes all seven selected suites: Engine, Release,
StringPitchRealism, PalmMutePitch, StringDecayRealism, RepeatedPluckRealism
and ReleaseRealism (297.56 seconds total). The shared DSP C++17 target builds,
and the changed engine/new test pass GCC C++17 `-Wall -Wextra -Werror` syntax
checks. These are targeted DSP checks, not the entire repository suite.

To reproduce with an ordinary CMake installation:

```sh
cmake -S . -B build-palm-pitch -DCMAKE_BUILD_TYPE=Release \
  -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=OFF -DBUILD_TESTING=ON
cmake --build build-palm-pitch --target AcustraPalmMutePitchTests AcustraDSPCxx17
ctest --test-dir build-palm-pitch -R '^Acustra.PalmMutePitch$' --output-on-failure
```

## Listening material and scope

The local review lives in `/workspace/scratch/acustra-realism`. Its renderer
and source/audio manifests accompany four 9.6-second stereo PCM16 files at
48 kHz: Original and Bellido, each before and after. Identical Pick phrases
slide low E and D strings up/down an octave, repeat notes, and change hand
pressure under a plucked refret. All four files share one gain of
2.6045101463952918, with no EQ, compression, per-file normalization or time
alignment. Highest sample peak is -3 dBFS. This is a diagnostic phrase, not a
recording of a guitarist. The same phrase with hand pressure disabled is
byte-identical before/after in both models.
The change is subtle in these phrases: difference-signal RMS is 63.0 dB
below the Original phrase and 64.3 dB below the Bellido phrase. This does
not demonstrate audibility or preference.

The Rack Extension consumes this repository through its `Instrument`
submodule. Integration records a committed source revision in that gitlink
and runs the Rack validation workflow; no instrument patch is copied into
the Rack tree. The Rack checkout and its validation record identify the
integrated revision separately from this DSP baseline comparison. No package
publication is part of this verification. JUCE plugin and licensed Reason
host builds are outside the local DSP checks reported above.

## Re-validation on main `844cf2f` (2026-10-10)

The change was rebased onto `844cf2f`, after the Bellido's removal and the
bridge termination work. There `configureVoice` reads its pitch geometry from
`PitchGeometryCache`, and the hand's shelf had been the cached unbent
frequency's last reader, so the cache drops that field. Nothing else in the
correction changed.

- **The test against main.** `Acustra.PalmMutePitch`, compiled against
  `844cf2f`'s own DSP, fails with the same figures as on `3d37a38`: 51.0249
  dB/s over the 324 slide/fret comparisons and a 127.101% hand-rate change.
- **The test against the rebased change.** It passes with 0.00194243 dB/s
  and 0.000480667%. The retained/held difference stays 4.42318e-05 dB/s.
- **Renders without hand pressure are unchanged.** The five
  `Tools/RenderRealismSongs.cpp` passages were rendered dry with each tree.
  The three with no CC2 (01, 03, 05) are byte-identical. The two that press
  the hand (02, 04) first differ 8 and 3 ms after their first nonzero CC2.
  Their difference sits 133.8 and 84.6 dB under the signal, because their
  pitches move only through attack settling.
- **Tests and builds.** The JUCE-free Release build passes the full CTest
  suite, 71 of 71, with the Python tool tests required. `AcustraEngine.cpp`,
  `AcustraPerformer.cpp` and the new test compile as C++17 with `-Wall
  -Wextra -Wshadow -Wpedantic -Werror` under GCC 13 and Clang 18.

Nobody has listened to the change. The scratch listening files and Bellido
renders described above belong to the `3d37a38` review and were not rebuilt.
