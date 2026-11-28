# Changelog

All notable changes to **jspdlog** are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.7.0] - 2026-11-28

The sink owns the schema: `json_options` are applied by the `json_formatter` of each sink and by nothing else, so a
line follows one set of options. The call API is smaller and does more: one message type, a public call site for
wrapper functions, `std::exception_ptr` as an exception and a null logger. Some behavior changes and two names change.

### Added

- `jspdlog::call_site`, the file, line and function of a call, and `json_logger::at`, which takes every call form and
  records the given place, so a function that logs for its caller can record the caller's line. A call site also comes
  from `call_site::of`, given a file, line and function, an `spdlog::source_loc` or, in C++20, a
  `std::source_location`.
- Every exception call form takes a `std::exception_ptr`, such as `std::current_exception()` in `catch (...)`.
- A default-constructed `json_logger` is a null logger, which writes nothing, and `explicit operator bool` tells it
  apart. A moved-from logger is a null logger too; using one dereferenced a null pointer.
- `json_options::error_cause_key`, the key of the nested cause inside an exception object.
- `json_logger::attach` wraps an `spdlog::logger` whose sinks already have a `json_formatter`, without replacing it, so
  sinks set up once with `install_json_formatter` keep their own options.

### Changed

- **Breaking:** `basic_format_here` is `basic_message` and `format_here_t` is `message_t`, without aliases. One message
  type takes format strings and runtime strings, so each level has seven overloads instead of eleven. Logging calls
  need no change: with arguments a string is a format string, and without them, a literal included, it is written as
  it is.
- **Breaking** for code that names internals: `detail::site` is the public `call_site`, whose constructor takes a tag
  before the file, line and function.
- **Breaking:** options that would write a key twice throw `std::invalid_argument` when a formatter is created: by the
  `json_logger` constructors, `adopt`, `install_json_formatter`, the factories and `make_error_handler`. Checked are the
  keys of the line (header, source, message and error keys), the members of the call site object and those of an
  exception object. Such options always produced duplicate keys.
- **Breaking:** an exception that is a field value uses the `error_type_key`, `error_message_key` and
  `error_cause_key` of the sink that writes the line, as the exception of an exception call does. `fields::to_json` and
  a `json_writer` that you create keep `type`, `message` and `cause`.
- **Breaking:** in an exception call the exception is the only field under the error key. It takes the position of the
  first other field with that key, if there is one, and the others are left out. A per-call field with the key no longer
  replaces the exception, and no longer triggers an assertion.
- **Breaking:** the assertion for a field key that equals a header, source or message key fires when a line is written,
  with the keys of the sink that writes it, and no longer in `with` or by the options of the logger. A
  `JSPDLOG_ASSERT` of your own that evaluates its condition in every build now walks the fields of every line.
- **Breaking** for code that builds payloads by hand: the payload protocol, which is internal, has a flags digit and
  markers for the keys that only the formatter knows.
- Every part of a line on a sink shared by several loggers follows the options of its formatter, exception keys
  included. They used to come from the logger that logged.

### Performance

- `with` copies only the bound fields, and with an empty set it returns a logger that shares them.
- A line is copied into spdlog's buffer in bulk instead of byte by byte. In the benchmark a line costs up to 40 percent
  less with MSVC and GCC, most for long messages.

## [0.6.1] - 2026-11-27

Fixes for defects found in a review of 0.6.0. No API changes.

### Fixed

- `std::byte` is a number with {fmt} 10 and newer too; it was written as a string of digits, so a
  `std::vector<std::byte>` was an array of strings.
- spdlog v1's `enable_backtrace()` stores `json_logger` calls below the level, from the member functions and the
  macros, so `dump_backtrace()` writes them. They were dropped. While a backtrace is enabled, such calls serialize their
  fields, run lazy values and format their arguments, as spdlog's own calls do. spdlog v2 has no backtrace.
- The header timestamp is a valid RFC 3339 string or `null`, whatever time a caller gives spdlog. A year outside 0000 to
  9999 and, on Windows, a time before 1970 wrote a broken string, or one that could break the line. UTC timestamps no
  longer depend on `gmtime`, and a time that has no local form is written in UTC with `Z`.
- On macOS, a `fork()` after `dlclose()` of a shared library that logged the process id no longer crashes: macOS keeps
  that library's `pthread_atfork` handler after unloading it, so the id is no longer cached there and is read on every
  line. glibc removes the handler, so Linux keeps the cache.
- Documentation: the README no longer says that the formatter caches the file name of a call site per pointer. It
  caches nothing, so a temporary file name is safe.

## [0.6.0] - 2026-11-26

Factories for the usual loggers, trace context, nlohmann/json values, and a vcpkg port and Conan recipe. Nothing
breaks.

### Added

- `jspdlog/factories.h`, an optional header: `make_stdout_logger`, `make_stderr_logger` and
  `make_rotating_file_logger` make a `json_logger` without an spdlog sink header, a registry or a global.
- `make_async_logger` makes a logger with a worker thread of its own, an spdlog thread pool that it owns with v1 and an
  `async_sink` with v2. The last copy of the logger writes the queued lines and stops the thread. `async_options` sets
  the queue size, the overflow policy (`discard_new` is not in spdlog 1.12, where it throws) and whether errors from
  both the worker and the calling thread are reported as JSON lines on stderr.
- Trace context in the core header: `trace_context`, `parse_traceparent` for W3C `traceparent` headers of version 00,
  and `trace_fields` with the OpenTelemetry names, which `trace_keys` renames.
- `presets::ecs_trace_fields`, `presets::gcp_trace_fields` (the bare trace id, or the `projects/<project>/traces/<id>`
  form when given a project) and `presets::datadog_trace_fields` (the full ids in hex).
- `jspdlog/nlohmann.h`, an optional header that makes `nlohmann::json`, `ordered_json` and other `basic_json` values
  field values, for nlohmann/json 3.10 or newer. Invalid UTF-8 in them is always replaced.
- README recipes for OpenTelemetry C++ and glaze, and the examples `12_factories.cpp` and `13_trace_context.cpp`.
- A vcpkg port, in `packaging/vcpkg`, and a Conan 2 recipe, `conanfile.py`, both tested in CI on Linux and Windows.
  They are not in the vcpkg registry or ConanCenter yet; the README shows how to use them from a checkout.
- A weekly build against the head of spdlog's `v2.x` branch, which has no tagged release yet.
- A release workflow that checks a pushed version tag and drafts the GitHub Release from this file.

## [0.5.0] - 2026-11-26

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
[0.6.0]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.6.0
[0.6.1]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.6.1
[0.7.0]: https://github.com/jan-moravec/jspdlog/releases/tag/v0.7.0
[unreleased]: https://github.com/jan-moravec/jspdlog/compare/v0.7.0...HEAD
