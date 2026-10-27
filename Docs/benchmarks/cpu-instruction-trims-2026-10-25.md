# Acustra CPU: instruction trims, 2026-10-25

Six exact changes to the canonical DSP lower the instructions the Rack-like
build executes on the factory presets by **9.56 %**. Every float32 output
sample of the regression stream is byte-identical to the baseline's, at
every rate and block size tested, in the Rack-like and the host builds.
No quality setting, default, latency, oversampling factor, voice count,
noise model, parameter range, filter length or test tolerance changed.
Baseline: `9621b3a` (1.6.3-build.24). Version 1.6.4.

## Protocol

The tool is `Tools/CpuRegressionStream.cpp` (protocol at its head), built by
`Tools/BuildCpuRegressionStream.sh` and registered as the CMake tool target
`AcustraCpuRegressionStream`.

- **Workload.** All eight factory presets (`Source/DSP/AcustraPresets.h`),
  each prepared as the preset demo renderer prepares it and playing its
  committed phrase `Docs/audio/midi/<NN>-<slug>.mid` from the start for 4 s,
  every event at its own sample offset inside the block, through the
  Performer, as the Rack wrapper drives it. Product configuration: the
  Rack's default 2x quality at 48 kHz, so the engine runs at **96 kHz in
  128-frame blocks** (64-frame batches times the factor): 3,072,000 frames,
  six strings, stereo Main. Heaviest supported quality: 4x at 48 kHz, the
  engine at 192 kHz in 256-frame blocks, 2 s per preset.
- **Builds.** Rack-like (primary): clang 18.1.3, `-std=c++17 -O3 -fno-builtin
  -DACUSTRA_EXTENDED_SAMPLE_RATES=1 -DNDEBUG`, the Rack's layout and flags as
  nearly as a host without the Jukebox SDK allows. Host: GCC 13.3, the same
  without `-fno-builtin`. No fast-math, FMA or LTO.
- **Count.** `valgrind --tool=callgrind --collect-atstart=no
  --toggle-collect='*acustraCpuRenderStream*'` (valgrind 3.22): rendering
  only; reading MIDI, construction, `prepare` and writing the output are not
  counted. Instruction counts (Ir) are reproducible on this shared machine
  (4 cores, five other agents' builds running); wall and thread-CPU timings
  there are not, and none is quoted. Ir is not Reason-host or SDK-native
  time: an instruction removed on x86-64 is not a guaranteed saving on the
  SDK's arm64 code (the 2026-10-16 report's SDK screens rejected changes that
  removed work and still ran slower).
- **Parity.** The tool writes every preset's Main L/R float32 frames.
  After each commit the Rack-like build rendered the whole matrix below and
  `cmp` compared it with the baseline's: 96 kHz/128 (4 s), 48 kHz/64 (4 s),
  192 kHz/256 (3 s), 44.1 kHz/64 (3 s), 88.2 kHz/100 (2 s), 48 kHz/1
  (0.5 s) and 96 kHz/37 (2 s), all eight presets, 143,114,400 bytes in all.
  The scalar fallbacks of the new vector code (non-GNU compilers, non-x86
  bridge) were also forced on and rendered the same bytes.

## Results

Rack-like build, product workload (96 kHz, 128 frames, eight presets):

| Commit | Mechanism | Grade | Ir | Step | Cumulative |
| --- | --- | --- | ---: | ---: | ---: |
| `9621b3a` | baseline | | 50,774,219,730 | | |
| `5408b8f` | room converter operands held as exact doubles | exact | 49,276,232,347 | −2.95 % | −2.95 % |
| `9162e3f` | bridge coefficients and residues packed in run order | exact | 47,941,069,279 | −2.71 % | −5.58 % |
| `87d9b1d` | room early-reflection line mirrored, no wrap per tap | exact | 47,288,269,297 | −1.36 % | −6.87 % |
| `516f445` | body ordered L/R pairs moved down with one shuffle | exact | 46,882,765,279 | −0.86 % | −7.66 % |
| `d68a934` | room output taps held once per channel lane | exact | 46,398,358,705 | −1.03 % | −8.62 % |
| `27945bd` | room converter histories mirrored, one run per filter | exact | 45,918,091,633 | −1.04 % | −9.56 % |

The same comparison, baseline against the final tree:

| Build and workload | Baseline Ir | 1.6.4 Ir | Change |
| --- | ---: | ---: | ---: |
| Rack-like, 2x quality (96 kHz/128, 8 presets × 4 s) | 50,774,219,730 | 45,918,091,633 | −9.56 % |
| Rack-like, 4x quality (192 kHz/256, 8 presets × 2 s) | 47,914,877,145 | 43,271,909,109 | −9.69 % |
| Host GCC -O3, 2x quality (96 kHz/128, 8 presets × 4 s) | 56,012,384,822 | 53,040,660,150 | −5.31 % |

