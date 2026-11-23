# jspdlog roadmap

Status: draft for discussion, 2026-11-21.

## 1. What jspdlog is

**Structured JSON logging for C++ with explicit, composable context, built
on spdlog's sinks.** In other ecosystems this design is standard: Go's
`slog` (`logger.With(...)` plus a JSON handler), zerolog, Serilog's
`ForContext`. Among maintained C++ libraries nothing combines:

| Requirement                  | Why it rules out most C++ loggers                                                                          |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------- |
| No macros needed             | glog, Abseil, plog, Boost.Log, log4cxx and Quill's JSON mode are macro-first.                              |
| No global singletons         | Registries, global backends and logger hierarchies (Quill, Boost.Log core, log4cxx).                       |
| JSON lines with typed fields | spdlog, glog and plog are text-only.                                                                       |
| Inner code adds context      | Usually done with thread-local MDC, which is hidden global state and breaks across threads and coroutines. |

spdlog already meets the first two when you construct `spdlog::logger`
yourself and avoid its registry, and it provides the sinks (rotating, daily,
syslog, Windows event log, async). jspdlog adds the JSON encoding and the
context model on top.

## 2. Design principles

These decide every feature below.

1. **No macros required.** Every feature, including source location, works
   through ordinary function calls. Macros may exist only as optional sugar.
1. **No global state.** jspdlog never uses spdlog's registry, default logger
   or global thread pool, and has no mutable static variables of its own. Everything
   hangs off a logger object that the application owns.
1. **Context is explicit.** Inner code receives a logger and derives a child
   with more fields. There is no thread-local MDC.
1. **Every line is valid JSON**, whatever is passed in, including messages
   that reach the underlying spdlog logger without going through jspdlog.
1. **Nothing happens when a level is off.** Level checks come before any
   formatting or serialization.
1. **Single header, spdlog v1.x (>= 1.12) and v2.x, C++17 baseline.** C++20
   may improve ergonomics but must not be required.
1. **No JSON library dependency.** `raw_json` is the bridge to one.

## 3. Where we are (0.2.0)

The header ([include/jspdlog/jspdlog.h](include/jspdlog/jspdlog.h))
provides `json_logger` (five call shapes per level plus `log(level, ...)`),
child loggers with `with()`, `adopt`, `json_options` (rename or omit header
fields, message key, call site, UTC, eol), typed `jspdlog::fields` with
nesting, and `raw_json`. It works with spdlog 1.12 to 1.17 and v2.x from one
header; the only differences it handles are listed in section 8.

A `json_formatter` renders every line from the spdlog message. `json_logger`
passes it a payload with a process-wide token, the fields and the escaped
message; any other payload is escaped into the message key, so every line is
valid JSON whoever logged it. `install_json_formatter` extends that to
loggers and sinks jspdlog does not wrap. Structural limits that remain:

- The options belong to each sink's formatter, so loggers sharing a sink share
  the options of the one created last.
- Per-call fields are serialized when the braced list is built, before the
  level check, so principle 5 holds only for the message. Lazy values
  (milestone 0.3.0) close this gap.

## 4. Architecture decision: custom `spdlog::formatter` (proposed for 0.2)

