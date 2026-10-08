# Extended internal sample rates

`ACUSTRA_EXTENDED_SAMPLE_RATES=1` enables internal rendering through 768 kHz,
including 4x rendering at a 192 kHz host rate. The default is zero: ordinary
native builds retain their 384 kHz ceiling and existing fixed storage layout.
`AcustraEngine::maximumSupportedSampleRate` exposes the selected upper bound.

This definition changes the engine and performer ABI. Compile every consumer,
the engine and the performer with the same value. The JUCE-free
`AcustraDSPExtendedCxx17` test library exports the definition publicly; a Rack
adapter compiling the canonical sources itself must supply it consistently.
There is no automatic resampling or plug-in parameter change in this option.

The extended layout reserves 32768 samples per string delay instead of 8192,
32 derivative-history samples instead of 16, 2305 samples per explicit finger
contact pulse instead of 1153, and 2048 contact-air samples instead of 512.
The derivative's 48 kHz reference delay covers a full 16 internal samples.
The pulse capacity preserves the 3 ms pull-off duration at 768 kHz. The string
capacity permits low Drop D with an octave-down bend and -100-cent master
tuning at 768 kHz; notes below the available delay range still reach a bound.

On the local arm64 build, an extended `Performer` occupies 15,902,544 bytes;
three preallocated instances occupy 47,707,632 bytes (45.50 MiB), before wrapper
filters and buffers. These objects belong on the heap. Preparation clears
voices and scheduling/controller history and rebuilds rate-dependent tables;
perform it outside the audio callback. A mode-bank wrapper must synchronize
parameters, controller state and clocks and account for queued notes and
retained/room tails before switching. Active held-voice count alone does not
establish silence.

The wrapper must scale event offsets and block lengths by the actual factor,
decimate every output bus and report its resulting host latency. The engine's
seven-sample output latency and Gather's additional 30 ms are expressed in
internal samples; querying them does not include a wrapper's FIR latency.

## Validation

`Acustra.ExtendedSampleRates` uses heap fixtures and compiles both its library
and tests as C++17. It exercises 352.8, 384, 705.6 and 768 kHz: low bent notes in
both guitar models, master tuning and channel bend, finite output and release,
passive remapped loss poles, derivative reference timing, contact-pulse length,
Gather latency and clean reprepare behavior.

`Acustra.SampleRateLayoutParity` builds one renderer per ABI and compares all
three output buses byte for byte at 44.1, 48, 96, 192 and 384 kHz. Its score
includes a chord, re-pluck, expression, bend, model change and panic with room
and piezo enabled. This covers ordinary notes within both layouts' capacities;
extended storage intentionally permits low pitches formerly limited by the
shorter delay. It does not assert that oversampling itself leaves audio equal.

The allocated maximum is not the amount copied or cleared on a note. Extended
builds activate 8192 string/contact samples through 192 kHz, 16384 through 384 kHz,
and 32768 above 384 kHz; arrival queues activate twice that length. Preparation
selects and clears each active span outside the audio callback. Prefix copies
carry the active size, and indexed ring access maps into that power-of-two
span. This preserves the existing 1x histories and callback workload through
Rack's 192 kHz maximum host rate while keeping 4x capacity preallocated.

Compared with the initial fixed 32768 implementation, very deep bends at 384 kHz
now reach the 16384-sample limit (about 23.44 Hz). The tested low Drop D with an
octave bend and -100-cent tuning remains above it. Explicit legato also depends
on the older sample in the legacy slope observer's unwritten ring slot, so
different ring capacities need not match even when pitch is within bounds.
The parity fixture exercises repeated six-string strokes, explicit legato,
wrapped tails and reset through 192 kHz, and retains its ordinary-note case
at 384 kHz. It does not claim general 384 kHz equivalence between 8192 and 16384
histories.

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON -DBUILD_TESTING=ON
cmake --build build-dsp --parallel 2 --target AcustraExtendedSampleRateTests AcustraNormalLayoutParity AcustraExtendedLayoutParity AcustraDSPCxx17
ctest --test-dir build-dsp -R '^Acustra[.](ExtendedSampleRates|SampleRateLayoutParity)$' --output-on-failure
```
