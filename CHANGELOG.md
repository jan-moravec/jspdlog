# Changelog

All notable changes to **jspdlog** are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.5.0] - 2026-11-25

Fewer allocations when fields are written, an error handler that reports as JSON, and spdlog v1 builds with
`SPDLOG_USE_STD_FORMAT`. Nothing breaks.

### Added

- `jspdlog::make_error_handler(logger_name, options)` returns an spdlog error handler that writes each logging failure
  as one JSON line, to stderr, another `FILE` or a sink (`error_report_options`). It never calls back into spdlog, so a
  sink that fails again cannot deadlock spdlog v2's error mutex (which is held while the handler runs) or recurse in
  spdlog v1, it does not throw, and reports closer together than `min_interval` are counted and carried as `dropped`.
- `jspdlog::key` and the `"name"_key` literal: a field key checked at compile time. A key that needs escaping does not
  compile in C++20 (it throws `std::invalid_argument` when created in C++17), and a checked key is copied into the line
  without a scan. They work in braced lists, `fields` constructors and `fields::insert`.
- `SPDLOG_USE_STD_FORMAT` is supported with spdlog v1 and C++20, where the header used to stop with an error. The CI
  matrix has two new legs for it. spdlog v2 has no such mode.
- Benchmark cases for long keys, `fields` objects built per call, prebuilt ones and `with()`, and tests for splicing,
  checked keys, the error handler (also under a CTest timeout) and std::format.

### Changed

- A `fields` object stores the text of all its fields in one string with an offset table, so building one costs two
  allocations instead of up to two per field, and `with()` no longer joins the text again. `fields::insert` throws
  `std::length_error` when the fields would exceed 4 GiB, the limit of the payload protocol.
- `json_logger` writes the values of a braced list straight into spdlog's buffer instead of through a scratch string.
- A `fields` object passed to a logging call of a logger without bound fields is copied into the line in one piece,
  which halves the cost of a prebuilt one on the benchmark.
- `fmt::runtime` has no counterpart in std::format mode before C++26; the runtime-string call forms cover runtime
  messages there.

## [0.4.0] - 2026-11-25

Exceptions are logged with their causes, the field names of common log schemas are one function call away, and strings
are valid UTF-8 by default. That last one changes output: invalid UTF-8, which earlier releases copied into the line, is
now replaced by U+FFFD (see Changed).

### Added

- Exceptions. A `std::exception` (or anything derived from it) and a `std::exception_ptr` are field values: an object
  with the `type` name and `message`, and the nested causes under `cause`, up to eight deep. Every level and `log()`
  take an exception as their first argument, followed by any of the usual fields and message arguments, and write it
  under `json_options::error_key`, with `error_type_key` and `error_message_key` for its members. It is written only
  when the line is, so a disabled level computes nothing. The type name is the same on GCC, Clang and MSVC, also for
  `std::throw_with_nested`.
- `json_options::source_flat` writes the call site as three fields of the line instead of an object, and
  `source_file_key`, `source_line_key` and `source_function_key` name its parts in both layouts.
- Schema presets, `jspdlog::presets::ecs()`, `gcp()`, `datadog()` and `otel()`, which return a filled `json_options`
  for Elastic ECS, Google Cloud Logging, Datadog and the OpenTelemetry log data model.
- A UTF-8 policy for every string jspdlog writes: keys, values, messages, logger names, level names and call sites.
  `JSPDLOG_UTF8_POLICY` (a macro, and a CMake cache variable that the `jspdlog::jspdlog` target passes on, installed
  or not) is `replace` (default, one U+FFFD per maximal invalid subsequence), `escape` (each invalid byte as `\u00XX`)
  or `pass` (copied, the behavior before this release). `JSPDLOG_ESCAPE_LINE_SEPARATORS` writes U+2028 and U+2029 as
  escapes. `raw_json` is still not checked.
- `json_options::level_names` replaces the level names, `severity_key` and `severity_numbers` add a numeric severity
  after the level (OpenTelemetry numbers by default).
- `json_options::timestamp_style` with `timestamp_format::rfc3339_millis` (default), `rfc3339_micros`,
  `rfc3339_nanos` and `epoch_millis`.
- Tests for each policy, level names, severity and timestamp formats, benchmark cases for UTF-8 and timestamps,
  "Exceptions", "Schema presets" and "Text encoding" sections in the README, and the example `11_presets.cpp`.

### Changed

- Invalid UTF-8 is replaced by U+FFFD by default. It was copied unchanged, which wrote invalid JSON; set
  `JSPDLOG_UTF8_POLICY=pass` for the old behavior. Two different invalid keys of one object can now be replaced by the
  same text and appear twice.
- The fuzz oracles parse every line instead of checking only its shape when a string is not valid UTF-8, and the
  `escape` target compares the result with an independent decoder.

## [0.3.0] - 2026-11-24

