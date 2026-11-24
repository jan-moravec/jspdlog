# Changelog

All notable changes to **jspdlog** are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

### Changed

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
[unreleased]: https://github.com/jan-moravec/jspdlog/compare/v0.2.0...HEAD
