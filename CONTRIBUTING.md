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
- [Fuzzing](#fuzzing)
- [Benchmarks](#benchmarks)
- [Packaging](#packaging)
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
| `clang-fuzz`       | `RelWithDebInfo` | Clang 23, libFuzzer targets with ASan and UBSan.           |
| `clang-coverage`   | `RelWithDebInfo` | Clang 23, source-based coverage.                           |
| `bench`            | `Release`        | The benchmark program only, no tests or examples.          |

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

| Option                           | Description                                                                               |
| -------------------------------- | ----------------------------------------------------------------------------------------- |
| `JSPDLOG_BUILD_TESTS`            | Build the Catch2 tests.                                                                   |
| `JSPDLOG_BUILD_EXAMPLES`         | Build the programs under `example/`.                                                      |
| `JSPDLOG_INSTALL`                | Generate install rules for the headers and the CMake package.                             |
| `JSPDLOG_TEST_WITH_NLOHMANN`     | Build the tests that need nlohmann/json: interop and fuzz replay. Default `ON`.           |
| `JSPDLOG_BUILD_FUZZERS`          | Build the libFuzzer targets (Clang only). Default `OFF`; the `clang-fuzz` preset sets it. |
| `JSPDLOG_BUILD_BENCH`            | Build the benchmark program. Default `OFF`; the `bench` preset sets it.                   |
| `JSPDLOG_WARNINGS_AS_ERRORS`     | Treat warnings in the tests and examples as errors. Default `OFF`; presets set it.        |
| `JSPDLOG_SPDLOG_GIT_TAG`         | spdlog tag or commit to fetch when no installed spdlog is found.                          |
| `JSPDLOG_UTF8_POLICY`            | Invalid UTF-8 in logged strings: `replace` (default), `escape` or `pass`.                 |
| `JSPDLOG_ESCAPE_LINE_SEPARATORS` | Write U+2028 and U+2029 as escapes. Default `OFF`.                                        |

The tests and examples compile with a strict warning set ([cmake/CompilerWarnings.cmake](cmake/CompilerWarnings.cmake)):
`/W4 /permissive-` on MSVC, and `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast` and
more on GCC and Clang. Users compile the header with their own flags, so it must stay clean under this set. Fetched
dependencies are marked `SYSTEM`, so their warnings don't count.

## Running tests

```sh
cmake --build --preset release
ctest --preset release
ctest --preset release -R with   # by test name
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

The sanitizer test presets stop at the first finding, so CTest reports it as a failure. Coverage counts the headers
only; dependencies, tests and examples are filtered out.

## Fuzzing

jspdlog promises that every line is one valid JSON object, whatever the input. The fuzz targets in
[test/fuzz](test/fuzz) test that with inputs nobody writes by hand. There are six libFuzzer targets:

| Target            | What it checks                                                                                                                                  |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `escape`          | The escaped string decodes to the input with each maximal invalid UTF-8 subpart replaced, for every input.                                      |
| `fields`          | Operations on `fields`, `with()` and per-call fields against a short reference model; a braced list gives the line of the same `fields` object. |
| `logger_line`     | Fuzzed logger name, header keys, time type, line terminator and message: one object per line. The message is also parsed as a `traceparent`.    |
| `foreign_payload` | Payloads logged straight on the `spdlog::logger`, with prefixes of the payload token: no injection.                                             |
| `numbers`         | Every integer width, `float`, `double` and `long double` round-trip; NaN and infinity are `null`.                                               |
| `value_types`     | Nested optionals, variants, ranges, maps, durations and time points against an independent model.                                               |

### Run a target

libFuzzer runs on Linux with Clang (natively, under WSL, or in CI):

```sh
cmake --preset clang-fuzz
cmake --build --preset clang-fuzz --target fuzz_escape
mkdir -p work/escape
build/clang-fuzz/test/fuzz/fuzz_escape work/escape test/fuzz/corpus/escape -max_total_time=60
```

The working corpus (`work/`) is yours; the checked-in corpus is read-only input. A crash writes `crash-*` to the current
directory (or to `-artifact_prefix`) and the run stops. `cmake --workflow --preset clang-fuzz` builds everything and
replays the saved inputs.

### Saved inputs and replay

Every file in `test/fuzz/corpus/<target>/` (small seeds) and `test/fuzz/regressions/<target>/` (minimized crashing inputs) is
also a CTest case on every compiler of the CI matrix, including MSVC and GCC, labelled `fuzz-replay`:

```sh
ctest --preset release -L fuzz-replay
```

Both folders are byte-exact. `.gitattributes` marks them `-text` and pre-commit skips them, so never run a formatter or
an editor over them.

### Rules

- A fuzz finding is fixed with its minimized input added to `test/fuzz/regressions/<target>/`, so it replays in every
  build. Minimize it with `fuzz_<target> -minimize_crash=1 -runs=10000 crash-<hash>`.
- Targets are deterministic and fast: no sleeps, no files, no network, no threads.
- The oracle must not give false alarms. A failing oracle is fixed before merging, not ignored. With the default UTF-8
  policy every line is valid UTF-8, so targets parse every line, and a model applies `fuzz::written_text` to each key and
  string it stores. Only a build with `JSPDLOG_UTF8_POLICY=pass` copies invalid UTF-8, which nlohmann/json rejects, so
  there the targets check only the shape of such a line. Fuzzing runs with the default policy; the replay tests of the
  escape and pass test executables cover the others.
- Keep each corpus under about 50 small files. Shrink one with `fuzz_<target> -merge=1 new_corpus work/<target> test/fuzz/corpus/<target>` and copy the result back.
- Change `escape`, the formatter or `fields`? Run `clang-fuzz` for 60 seconds per target before opening the PR.

### In CI

The `fuzz` job fuzzes each target for 60 seconds on every pull request and uploads the reproducer on a crash. The
`fuzz-long` workflow takes the time per target as an input (default 1800 seconds) and also runs weekly, opening an issue
on a crash.

## Benchmarks

```sh
cmake --preset bench
cmake --build --preset bench
build/bench/bench/jspdlog_bench [iterations]
```

The program prints a Markdown table with the cost of one log call per case, single-threaded, into a sink that formats
and discards. The `bench` job of the CI workflow prints it in the job summary for GCC 13 and Clang 23. It never fails
on numbers, because CI machines are too noisy for thresholds: compare cases within one run, not across runs.

## Packaging

The repository holds a vcpkg port ([packaging/vcpkg/ports/jspdlog](packaging/vcpkg/ports/jspdlog)) and a Conan recipe
([conanfile.py](conanfile.py), with its test package in
[packaging/conan/test_package](packaging/conan/test_package)). The `vcpkg` and `conan` jobs of the CI workflow test
both against the checkout on Linux and Windows. Both packages carry the default text policy, and neither defines
`JSPDLOG_ACTIVE_LEVEL`, which belongs to the consumer.

The version is also stated in the port's `vcpkg.json` and in `conanfile.py`; pre-commit checks that they match
`project(VERSION)` ([scripts/ci/check-version.py](scripts/ci/check-version.py)). A new optional header needs no change
here, because the CMake install rules and the recipe copy every header under `include/`.

### vcpkg port

The port downloads the tag `v<version>`, which does not exist before the release.
[scripts/ci/vcpkg-local-port.sh](scripts/ci/vcpkg-local-port.sh) writes a copy that builds a checkout instead. With a
[vcpkg](https://github.com/microsoft/vcpkg) clone in `$VCPKG_ROOT`:

```sh
scripts/ci/vcpkg-local-port.sh "$PWD" "$PWD/build/overlay"
"$VCPKG_ROOT/vcpkg" install jspdlog --overlay-ports="$PWD/build/overlay" --enforce-port-checks
cmake -S test/install_smoke -B build/vcpkg-smoke -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/vcpkg-smoke
./build/vcpkg-smoke/jspdlog_install_smoke
```

On Windows, run these in Git Bash started from a developer prompt, so Ninja finds the MSVC compiler. vcpkg reuses an
installed package: run `vcpkg remove jspdlog` before installing a changed checkout. Format `vcpkg.json` with
`vcpkg format-manifest`.

### Conan recipe

With Python 3.10+, in a virtual environment:

```sh
pip install -r packaging/conan/requirements.txt
conan profile detect
conan create . --test-folder packaging/conan/test_package --build=missing -s compiler.cppstd=17
```

`compiler.cppstd` is given because the profile that Conan detects for MSVC says C++14.
[packaging/conan/requirements.txt](packaging/conan/requirements.txt) pins the Conan version, which Dependabot updates.

### spdlog v2

spdlog v2 has no tagged release, so `JSPDLOG_SPDLOG_GIT_TAG` defaults to a commit of its `v2.x` branch. The weekly
[spdlog-v2-head workflow](.github/workflows/spdlog-v2-head.yml) builds and tests against the head of that branch and, on
failure, opens an issue with the `spdlog-v2` label or comments on the open one. The next passing run on `main` closes
that issue; runs on other branches only open or comment on it. When spdlog tags v2.0.0, move the
default in [CMakeLists.txt](CMakeLists.txt) and its comment to the tag, and update the README, this file and the
ROADMAP.

### Submitting the packages upstream

After a release is tagged, each registry takes the package through a pull request in its own repository.

**vcpkg** ([microsoft/vcpkg](https://github.com/microsoft/vcpkg)). New ports have to meet the
[maintainer guide](https://learn.microsoft.com/vcpkg/contributing/maintainer-guide), which among other things asks for
six months of public development or a release that old.

1. Copy `packaging/vcpkg/ports/jspdlog` to `ports/jspdlog` of a vcpkg fork.
1. Run `vcpkg install jspdlog --overlay-ports=ports/jspdlog`. It fails and prints the SHA512 of the release archive;
   replace `SHA512 0` with it, also in this repository, and install again.
1. Run `vcpkg format-manifest ports/jspdlog/vcpkg.json` and commit. Then run `vcpkg x-add-version jspdlog` and commit
   the version files it writes.
1. Open the pull request, as a draft until its checks pass.

**ConanCenter** ([conan-io/conan-center-index](https://github.com/conan-io/conan-center-index)). A new library goes
directly into a pull request; check the
[inclusion criteria](https://github.com/conan-io/conan-center-index/blob/master/docs/adding_packages/inclusion_criteria_for_new_recipes.md)
first and sign the contributor license agreement the pull request asks for.

1. Start from the
   [header-only template](https://github.com/conan-io/conan-center-index/tree/master/docs/package_templates/header_only)
   in `recipes/jspdlog/`: `config.yml` naming the version and the folder `all`, and in `all/` the files `conanfile.py`,
   `conandata.yml` and `test_package/`.
1. `conandata.yml` gives the URL `https://github.com/jan-moravec/jspdlog/archive/refs/tags/v<version>.tar.gz` and its
   sha256.
1. The upstream recipe differs from the one here: no `version` attribute, `url` is conan-center-index, a
   `source()` with `get(self, **self.conan_data["sources"][self.version], strip_root=True)` instead of
   `exports_sources`, `basic_layout(self, src_folder="src")`, and a fixed spdlog version such as `spdlog/1.17.0`,
   because ConanCenter allows version ranges only for a few libraries.
1. Copy [packaging/conan/test_package](packaging/conan/test_package) and test with
   `conan create recipes/jspdlog/all --version=<version> --build=missing`.

Once a registry has merged the package, add its install instructions to the README.

## Code style and pre-commit

- **Naming follows spdlog.** Types, functions, members and locals are `snake_case`; template parameters are `CamelCase`;
  private members end with an underscore. [.clang-tidy](.clang-tidy) enforces this.
- **Formatting** is [.clang-format](.clang-format) (Allman braces, 120 columns). Don't format by hand; let the hook do it.
- **Comments** in `include/` follow the Doxygen rules in [AGENTS.md](AGENTS.md): a `@brief` file comment above
  `#pragma once`, `/** */` on every public declaration, no backticks, no history. `scripts/check-docs.py` checks this.
- **One JSON object per line.** The spdlog pattern is built by `jspdlog::detail::make_json_pattern` and there is no
  public API to override it.
- **No new runtime dependencies.** spdlog, and {fmt} through it, is the only one of the core header. What needs more
  goes into an optional header next to it: `jspdlog/nlohmann.h` includes nlohmann/json, which the build fetches only
  for the tests and examples.

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
  TSan, coverage), `fuzz`, the `build-and-test` matrix, `install-smoke`, `vcpkg`, `conan` and `bench`.
- Keep changes focused. A reformat, a dependency bump and a behavior change are three PRs.
- If the change affects users, add a [CHANGELOG.md](CHANGELOG.md) entry under `## [Unreleased]`.

## Release process

jspdlog is header-only; a release is a tag and a GitHub Release.

1. Land the changes on `main` with CI green.
1. Pick the version following [SemVer](https://semver.org/). While the version is 0.x, a minor version may break the API.
1. Update the version in [CMakeLists.txt](CMakeLists.txt) (`project(jspdlog VERSION X.Y.Z)`), the
   `JSPDLOG_VERSION_MAJOR/MINOR/PATCH` macros in [include/jspdlog/jspdlog.h](include/jspdlog/jspdlog.h),
   `version-semver` in the port's [vcpkg.json](packaging/vcpkg/ports/jspdlog/vcpkg.json) and `version` in
   [conanfile.py](conanfile.py). Set the `GIT_TAG` of the README's FetchContent example to `vX.Y.Z`.
1. Rename `## [Unreleased]` in [CHANGELOG.md](CHANGELOG.md) to `## [X.Y.Z] - YYYY-MM-DD`, add a new empty
   `## [Unreleased]` above it, and update the comparison links at the bottom.
1. Merge a "Release vX.Y.Z" PR.
1. Tag the merge commit: `git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z`. The
   [release workflow](.github/workflows/release.yml) checks that the tag names the version and drafts a GitHub Release
   with the CHANGELOG section as its notes.
1. Publish the draft release the workflow created.

## Repository security (maintainers)

- Protect `main`: require green CI and a CODEOWNERS review, dismiss stale reviews, disallow force-pushes.
- Dependabot ([.github/dependabot.yml](.github/dependabot.yml)) opens one grouped weekly PR for GitHub Actions, and a PR
  for the pinned Conan version, waiting 7 days after an upstream release. Review it and squash-merge once CI is green.
- The weekly [Scorecard workflow](.github/workflows/scorecard.yml) reports supply-chain findings under Security, Code
  scanning.
- Report vulnerabilities as described in [SECURITY.md](SECURITY.md), not in public issues.
