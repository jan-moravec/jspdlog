# AGENTS.md: build, test and comment policy for AI agents

This file is the binding, agent-facing summary. Read it before changing code or running a build. Humans should use
[CONTRIBUTING.md](CONTRIBUTING.md) for the full development guide.

## Comments and Doxygen

Every comment in `include/` follows these rules. Match the example below.

1. The header starts with a file comment, above `#pragma once`. It has `@file`, a one-sentence `@brief`, then a body
   saying what the library does, how it works at a high level and what it guarantees.
1. Every public declaration has a `/** */` comment with a `@brief`. Internal helpers in `detail` and private members get
   one when the reason for them is not obvious.
1. No backticks in comments. Write json_logger, not the name in backticks. Put longer code in `@code` / `@endcode`
   blocks.
1. Say why and how to use it. Do not restate the name: "Returns the size" on size() adds nothing.
1. Describe current behavior only. No history, no "previously", "no longer", "legacy", issue or PR numbers.
1. Keep it short. Use `@param` and `@return` only when they add information the name and type do not.
1. Let clang-format wrap the text at 120 columns. Write each paragraph as one long line, then run clang-format.
1. Every comment must be true. Check what the code does before writing about it.

`scripts/check-docs.py` checks rules 1 (file comment), 3 (backticks) and that no `///` comments are used. pre-commit runs
it.

```cpp
/**
 * @brief Typed key/value fields for a log line, serialized when they are inserted.
 *
 * Pass them per call, usually as a braced list, or bind them to a child logger with json_logger::with.
 * Values keep their JSON type, so numbers and booleans stay filterable as such.
 */
class fields
```

## Naming and style

- Naming follows spdlog: `snake_case` types, functions and members; `CamelCase` template parameters; private members end
  with an underscore. `.clang-tidy` enforces it.
- Formatting is the repository's `.clang-format`, applied by pre-commit. Do not reformat unrelated code.
- The library must stay a single header with no dependency beyond spdlog and {fmt}.
- Support spdlog v1.x (1.12 or newer) and v2.x. Put version differences behind `JSPDLOG_SPDLOG_V2`.

## Non-negotiable rules

- Run commands from the repository root.
- Use the CMake presets. Do not improvise with `cmake --build <dir>`, `ctest --test-dir ...` or direct test executables.
- On Windows, run `.\scripts\Enter-DevShell.ps1` in the same PowerShell session that runs CMake. Without it Ninja cannot
  find the MSVC compiler or the standard headers.
- Warnings are errors in preset builds. Fix them in the header; do not silence them in `cmake/CompilerWarnings.cmake`.
- Do not commit, push or tag unless the user asked for it.

## Default validation

Windows (MSVC):

```powershell
.\scripts\Enter-DevShell.ps1
cmake --workflow --preset release
```

Linux and macOS:

```sh
cmake --workflow --preset release
```

The workflow configures, builds and runs the tests. Iterate with the stages you need:

```sh
cmake --preset release          # after CMake or preset changes
cmake --build --preset release
ctest --preset release
ctest --preset release -R json_options    # one Catch2 test case by name
```

## Validation by change type

- Documentation only: no build. Check that documented commands and preset names exist.
- Any header or CMake change: the `release` workflow.
- Code that differs between spdlog versions: also configure a second build with `-DJSPDLOG_SPDLOG_GIT_TAG=v1.12.0` (the
  oldest supported release), for example `cmake --preset debug -DJSPDLOG_SPDLOG_GIT_TAG=v1.12.0`, then build and test
  with the `debug` presets.
- Memory or undefined-behavior-sensitive changes: `clang-asan-ubsan` (Linux).
- Threading or async changes: `clang-tsan` (Linux).
- Every change: `pre-commit run --all-files`.

## Common traps

- The Clang presets pin `clang-23` and Linux flags. Do not run them natively on Windows.
- `FetchContent` downloads spdlog, Catch2 and nlohmann/json on the first configure, which needs network access.
- MSVC mis-stringifies raw string literals that contain backslashes inside Catch2 assertion macros. Put such expected
  values in a local variable first.
- A test failing once may be a real failure. Rerun it once with the same preset and `-V` before drawing conclusions; do
  not disable tests to make a run green.