Field values cover most of JSON, a disabled level costs a few nanoseconds, and expensive values and arguments can be
skipped when the level is off.

### Added

- Field values cover more of JSON, recursively: `std::optional` (the value or `null`), enums (the name when {fmt} has a
  formatter for the enum, otherwise the number), `std::chrono` durations (milliseconds as a number), `system_clock`
  time points (an RFC 3339 UTC string with milliseconds), ranges (arrays), maps with string-like keys (objects), pairs,
  tuples, maps with other keys (arrays) and `std::variant`.
- `json_writer` and two customization points for your own types: a `jspdlog_serialize(json_writer &, const T &)`
  function found by ADL, and a `jspdlog::serializer<T>` specialization for types you do not own. No JSON library is
  needed.
- Fuzz target `value_types`, benchmark cases for the new value types, the example `09_value_types.cpp` and a "Value
  types" section in the README.
- Lazy values: a callable that takes no arguments, passed as a value in a braced list, runs only when the line is
  written, for example `logger.debug({"plan", [&] { return explain(q); }}, "planned")`. `fields` and `with()` reject
  callables at compile time.
- Optional level macros `JSPDLOG_TRACE`, `JSPDLOG_DEBUG`, `JSPDLOG_INFO`, `JSPDLOG_WARN`, `JSPDLOG_ERROR`,
  `JSPDLOG_CRITICAL` and `JSPDLOG_LOG`, which evaluate none of their arguments when the level is off, and
  `JSPDLOG_ACTIVE_LEVEL`, which removes the calls below a level at compile time. Removed calls stay type-checked.
- Benchmark cases for disabled levels, macros and lazy values, a fuzz check that a braced list and a `fields` object
  give the same line, and the example `10_lazy_values.cpp`.

### Changed

- A braced list passed to a logging call is kept as references and serialized only when the line is written, so a
  call at a disabled level serializes nothing, with no change at the call site. A braced list of more than 16 pairs no
  longer compiles (a `static_assert` says so); build a `fields` object for more. A named `fields` object is still
  accepted.
- A serializer that throws, or a broken one caught by `JSPDLOG_ASSERT`, now fails while the line is written: the
  exception goes to the error handler and the line is dropped. Before, it threw from the logging call while the braced
  list was built. A `fields` object still serializes, and throws, when it is built.
- `fields::insert` is one template that accepts every value with a JSON form. The output of the types that were already
  supported is byte for byte the same. A type without a JSON form, a wide character and a pointer other than a C
  string are now rejected by a `static_assert` with a message instead of a deleted overload, so
  `can_insert`-style SFINAE checks no longer see them as invalid.
- A class whose only route to a field value was an implicit conversion to `bool`, `char` or `const char *` no longer
  compiles as a field value. Cast it, or give it a `jspdlog_serialize` function. A class that converts to
  `std::string_view` keeps working.

## [0.2.0] - 2026-11-23

First release. jspdlog is a single-header library that writes one JSON object per log line on top of spdlog v1.x
(1.12 or newer) and v2.x, with no dependency beyond spdlog and {fmt}.

### Added

- `json_logger` with five call shapes per level plus `log(level, ...)`: a compile-time-checked format string, a
  runtime string, fields alone, and fields with either kind of message. Logging calls do not throw; formatting errors
  go to spdlog's error handler.
- `jspdlog::fields`: typed key/value fields (strings, `bool`, integers, floating point with NaN and infinity as
  `null`, `nullptr`, nested `fields`, `raw_json`), serialized when they are inserted and kept in insertion order. A
  replaced key keeps its position.
- Child loggers with `with()`. They share an immutable, pre-serialized context instead of copying it. Lines read outer
  context, inner context, per-call fields, then the message.
- `json_formatter`, a `spdlog::formatter` that renders the whole line from the spdlog message. A payload from
  `json_logger` carries a process-wide token; any other payload is escaped into the message key, so every line is
  valid JSON whoever logged it.
- `install_json_formatter` for spdlog loggers and sinks that jspdlog does not wrap, and `adopt` for wrapping an
  existing spdlog logger.
- `json_options`, one options struct for the formatter and the logger: rename or omit the timestamp, logger, level,
  process and thread keys, rename the message key, enable the call site with `source_key`, choose local time or UTC
  (ending in `Z`), and set the line ending.
- Call-site capture without macros. The file base name, line and function are written as an object under
  `source_key`, which is off by default.
- Debug-build check that no field key equals an enabled header key, the message key or the source key, through an
  overridable `JSPDLOG_ASSERT`.
- Documentation of every spdlog sink, the error handler, the registry, the default logger and compile-time settings.
- Fuzz targets for escaping, numbers, `fields`, foreign payloads and whole logger lines, and a benchmark program.

[0.2.0]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.2.0
[0.3.0]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.3.0
[0.4.0]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.4.0
[0.5.0]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.5.0
[unreleased]: https://github.com/jan-moravec/jspdlog/compare/v0.5.0...HEAD