The host build gains less: GCC generates different baseline code, and the
changes were chosen on the Rack-like clang build. Its output is
byte-identical to the baseline's as well.

Where the baseline spent its instructions: the string loops and voice
bookkeeping inside `process` about 40 %, the body's 160 radiation modes
21 %, the bridge's modal load 16 %, the room 13 %, the capture filter 2 %,
the piezo 2 %. Out-of-line libm calls were under 1 %, but the host's
libstdc++ inlines what the SDK's libc++ calls out of line, so this count
cannot see that class of saving; earlier rounds took it on SDK object code.

## What each change does

All six keep every operand, product, sum and order; they remove
conversions, address arithmetic and shuffles around the arithmetic.

1. **Room converter operands.** The decimating and interpolating sincs
   multiply in double and converted each float tap and each float history
   sample on every product. The taps are still designed and rounded in
   float but held as their binary64 images; the histories hold their values
   widened as written, the output history as left/right pairs. Widening a
   float is exact. The equality assumes one floating-point environment for
   the life of a history value (a denormal input written under one DAZ
   setting and read under another would keep its write's setting); the
   engine never changes the environment, and the room's input is flushed at
   1e-20 before most paths reach it.
2. **Bridge packing.** When `configureBridge` lists the active slots it also
   copies each slot's heave/rock coefficient pairs side by side and its
   float residues as doubles, so a rocking mode loads each pair at once.
   The states stay in `heaveModes`/`rockModes`, where the tests and the
   construction fades read and copy them.
3. **Early-line mirror.** Every input is written at the head and one
   capacity on; every delay is in [0, capacity] by `prepare`'s clamps, so
   each tap reads a plain pointer. +8 KiB per engine.
4. **Body shuffles.** The second and fourth pairs of each ordered group
   reach the low accumulator lanes as the high halves of the interleaved
   first and third pairs (`movhlps`) instead of two `shufps` each. The low
   lanes add the same pairs in the same order; only the never-read upper
   lanes differ.
5. **Output tap pairs.** Each interpolator tap is stored as `{tap, tap}`,
   one aligned load instead of a load and a broadcast. +6 KiB.
6. **Converter mirrors.** Both converter histories are written twice so
   each filter reads one contiguous window in one loop (the second span of
   the old two-span walk did not unroll). +7 KiB.

## Tried and not shipped

Each was built, null-tested byte-identical and counted; none is in the
tree.

| Candidate | Ir change | Why not |
| --- | ---: | --- |
| Skip the delay follower's step when the delay sits at its target | −0.25 % | too small to matter; adds a branch per loop sample |
| Left/right through the capture filter's sections in one vector | −0.32 % | too small; the 2026-10-13 SDK screen also measured it slower |
| Force `finishVoice` inline into `process` | +0.39 % | more instructions (spills) |

Further exact candidates, not built: precomputing `1 − gain` and
`N2 − A2` of the string loss sections at their three writers (estimated
0.3–0.6 %); a contiguous packed layout for the room's receiver arrivals
(about 1 %, but its natural vector form separates products from sums,
which changes bits on FMA-contracting arm64 host builds, so only a scalar
form could ship); moving the bridge's mode states into the packed layout
(the remaining loads, but the tests read those states where they are).
Nothing in this round needed a listening test.

## Validation

- Parity: the matrix above after every commit, Rack-like build; the final
  tree also in the host build (96/48/192/44.1 kHz and the one-frame block),
  where the output is additionally byte-identical to the Rack-like build's.
- Strict C++17: `AcustraEngine.cpp` and `AcustraPerformer.cpp` compile with
  clang 18 and GCC 13, `-std=c++17 -Wall -Wextra -Wshadow -Werror`, with
  and without `ACUSTRA_EXTENDED_SAMPLE_RATES`; `AcustraDSPCxx17` builds.
- Unit tier, final tree (`build-dsp`, GCC 13 Release, `ctest -L unit -j2`):
  101 of 108 passed under CTest. The other seven (Engine, BodyShape,
  ConstructionHistory, VelocityRealism, StringPitchRealism,
  StrummingRealism, PhysicalHandRealism) hit their CTest time limits on
  this machine, loaded to about 11 on 4 cores by other builds; each was then
  run directly, unchanged, and passed every assertion (Engine in 433 s
  against its 600 s limit, VelocityRealism in 70 s against 60 s). No limit
  or test was changed. A focused 16-suite body/bridge/room/capture run after
  the fourth commit also passed.
- Not run: the `audio` tier (every demo rendered twice, hours on this
  machine); the plug-in build and its `fuzz` tier (`Acustra.ParameterMatrix`),
  since no plug-in source changed; the DSP build registers no `fuzz` test.
  No SDK-native module was built (no Jukebox SDK here).
