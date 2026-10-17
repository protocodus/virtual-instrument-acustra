# Final native build and test coverage

The final DSP runtime is `edba920`, library SHA256 `282ae53ffe4a0f977bb9c11ec4c06858303dd118aa00a03bc8315e021da7ca00`. The Release build completed for DSP/tools and arm64 Standalone, AU and VST3. Architecture inspection and relevant CMake settings are retained beside the logs.

`final-tests.log` is the unmodified 89-test run: 88 passed and `Acustra.Engine` failed because a test oracle omitted the new flat studio level factor. The test-only patch is retained (committed as `5402854b32fe1bfa8368b46d20184c1df4d0b28c`); its existing waveform/monotonicity/tolerance/fade checks were preserved. `engine-transition-build.log` and `engine-transition-tests.log` record the rebuilt complete Engine executable passing after that repair. The runtime library did not change. Together these cover **89/89 distinct tests**, without claiming that the original CTest invocation passed all 89 or rerunning unrelated tests.

`receipt.json` maps every test to its passing evidence and hashes the final relevant sources. The preceding physics receipt deliberately keeps its historical test-source hash; the later engine-transition review and this receipt identify the repaired source. No build, tests, render, or benchmark was run to package these files.

Reproduction commands (past full-run parallelism is not asserted):

```sh
cmake --build build-audit -j 2
ctest --test-dir build-audit --output-on-failure -j 2
cmake --build build-audit --target AcustraEngineTests -j 2
build-audit/AcustraEngineTests
```

Build logs include the VST3's existing signature being replaced by an ad-hoc signature before successful packaging. This is a native build record, not interactive DAW playback or a newly signed distribution claim.
