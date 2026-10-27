# Packaging and the release workflow

Package layout, build numbers and the tag-driven release workflow, moved
unchanged from the README's **Build** section on 2026-10-17
([README: Build](../../README.md#build)).

### Packages

Customer files use `Acustra-<version>-build.<number>-<Platform>-<arch>` and live
under `dist/<version>/build.<number>/<Platform>-<arch>/`. For example, a
universal 1.1.3 build numbered 1 uses `dist/1.1.3/build.1/macOS-universal/`, containing
`Acustra-1.1.3-build.1-macOS-universal.zip`, the matching `.pkg`,
`.manifest.json` and `-SHA256SUMS.txt`. The manifest records the compiled
identity, source revision, architectures and package hashes. These are local
development packages; the existing validation findings are not a release claim.

The default `dist/` belongs to the primary Git checkout. Linked worktrees use
the Git common directory plus the project's repository-relative path, so their
packages land in the same stable location. Without Git, it is the source
project's `dist/`. Set `DIST_ROOT` to choose another root. Earlier version/build
folders are retained. A populated version/build/platform folder is immutable:
packaging rejects it, so select a fresh build number for a new package. Generated `Acustra_artefacts/` files and package staging
folders are compiler intermediates; installed `Acustra.vst3`, `.component` and
`.app` names and host IDs stay stable.

`ACUSTRA_BUILD_NUMBER` is a positive integer or `run.attempt`, defaults to `1`
locally, and defaults to the workflow's run number (`GITHUB_RUN_NUMBER`) in CI. CMake freezes
it with the project version from `project(Acustra VERSION ...)` and the source revision. Changing that identity rebuilds
the wrappers; each linked format receives its frozen identity record (the
editor reads its version and build number from it), and macOS
bundles embed the build number. Packaging rejects a cache
that disagrees with those records or macOS bundle metadata. Assign a new build
number for a new distributable; changing the package name alone is insufficient.

To rebuild only the macOS product formats and package build 1:

```sh
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_NUMBER=1
cmake --build build-macos --config Release --parallel 3 --target Acustra_VST3 Acustra_AU Acustra_Standalone
./scripts/sign-and-package-macos.sh
```

The full validation script accepts `BUILD_NUMBER=42 ./scripts/build-macos.sh`.
It defaults to three concurrent build jobs; set `BUILD_JOBS` to change that limit.
For existing Windows or Linux builds, package with
`python scripts/package-windows.py --build-dir build-win`
or `python3 scripts/package-linux.py --build-dir build-dsp`.
Both use the same versioned layout and named manifests/checksums. Run
`python3 scripts/test-distribution.py` to check packaging without rebuilding DSP.

### Release workflow

[`.github/workflows/release.yml`](../../.github/workflows/release.yml) builds,
validates and packages the three platforms from one commit:

- **macOS universal** (macos-15): `scripts/build-macos.sh` with
  `BUILD_UNIVERSAL=ON` builds VST3, AU, CLAP and Standalone and runs the whole test suite,
  `scripts/validate-plugins.sh` runs pluginval on the VST3, clap-validator on
  the CLAP and `auval -strict` on the AU, then
  `scripts/sign-and-package-macos.sh` makes the zip and the pkg. The runner is
  arm64, so the x86_64 slice is built but never run.
- **Windows x64** (windows-2025) and **Linux x64** (ubuntu-22.04): the VST3,
  the CLAP, the Standalone and the plug-in wrapper tests (`Acustra.PluginProcessor`,
  `Acustra.PluginTempoRelease`), pluginval on the VST3 and clap-validator on
  the CLAP, then
  `scripts/package-windows.py` or `scripts/package-linux.py`. The Windows job fails if either binary imports
  the Visual C++ runtime DLLs. The Linux job links libstdc++ and libgcc
  statically and fails if either binary asks for a glibc newer than Ubuntu
  22.04's 2.35.

[`.github/workflows/main-build.yml`](../../.github/workflows/main-build.yml) calls
the same workflow on every push to `main`, nightly at 03:00 UTC and on demand,
keeps the packages for 30 days, renders the ten demos, collects everything with
`BUILD.txt` and `SHA256SUMS` into one `acustra-main-build-<run>` artifact, and
commits the editor screenshot and the [Downloads](../../README.md#downloads) block when `main`
still points at the commit it built. CLAP is built, packaged and checked by
clap-validator on every platform.

`scripts/validate-plugins.sh` is the portfolio's shared copy: it downloads
pluginval v1.0.4 and clap-validator 0.3.2, checks them against pinned SHA-256
sums and runs pluginval at strictness level 10 with the fixed seed
0x41435354. Build numbers are the workflow's run number,
as in CI, and each platform's package set is kept as a workflow artifact for
seven days (30 for a main build).

The workflow runs on a pull request that changes it, `scripts/`, `cmake/` or
`CMakeLists.txt`, never signing there; on a manual run of any branch, which
signs on macOS when the secrets exist and makes no release; and on a pushed
`v*` tag. After a tag's three jobs pass, a last job checks that every manifest
names the tag's version and the tagged commit built from a clean tree, writes
one `SHA256SUMS` over all the files, and attaches them to a **draft** GitHub
release whose notes state each platform's signing and what was validated. To
draft a release of a version, confirm that `project(Acustra VERSION ...)` in
`CMakeLists.txt` names it and that CI passed on the commit, then tag that
commit:

```sh
git tag -a v<version> -m "Acustra <version>" <commit>
git push origin v<version>
```

macOS signing runs only when its secrets exist (repository Settings, Secrets
and variables, Actions). With none set, nothing fails for it: the macOS bundles
are signed ad hoc, the pkg is unsigned and nothing is notarized. The workflow
has no Windows signing step: publicly trusted code-signing certificates issued
since June 2023 keep their private key in a hardware module (a token, an HSM or
a cloud signing service), not in an exportable .pfx, so that step depends on
the provider chosen.

| Secret | Use |
| --- | --- |
| `MACOS_CERTIFICATES_P12` | Base64 of a .p12 holding the Developer ID Application and Developer ID Installer identities and their keys; imported into a temporary keychain |
| `MACOS_CERTIFICATES_PASSWORD` | The .p12's password |
| `MACOS_APP_SIGN_IDENTITY` | The application identity's name, passed as `APP_SIGN_IDENTITY` |
| `MACOS_INSTALLER_SIGN_IDENTITY` | The installer identity's name, passed as `INSTALLER_SIGN_IDENTITY` |
| `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD` | Apple ID, team ID and app-specific password, saved as the notarytool profile passed as `NOTARY_PROFILE`; notarizing needs the certificates as well |

The owner's decisions before publishing a draft are still open, and a run
settles none of them:

- Whether and how to sign: an Apple Developer ID with notarization, and a
  Windows signing provider. Until then the packages are signed ad hoc or
  unsigned, as above.
- Qualification in real hosts. The workflow runs the wrapper tests, pluginval,
  clap-validator and auval; it opens the plug-ins in no DAW.
- A listening verdict on the version's sound changes.
- The JUCE licence route (see [Licensing](../../README.md#licensing)), the EULA, terms and
  privacy policy, and the price and purchase path.
