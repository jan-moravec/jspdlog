# Contributing

Thanks for your interest in contributing! This document is the developer reference for the project: how to build from source, run the test suite, the code-style rules, the pull-request workflow, and the release process.

## Table of contents

- [Prerequisites](#prerequisites)
- [Building](#building)
  - [Quick start](#quick-start)
  - [Available presets](#available-presets)
  - [Without presets](#without-presets)
  - [Build options](#build-options)
- [Running tests](#running-tests)
- [Sanitizers and coverage](#sanitizers-and-coverage)
  - [AddressSanitizer + UBSan](#addresssanitizer--ubsan)
  - [Coverage](#coverage)
- [Code style and pre-commit](#code-style-and-pre-commit)
- [Pull requests](#pull-requests)
- [Release process](#release-process)
- [Repository security (maintainers)](#repository-security-maintainers)

## Prerequisites

- A C++17 compiler (the CI matrix covers GCC 11+, Clang 15+, AppleClang and MSVC v143).
- CMake **3.25 or newer** if you want to use the [CMake presets](CMakePresets.json); the minimum to build at all is the 3.16 declared in [`CMakeLists.txt`](CMakeLists.txt).
- Ninja (preset builds use it).
- An internet connection on the first configure: spdlog v2 (and, for the test/example suite, Catch2 and optionally nlohmann/json) are pulled in via `FetchContent` unless a system package is already on `CMAKE_PREFIX_PATH`.
- For contributor-side checks: Python 3.10+ and `pip install pre-commit==4.2.0` (the same pin CI uses).

## Building

### Quick start

```sh
cmake --workflow --preset release
```

This is equivalent to `cmake --preset release && cmake --build --preset release && ctest --preset release` and is the recommended local round-trip.

### Available presets

| Preset             | Build type       | Notes                                                                                        |
| ------------------ | ---------------- | -------------------------------------------------------------------------------------------- |
| `release`          | `Release`        | Default. Ninja generator. Used by the workflow alias above.                                  |
| `debug`            | `Debug`          | Full assertions, no optimization.                                                            |
| `relwithdebinfo`   | `RelWithDebInfo` | Release optimisations + debug info; suitable for profiling.                                  |
| `clang-asan-ubsan` | `RelWithDebInfo` | Clang 18 + AddressSanitizer + UndefinedBehaviorSanitizer. CI uses this as `mode=asan-ubsan`. |
| `clang-coverage`   | `RelWithDebInfo` | Clang 18 + `-fprofile-instr-generate -fcoverage-mapping`. CI uses this as `mode=coverage`.   |

Each configure preset has a matching build, test and (for the major ones) workflow preset.

### Without presets

For toolchains that don't ship CMake 3.25 or for IDEs that don't speak presets:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with Visual Studio, replace the configure line with:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

### Build options

All options default to `ON` when jspdlog is the top-level CMake project and `OFF` when it's pulled in via `add_subdirectory` / `FetchContent`, so an embedding project gets a quiet build by default.

| Option                          | Description                                                           |
| ------------------------------- | --------------------------------------------------------------------- |
| `JSPDLOG_BUILD_TESTS`           | Build the Catch2 unit-test suite.                                     |
| `JSPDLOG_BUILD_EXAMPLES`        | Build the standalone example programs under `example/`.               |
| `JSPDLOG_INSTALL`               | Generate `install` rules for the header and CMake package config.     |
| `JSPDLOG_TEST_NLOHMANN_INTEROP` | Build the optional `raw_json` ↔ nlohmann/json interop test (fetched). |

## Running tests

```sh
ctest --preset release
```

The test suite is built with Catch2 v3 (fetched on first configure unless system Catch2 is available) and is registered with CTest via `catch_discover_tests`, so individual cases show up as separate CTest entries.

To skip the nlohmann/json interop test (e.g. when offline):

```sh
cmake -S . -B build -DJSPDLOG_TEST_NLOHMANN_INTEROP=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

The `test/install_smoke/` subdirectory is a separate standalone project — it `find_package`s an *installed* jspdlog and is exercised by the `install-smoke` CI job. To run it locally:

```sh
cmake --preset release -DJSPDLOG_BUILD_TESTS=OFF -DJSPDLOG_BUILD_EXAMPLES=OFF
cmake --build --preset release
cmake --install build/release --prefix "$PWD/_install"
cmake -S test/install_smoke -B build/install-smoke \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build build/install-smoke
./build/install-smoke/jspdlog_install_smoke
```

## Sanitizers and coverage

Both sanitizer and coverage presets pin Clang 18 (matching the CI `clang-ci` matrix) and turn off IPO/LTO — LTO breaks sanitizer stack traces and skews coverage line mapping.

### AddressSanitizer + UBSan

```sh
cmake --workflow --preset clang-asan-ubsan
```

The test preset exports `ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:abort_on_error=1`, so the first hit fails the test binary and CTest turns it into a job failure. Configure flags use `-fno-sanitize-recover=all` for the same reason.

### Coverage

```sh
cmake --workflow --preset clang-coverage
```

Each test binary writes its own `.profraw` under `build/clang-coverage/profiles/` via the preset's `LLVM_PROFILE_FILE=${sourceDir}/build/clang-coverage/profiles/%p-%m.profraw`. After the workflow finishes:

```sh
llvm-profdata-18 merge -sparse build/clang-coverage/profiles/*.profraw \
  -o build/clang-coverage/merged.profdata
llvm-cov-18 report \
  -instr-profile=build/clang-coverage/merged.profdata \
  -ignore-filename-regex='(_deps|build|test|example)/' \
  build/clang-coverage/test/jspdlog_tests \
  -object build/clang-coverage/test/jspdlog_nlohmann_interop_test
```

CI does the same merge / export and uploads the resulting lcov to Codecov via tokenless OIDC.

## Code style and pre-commit

The repository ships [`.clang-format`](.clang-format), [`.clang-tidy`](.clang-tidy) (plus a relaxed [`test/.clang-tidy`](test/.clang-tidy)), [`.gitattributes`](.gitattributes), [`.markdownlint-cli2.yaml`](.markdownlint-cli2.yaml) and [`.pre-commit-config.yaml`](.pre-commit-config.yaml). A few conventions worth calling out:

- **snake_case API.** Every user-facing identifier — types, functions, members, locals — is `lower_case`. This mirrors `spdlog`'s API and is enforced by `.clang-tidy`'s `readability-identifier-naming` block. Template parameters keep `CamelCase` so they stand out. Private members carry a trailing underscore (Google-style) to avoid collisions with same-named getters/parameters.
- **One JSON object per line.** That is the entire point of the library; the pinned spdlog pattern lives in `jspdlog::detail::pattern()` in [`include/jspdlog/jspdlog.h`](include/jspdlog/jspdlog.h) and there is intentionally no public API to override it.
- **No new third-party runtime dependencies.** spdlog (and {fmt} transitively) is the only one. nlohmann/json is fetched only when the optional interop test or example is built; never linked into the library target.

To run every hook locally before pushing:

```sh
pip install pre-commit==4.2.0
pre-commit install
pre-commit run --all-files
```

The `pre-commit` job in CI runs the same hooks with the same versions; if it passes locally it will pass there.

## Pull requests

- Open the PR against `main`. Squash-merge is the project default; the merge commit message is whatever the PR title says, so write the title as if it were a commit subject.
- The PR must be green: every job under the [`ci` workflow](.github/workflows/ci.yml) — `pre-commit`, the `clang-ci` matrix (asan-ubsan + coverage), the cross-platform `build-and-test` matrix, and `install-smoke` — must succeed.
- Keep changes focused. A reformat of unrelated lines, a dependency bump and a behaviour change should be three PRs, not one.
- If your change affects the public header, add a `CHANGELOG.md` entry under an `## [Unreleased]` heading (creating it if absent). The release step turns that section into the release notes.

## Release process

jspdlog is header-only, so there are no binary artifacts to ship. A release is a tag + a GitHub Release entry.

1. Land all changes on `main` and wait for CI to be green.
1. Decide the new version. The project follows [SemVer](https://semver.org/): patch for bug fixes, minor for additive API, major for breaking changes.
1. Update the version in [`CMakeLists.txt`](CMakeLists.txt) (`project(jspdlog VERSION X.Y.Z …)`).
1. Move the `## [Unreleased]` section in [`CHANGELOG.md`](CHANGELOG.md) to a new `## [X.Y.Z] - YYYY-MM-DD` heading.
1. Open a "release: vX.Y.Z" PR, get it merged.
1. Tag the merge commit: `git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z`.
1. On GitHub: **Releases → Draft a new release**. Pick the tag, use "vX.Y.Z" as the title, paste the relevant `CHANGELOG.md` section into the body. Publish.

## Repository security (maintainers)

- Branch protection on `main`: require a green CI run, dismiss stale reviews on new commits, require CODEOWNERS review, and disallow force-pushes.
- Dependabot is configured in [`.github/dependabot.yml`](.github/dependabot.yml) to open grouped weekly PRs for GitHub Actions versions. Review each grouped PR and squash-merge once CI is green.
- For undisclosed security vulnerabilities, follow the disclosure process in [`SECURITY.md`](SECURITY.md). Do not file public issues with proof-of-concept exploits.
