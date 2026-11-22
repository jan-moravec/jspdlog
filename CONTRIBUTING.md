# Contributing

This is the developer reference for jspdlog: building, testing, code style, pull requests and releases. AI agents should
also read [AGENTS.md](AGENTS.md), the short binding summary of the build and comment rules.

## Table of contents

- [Prerequisites](#prerequisites)
- [Building](#building)
  - [Quick start](#quick-start)
  - [Available presets](#available-presets)
  - [Without presets](#without-presets)
  - [Build options](#build-options)
- [Running tests](#running-tests)
- [Sanitizers and coverage](#sanitizers-and-coverage)
- [Code style and pre-commit](#code-style-and-pre-commit)
- [Pull requests](#pull-requests)
- [Release process](#release-process)
- [Repository security (maintainers)](#repository-security-maintainers)

## Prerequisites

- A C++17 compiler. CI covers GCC 11 and 13, Clang 15, 18 and 23, AppleClang and MSVC 2022.
- CMake 3.25 or newer for the [presets](CMakePresets.json); the minimum to build at all is 3.16.
- Ninja, used by every preset. On Windows, run `.\scripts\Enter-DevShell.ps1` in the PowerShell session first, so Ninja
  finds the MSVC compiler.
- An internet connection on the first configure: spdlog, Catch2 and nlohmann/json are fetched with `FetchContent` unless
  an installed package is found. spdlog defaults to a pinned v2.x commit; pass `-DJSPDLOG_SPDLOG_GIT_TAG=v1.17.0` (or any
  tag from 1.12 on) to build against v1.x.
- For the checks: Python 3.10+ and `pip install pre-commit==4.7.0`, the version CI uses.

## Building

### Quick start

```sh
cmake --workflow --preset release
```

This configures, builds and runs the tests. On Windows:

```powershell
.\scripts\Enter-DevShell.ps1
cmake --workflow --preset release
```

### Available presets

| Preset             | Build type       | Notes                                                      |
| ------------------ | ---------------- | ---------------------------------------------------------- |
| `release`          | `Release`        | Default.                                                   |
| `debug`            | `Debug`          | Assertions on, no optimization.                            |
| `relwithdebinfo`   | `RelWithDebInfo` | Optimized with debug info, for profiling.                  |
| `clang-asan-ubsan` | `RelWithDebInfo` | Clang 23, AddressSanitizer and UndefinedBehaviorSanitizer. |
| `clang-tsan`       | `RelWithDebInfo` | Clang 23, ThreadSanitizer.                                 |
| `clang-coverage`   | `RelWithDebInfo` | Clang 23, source-based coverage.                           |

Every configure preset has matching build, test and workflow presets. All presets treat warnings in the tests and
examples as errors (`JSPDLOG_WARNINGS_AS_ERRORS=ON`).

### Without presets

For IDEs or toolchains without preset support:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

With the Visual Studio generator, add `-G "Visual Studio 17 2022" -A x64` to the first line and `--config Release` /
`-C Release` to the other two.

### Build options

`JSPDLOG_BUILD_TESTS`, `JSPDLOG_BUILD_EXAMPLES` and `JSPDLOG_INSTALL` default to `ON` only when jspdlog is the top-level
project, so an embedding project gets a quiet build.

| Option                          | Description                                                                        |
| ------------------------------- | ---------------------------------------------------------------------------------- |
| `JSPDLOG_BUILD_TESTS`           | Build the Catch2 tests.                                                            |
| `JSPDLOG_BUILD_EXAMPLES`        | Build the programs under `example/`.                                               |
| `JSPDLOG_INSTALL`               | Generate install rules for the header and the CMake package.                       |
| `JSPDLOG_TEST_NLOHMANN_INTEROP` | Build the `raw_json` / nlohmann/json interop test. Default `ON`.                   |
| `JSPDLOG_WARNINGS_AS_ERRORS`    | Treat warnings in the tests and examples as errors. Default `OFF`; presets set it. |
| `JSPDLOG_SPDLOG_GIT_TAG`        | spdlog tag or commit to fetch when no installed spdlog is found.                   |

The tests and examples compile with a strict warning set ([cmake/CompilerWarnings.cmake](cmake/CompilerWarnings.cmake)):
`/W4 /permissive-` on MSVC, and `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast` and
more on GCC and Clang. Users compile the header with their own flags, so it must stay clean under this set. Fetched
dependencies are marked `SYSTEM`, so their warnings don't count.

## Running tests

```sh
cmake --build --preset release
ctest --preset release
ctest --preset release -R with_properties   # by test name
```

The tests use Catch2 v3 and are registered with CTest per test case.

`test/install_smoke/` is a separate project that uses an *installed* jspdlog through `find_package`. CI runs it in the
`install-smoke` job. Locally:

```sh
cmake --preset release -DJSPDLOG_BUILD_TESTS=OFF -DJSPDLOG_BUILD_EXAMPLES=OFF
cmake --build --preset release
cmake --install build/release --prefix "$PWD/_install"
cmake -S test/install_smoke -B build/install-smoke -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build build/install-smoke
./build/install-smoke/jspdlog_install_smoke
```

## Sanitizers and coverage

The Clang presets pin `clang-23` / `clang++-23` and target Linux (natively, under WSL, or in CI):

```sh
cmake --workflow --preset clang-asan-ubsan
cmake --workflow --preset clang-tsan
cmake --workflow --preset clang-coverage
scripts/ci/export-coverage.sh   # merges the profiles into coverage.lcov and prints a summary
```

The sanitizer test presets stop at the first finding, so CTest reports it as a failure. Coverage counts the header only;
dependencies, tests and examples are filtered out.

## Code style and pre-commit

- **Naming follows spdlog.** Types, functions, members and locals are `snake_case`; template parameters are `CamelCase`;
  private members end with an underscore. [.clang-tidy](.clang-tidy) enforces this.
- **Formatting** is [.clang-format](.clang-format) (Allman braces, 120 columns). Don't format by hand; let the hook do it.
- **Comments** in `include/` follow the Doxygen rules in [AGENTS.md](AGENTS.md): a `@brief` file comment above
  `#pragma once`, `/** */` on every public declaration, no backticks, no history. `scripts/check-docs.py` checks this.
- **One JSON object per line.** The spdlog pattern is built by `jspdlog::detail::make_json_pattern` and there is no
  public API to override it.
- **No new runtime dependencies.** spdlog, and {fmt} through it, is the only one. nlohmann/json is fetched only for the
  interop test and example.

Run every hook locally before pushing:

```sh
pip install pre-commit==4.7.0
pre-commit install
pre-commit run --all-files
```

The hooks are clang-format, gersemi (CMake), mdformat and markdownlint (Markdown), yamlfmt and yamllint (YAML), ruff
(Python), codespell, actionlint and zizmor (GitHub Actions), and the comment checker. CI runs the same hooks at the same
versions.

## Pull requests

- Open the PR against `main`. Squash-merge is the default, so write the PR title as a commit subject.
- Every job of the [CI workflow](.github/workflows/ci.yml) must pass: `pre-commit`, the `clang-ci` matrix (ASan + UBSan,
  TSan, coverage), the `build-and-test` matrix and `install-smoke`.
- Keep changes focused. A reformat, a dependency bump and a behavior change are three PRs.
- If the change affects users, add a [CHANGELOG.md](CHANGELOG.md) entry under `## [Unreleased]`. Until the first
  release the changelog stays empty, so skip this step.

## Release process

jspdlog is header-only; a release is a tag and a GitHub Release.

1. Land the changes on `main` with CI green.
1. Pick the version following [SemVer](https://semver.org/). While the version is 0.x, a minor version may break the API.
1. Update the version in [CMakeLists.txt](CMakeLists.txt) (`project(jspdlog VERSION X.Y.Z)`) and the
   `JSPDLOG_VERSION_MAJOR/MINOR/PATCH` macros in [include/jspdlog/jspdlog.h](include/jspdlog/jspdlog.h).
1. Rename `## [Unreleased]` in [CHANGELOG.md](CHANGELOG.md) to `## [X.Y.Z] - YYYY-MM-DD`. For the first release, write
   that section from scratch, summarizing what the release contains.
1. Merge a "Release vX.Y.Z" PR.
1. Tag the merge commit: `git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z`.
1. On GitHub, draft a release from the tag with the CHANGELOG section as the notes.

## Repository security (maintainers)

- Protect `main`: require green CI and a CODEOWNERS review, dismiss stale reviews, disallow force-pushes.
- Dependabot ([.github/dependabot.yml](.github/dependabot.yml)) opens one grouped weekly PR for GitHub Actions, waiting
  7 days after an upstream release. Review it and squash-merge once CI is green.
- The weekly [Scorecard workflow](.github/workflows/scorecard.yml) reports supply-chain findings under Security, Code
  scanning.
- Report vulnerabilities as described in [SECURITY.md](SECURITY.md), not in public issues.
