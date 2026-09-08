# Final paired native callback timings — 8 September 2026

The final combined engine has p95 below the callback deadline in every measured configuration, including all four new measured guitars. At 96 kHz, the largest current p95 uses 68.7% of a 32-frame deadline, 40.0% at 64 frames and 25.7% at 128 frames. The 64/128-frame results leave substantial measured p95 headroom before host overhead. Ordinary-desktop scheduling outliers still occurred, so these measurements do not promise glitch-free operation at any block size.

The [standalone probe](../Tools/CallbackBenchmark.cpp) links namespace-renamed task-start and final engines in one executable. It alternates their execution order on every trial. Each of 108 configurations has 16 warmup pairs and 256 measured pairs. The timed interval contains every noteOn and the following full process block. The six-string E-major chord uses MIDI 40,47,52,56,59,64 on channels 1–6; repick repeats it after 50 ms of ringing, including tail capture. MIDI 100 selects the eighth natural harmonic. Fresh construction/prepare and repick pre-roll occur outside timing. Every repeated fresh-state callback has a deterministic output hash, finite samples and the expected active-string count. A measured microphone delay can legitimately leave a short initial block silent.

The cases cover Original nylon, Original steel, Bellido1978 nylon, Washburn1897 steel, SantaCruzOM2022 steel and MartinD18V2007 steel. Named guitars use their native shape/wood settings. The old baseline has no named banks and therefore uses Original with matching material, shape and wood; those rows compare complete model costs, not two implementations of the same measured instrument. Both sides use velocity 1, Finger, Stereo mic, Touch 0.58 and Pluck Position 0.28, with their own compiled physical calibration.

Team compilation, rendering and native-test workloads were paused during this final run. Normal desktop applications remained active, and no real-time thread priority or CPU affinity was requested. Current exceeded its deadline in 19/27,648 observations; baseline in 14/27,648. The largest current outlier was 3.666 ms at 96 kHz / 64 frames. Quantiles use nearest rank. This native probe excludes JUCE, host mixing and device overhead; a finite observed maximum is not a worst-case execution-time bound.

The table takes the largest current statistic across the six guitar/material cases and three event cases at each rate/block size. The maxima in different columns can come from different cases.

| Rate / frames | Deadline (µs) | Largest p50 (µs) | Largest p95 (µs) | Largest p95 / deadline | Largest observed max (µs) | Current overruns / 4,608 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 48k / 32 | 666.7 | 155.0 | 285.6 | 0.428 | 2061.9 | 8 |
| 48k / 64 | 1333.3 | 187.1 | 216.5 | 0.162 | 512.3 | 0 |
| 48k / 128 | 2666.7 | 261.1 | 279.8 | 0.105 | 956.9 | 0 |
| 96k / 32 | 333.3 | 208.6 | 229.0 | 0.687 | 833.8 | 6 |
| 96k / 64 | 666.7 | 238.8 | 266.7 | 0.400 | 3666.0 | 3 |
| 96k / 128 | 1333.3 | 312.0 | 342.5 | 0.257 | 2865.9 | 2 |

Per-model detail for the most demanding block, 96 kHz / 32 frames (largest p95 across the three events):

| Current model | Material | Baseline p95 (µs) | Current p95 (µs) | Current p95 / deadline |
| --- | --- | ---: | ---: | ---: |
| Original | nylon | 187.0 | 229.0 | 0.687 |
| Original | steel | 192.2 | 195.0 | 0.585 |
| Bellido1978 | nylon | 190.0 | 224.3 | 0.673 |
| Washburn1897 | steel | 192.0 | 192.9 | 0.579 |
| SantaCruzOM2022 | steel | 192.9 | 200.4 | 0.601 |
| MartinD18V2007 | steel | 188.8 | 191.9 | 0.576 |

Across all cases, paired median current/baseline ratios range from 0.942 to 1.362. The [separate isolated Gaussian optimization](gaussian-initialization-cost-2026-09-08.md) compares identical model settings and shows 0.4%–13.2% lower callback medians with bit-identical audio. The complete-engine comparison also contains the new banks, delay handling and physical calibration.

The [machine-readable report](callback-benchmark-2026-09-08.json) contains every timing sample, output hash, source hash, compiler version, build command and executable hash. Source matched the working DSP files at review. The frozen directory is `/var/folders/jl/cjv384t54g3bpst5xzs9zqnc0000gn/T/acustra-callback-final-20260908-2gh3rnex`. Combined contact tests pass with Fourier error 1.45e-8 and broad-contact error 5.14e-8, including 16 exact-uniform cases.

To rerun the frozen binary, choose a new output path:

```sh
/var/folders/jl/cjv384t54g3bpst5xzs9zqnc0000gn/T/acustra-callback-final-20260908-2gh3rnex/AcustraCallbackBenchmark /tmp/acustra-callback-new-run.json 256 16 --guitar-models
```

To build a new pair, follow the recorded compiler commands with new frozen `baseline/Source` and `current/Source` trees. Compile the probe as two adapters with `ACUSTRA_CALLBACK_ADAPTER=baseline` or `current`; compile each engine and its adapter with matching `acustra=acustra_baseline` or `acustra=acustra_current`. Define `ACUSTRA_CALLBACK_GUITAR_MODELS` for the current adapter. Compile the main probe without adapter/namespace definitions, then link all five objects. Keep each engine and adapter on matching headers/capacity definitions. There is no timing assertion or CMake mutation.