The proof of concept on the
[poc/formatter branch](https://github.com/jan-moravec/jspdlog/tree/poc/formatter/poc/formatter)
implements the proposed design. Evidence:

| Question                                     | Result                                                                                                                                                                                      |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Valid JSON for messages that bypass jspdlog? | Yes. Plain payloads become an escaped `"message"`. The current header produces a broken line in the same test.                                                                              |
| Injection by a crafted message?              | No. In the PoC, jspdlog payloads carry a random 16-byte token per logger, shared only with that logger's formatter; anything else is escaped. 0.2 uses one process-wide token (decision 6). |
| Async (v1 `async_logger`, v2 `async_sink`)?  | Yes. Token and fields travel in the copied payload.                                                                                                                                         |
| Source location without macros?              | Yes. `__builtin_FILE/LINE/FUNCTION` as default arguments in C++17 (GCC, Clang, MSVC); consteval format-string wrapper in C++20. Tested on MSVC with C++17 and C++20.                        |
| spdlog v1 and v2 from one header?            | Yes. Tested with 1.12.0 (C++20), 1.17.0 and v2.x (C++17).                                                                                                                                   |
| Performance                                  | Equal or slightly better (see below).                                                                                                                                                       |

Benchmark: Windows, MSVC Release, single thread. The sink formats every
message and discards it, so the numbers are ns/message for serialization plus
formatting, with no I/O.

| Path                                             | spdlog 1.17 | spdlog v2.x |
| ------------------------------------------------ | ----------: | ----------: |
| spdlog plain-text pattern (reference)            |         220 |         228 |
| current header, message only                     |         219 |         211 |
| current header, bound + 2 per-call props         |         416 |         386 |
| formatter PoC, message only                      |         189 |         219 |
| formatter PoC, bound + 2 per-call props          |         390 |         379 |
| formatter PoC with source location, message only |         299 |         315 |

Source location currently costs about 100 ns, mostly from escaping the full
path on every line. Planned fix: emit the base name and cache the escaped
form per `filename` pointer.

**Decision:** adopt the formatter design for 0.2. The public API keeps its
shape (`json_logger`, a typed field set, `raw_json`, one options struct), with
the renames listed in section 9.

## 5. Milestones

Each milestone is tagged `v0.N.0` when it is complete. The API may break
between minor versions until `v1.0.0`.

### 0.1.0: baseline

- [x] API with construction-time `json_options`.
- [x] spdlog v1.x (>= 1.12) and v2.x support, CI legs for both.
- [x] UTC timestamps end in `Z`.
- [x] Proof of concept for the formatter design (branch `poc/formatter`).
- [x] Logger name read from each message, so shared sinks and
  `spdlog::logger::clone()` keep the right names; loggers with different
  options can share a sink and still write valid JSON.
- [x] Logging calls don't throw: formatting errors go to spdlog's error
  handler.
- [x] README section "Working with spdlog": sinks, error handler, registry,
  default logger and compile-time settings.
- [x] Strict warnings (warnings as errors) for tests and examples on MSVC, GCC
  and Clang.
- [x] Doxygen comments in the header, checked in pre-commit.
- [x] Tooling: presets with TSan, Clang 23 CI, zizmor, yamllint, ruff,
  Scorecard, `AGENTS.md`.
- [x] CI green on the full matrix.

### 0.2.0: formatter core (breaking)

- [x] `json_formatter : spdlog::formatter` renders the header from `log_msg`;
  the pinned pattern goes away.
- [x] One immutable, process-wide payload token, generated once at startup.
  Payloads without it are escaped into the message key. Wrapping the same
  spdlog logger twice keeps working.
- [x] `install_json_formatter(spdlog::logger&)` gives JSON output for loggers
  jspdlog doesn't wrap. Documented pattern: install it on the default (v1) or
  global (v2) logger so that third-party spdlog output, `dup_filter_sink`'s
  "Skipped" line and v1 backtrace markers become valid JSON too. A sink
  overload covers sinks added later.
- [x] Sinks that write the raw message (`callback_sink`, `mongo_sink`,
  `kafka_sink`, syslog and systemd without formatting): a documented route
  per sink, no stripping helper (decision 8).
- [x] Source location without macros (C++17 builtins as default arguments).
  The call site is always captured; it is written only when `source_key` is
  set (**off by default**), as `"source":{"file","line","function"}` with the
  file's base name.
- [x] `message_key` option.
- [x] Renames: `json_properties` → `jspdlog::fields`, `with_properties()` →
  `with()`, `properties()` → `context()`.
- [x] `fields` keep **insertion order**, stored flat (vector of pre-serialized
  `,"k":v` entries); a replaced key keeps its position. Lines read outer
  context, inner context, per-call fields, message. *Breaking: today's output
  is sorted by key.*
- [x] Child loggers share an immutable, pre-serialized context through a
  `shared_ptr` instead of copying it.
- [x] Single options struct for the formatter and the logger.
- [x] Fuzzing infrastructure and targets 1 to 4 and 6 (section 6), with a CI
  fuzz job, plus a benchmark program that CI runs and reports without gating.
- [x] `info(value)` accepts runtime strings only (`std::string`,
  `std::string_view`, `const char*`, writable `char` buffers); string literals still go to the
  compile-time-checked format overload. Typed values belong in fields.
- [x] **Duplicate keys are the caller's responsibility.** Debug builds check that
  no field key equals an enabled header key, the message key or the source
  key, via an overridable `JSPDLOG_ASSERT` (defaults to `assert`). Bound
  context is checked once in `with()`, per-call fields on each call. Release
  builds do no checks.

### 0.3.0: value types

Field keys are always strings; field values should cover as much of JSON as
possible, so numbers, booleans and nested data stay filterable as such
(`"context":123`, not `"context":"123"`).

- Already supported: strings, `bool`, all integers, floating point (NaN/Inf →
  `null`), `nullptr`, nested `fields`, `raw_json`.
- `std::optional<T>` (empty → `null`).
- Enums: name if the enum is {fmt}-formattable, otherwise the underlying
  number.
- `std::chrono` durations (number plus unit in the key is the caller's
  choice; default milliseconds as a number) and time points (RFC 3339
  string).
- Ranges (`std::vector`, `std::array`, `std::span`, ...) → JSON arrays;
  string-keyed maps → JSON objects; recursive, so `std::vector<fields>`
  works.
- `std::variant` → whichever alternative is active.
- Customization point for user types, e.g. ADL
  `void jspdlog_serialize(jspdlog::json_writer&, const T&)`, with no JSON
  library needed.
- Lazy values: a callable evaluated only when the level is enabled.
- Fuzz target for nested value types (section 6, target 5).

### 0.4.0: correctness and schema

- **UTF-8.** Replace invalid sequences with U+FFFD (default), or pass them
  through or `\u`-escape them, as an option. Optionally escape U+2028/2029.
- **Level names.** Mapping table (`warning` vs `warn`, uppercase) plus an
  optional numeric severity field.
- **Timestamps.** RFC 3339 in ms (default), µs or ns precision, or epoch
  milliseconds.
- **Schema presets** for common ingestion formats: Elastic ECS (`@timestamp`,
  `log.level`), OpenTelemetry log data model, GCP (`severity`), Datadog.
  Presets are just pre-filled options structs.
- **Exceptions.** `logger.error(ex, "...")` emits
  `"error":{"type":...,"message":...}`.

### 0.5.0: ergonomics

- Keys known at compile time are escaped at compile time.
- Error-handler helper that reports sink failures as JSON without deadlocking
  spdlog's non-recursive error mutex.
- `SPDLOG_USE_STD_FORMAT` builds (needs a `std::formattable` fallback for the
  value-form trait).

### 0.6.0: integration and packaging

- Factory helpers that use no globals: stdout JSON lines, rotating file,
  async with an owned thread pool (v1) or `async_sink` (v2).
- Trace context: helpers to bind `trace_id` / `span_id` (OpenTelemetry) as
  child-logger properties.
- Optional integration headers, never required by the core:
  `jspdlog/nlohmann.h` (`nlohmann::json` as a value type), maybe
  `jspdlog/glaze.h`.
- vcpkg and Conan ports; track spdlog v2's first tagged release.

### 1.0.0: stable

- API freeze and semantic versioning.
- Long fuzzing runs on a schedule and, if accepted, OSS-Fuzz (section 6).
- Published benchmarks, including multi-threaded and async.
- Documentation site with a migration guide from 0.x.

## 6. Fuzzing

jspdlog's main promise is that every line is one valid JSON object, whatever
the input. Fuzzing tests exactly that promise, with inputs nobody would write by
hand: invalid UTF-8, control bytes, quotes and backslashes in keys and logger
names, spdlog pattern flags, extreme numbers, and payloads that imitate
jspdlog's own fields.

### Setup

How to build, run and extend the targets, and the rules for saved inputs, is in
[CONTRIBUTING.md](CONTRIBUTING.md#fuzzing). In short: libFuzzer with ASan and
UBSan from the `clang-fuzz` preset, every saved input replayed as a CTest case
with the label `fuzz-replay` on every compiler, and an oracle that parses each
line with nlohmann/json.

### Targets

| #   | Target                  | Input                                                                                                        | Checks                                                                                                                     |
| --- | ----------------------- | ------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| 1   | `escape`                | Raw bytes                                                                                                    | The escaped string parses back to the same bytes (for valid UTF-8; from 0.4, invalid UTF-8 follows the configured policy). |
| 2   | `fields`                | A byte-coded sequence of operations: insert typed values under fuzzed keys, merge, `with()`, per-call fields | The line parses; keys, values, insertion order and override rules match a simple reference model.                          |
| 3   | `logger_line`           | Fuzzed logger name, header keys, time type, eol and message                                                  | Every line is one object; header fields are present with the configured keys; the message round-trips.                     |
| 4   | `foreign_payload` (0.2) | Raw payloads logged directly on the underlying `spdlog::logger`, including prefixes of the payload token     | The line is valid JSON and the payload appears only inside the message; no injected fields.                                |
| 5   | `value_types` (0.3)     | `FuzzedDataProvider`-built nested optionals, variants, ranges and maps                                       | The parsed value equals the serialized structure.                                                                          |
| 6   | `numbers`               | Bit patterns for every integer width, `float`, `double`, `long double`                                       | Integers round-trip exactly; floats round-trip; NaN and infinity become `null`.                                            |

### CI

- **Pull requests:** a `fuzz` job builds the `clang-fuzz` preset and runs each
  target for 60 seconds. A crash fails the job and uploads the reproducer as an
  artifact.
- **Manual runs** take the run time per target as an input, for deeper runs.
- **Weekly** scheduled run of 30 minutes per target, opening an issue on a
  crash.
- **1.0:** apply to OSS-Fuzz for continuous fuzzing.

### Rules

- A fuzz finding is fixed with its minimized input added to
  `test/fuzz/regressions/`, so it replays in every build.
- Targets must be deterministic and fast (no sleeps, no files, no network), so
  that replay works on every platform.

## 7. Non-goals

- Our own sink or backend. spdlog's sinks are the reason to build on spdlog.
- Thread-local or global context (MDC). It contradicts principles 2 and 3; if
  ever added, only as a separate opt-in header.
- A logger registry or "get logger by name".
- Parsing JSON or validating `raw_json`.
- Pretty-printed or multi-line output.

## 8. spdlog v1 / v2 compatibility notes

| Area                  | v1.x (1.12 to 1.17)                                                  | v2.x (untagged branch)     | jspdlog handling                                              |
| --------------------- | -------------------------------------------------------------------- | -------------------------- | ------------------------------------------------------------- |
| Level type            | `spdlog::level::level_enum`                                          | `enum class spdlog::level` | `jspdlog::level` alias; `jspdlog::level::info` works for both |
| Logger level getter   | `level()`                                                            | `log_level()`              | wrapped in `json_logger::log_level()`                         |
| `log_msg` level field | `level`                                                              | `log_level`                | internal helper (formatter)                                   |
| Level names           | `spdlog::level::to_string_view`                                      | `spdlog::to_string_view`   | internal helper (formatter)                                   |
| {fmt} include         | bundled or external via `<spdlog/fmt/fmt.h>`; `std::format` optional | external `<fmt/format.h>`  | version switch; `std::format` mode rejected for now           |
| Async                 | `async_logger` + thread pool                                         | `async_sink`               | user's choice; use `adopt()` for v1                           |
| Global logger         | `set_default_logger`                                                 | `set_global_logger`        | not used by jspdlog                                           |
| Registry              | `register_logger`, `get`, global `set_pattern` etc.                  | removed                    | not used; README lists the safe and breaking calls            |
| Async worker errors   | the logger's error handler                                           | `async_sink`'s own handler | documented in README                                          |
| Library form          | header-only or compiled                                              | compiled                   | transparent                                                   |

The differences are small enough for **one header** with a few `#if`s;
separate per-version headers aren't needed.

## 9. Decisions (2026-11-21 and 2026-11-22)

| #   | Question                             | Decision                                                                                                                                                                    |
| --- | ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Key order                            | Insertion order: outer context, inner context, per-call fields, message. A replaced key keeps its position.                                                                 |
| 2   | Source location                      | Always captured; written only when enabled (off by default); file base name.                                                                                                |
| 3   | Fields that clash with header keys   | Caller's responsibility. Debug-build `JSPDLOG_ASSERT`, like bounds checks in `std::vector`; no release cost.                                                                |
| 4   | Value types                          | Keys are strings; values map to native JSON types wherever possible (section 5, milestone 0.3.0). The message is always a string; `info(value)` takes runtime strings only. |
| 5   | Naming                               | `jspdlog::fields`, `logger.with(...)`.                                                                                                                                      |
| 6   | Payload token                        | One immutable process-wide token. Accepted exception to "no global state": it is a constant, not configurable shared state. `adopt()` keeps installing the formatter.       |
| 7   | Minimum spdlog                       | 1.12.                                                                                                                                                                       |
| 8   | Raw-message sinks                    | A documented route per sink, no stripping helper; `mongo_sink` and `kafka_sink` unsupported.                                                                                |
| 9   | Source `file`                        | Base name, written by the formatter from 0.2.0 on.                                                                                                                          |
| 10  | Process id                           | Cached, because getpid is a system call on Linux; a pthread_atfork handler clears it in a forked child.                                                                     |
| 11  | Per-call key replacing a context key | Context position: a key stays where it first appeared.                                                                                                                      |
| 12  | Getter name for bound fields         | `context()`.                                                                                                                                                                |
| 13  | Deprecated aliases for 0.1 names     | None, clean rename.                                                                                                                                                         |
| 14  | Test option name                     | `JSPDLOG_TEST_NLOHMANN_INTEROP` becomes `JSPDLOG_TEST_WITH_NLOHMANN`.                                                                                                       |
| 15  | Releases                             | Each milestone is tagged `v0.N.0` when complete; the API may break between minor versions until `v1.0.0`.                                                                   |

## 10. Quality gates (every milestone)

- CI matrix: GCC, Clang, AppleClang and MSVC; C++17 and C++20; spdlog 1.12,
  latest 1.x and pinned v2.x.
- Sanitizers (ASan/UBSan, TSan for async), coverage, clang-tidy.
- Warnings as errors in tests and examples.
- From 0.2: fuzz targets run on every pull request and their saved inputs
  replay in every build.
- Every example's output is checked to parse as JSON.
- Benchmark run in CI, reported but not gating.
