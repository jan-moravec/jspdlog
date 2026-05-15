# Changelog

All notable changes to **jspdlog** are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `jspdlog::json_pattern_options` lets callers rename or omit any of the
  five fixed JSON header fields (`timestamp`, `logger`, `level`, `process`,
  `thread`). All `json_logger` constructors and `adopt()` accept it as an
  optional argument; defaults reproduce the previous output. Omitting every
  field is supported and produces `{"message":"..."}` (or `{}` for an empty
  properties-only call) while still being structurally valid JSON.
- `json_logger::adopt(logger, time_type)` and the three-argument
  `adopt(logger, options, time_type)` overload, so callers don't have to
  remember a separate `set_pattern_time(utc)` after `adopt()` to keep a
  UTC-configured logger UTC.
- `json_logger::pattern_time()` getter for the persisted timestamp mode.
- Rvalue-qualified `json_logger::with_properties() &&` mutates `*this` in
  place and returns by move, so chained construction
  (`make_logger().with_properties(a).with_properties(b)`) avoids the
  lvalue overload's copy step.
- `json_properties` keys now accept `std::string_view` (in addition to
  `std::string` and string literals), via an explicit `std::string{view}`
  step inside the variadic constructor.

### Changed

- **Breaking.** Error-handling surface tightened to two named helpers, both
  now members of `json_logger`:
  - `json_logger::silence_errors()` replaces the previous
    `set_error_handler({})` idiom for dropping spdlog runtime errors.
  - `json_logger::forward_errors_to(destination)` replaces the free
    `jspdlog::forward_errors_to(source, destination)` function.

  The previous `json_logger::set_error_handler(...)` is no longer part of
  the public API; reach through `spdlog_logger()->set_error_handler(...)`
  if you genuinely need a raw callback.
- Integer-valued finite floats (`1.0`, `-3.0`, ...) now serialize with a
  trailing `.0` instead of `1`, `-3`. Without the decimal, downstream JSON
  consumers parse the value as an integer, making the producer's choice of
  `float`/`double` invisible. Negative zero is preserved as `-0.0`.
  Non-finite values still serialize as `null`.
- `set_pattern_time(time_type)` now persists the mode on the json_logger
  rather than only passing it to spdlog's `set_pattern()`. Future internal
  pattern reapplications keep the configured mode instead of silently
  reverting to local.
- The iterator constructor `json_logger(name, begin, end, options = {})`
  is now SFINAE-constrained so a stray call with an iterator over some
  unrelated type fails at the constructor signature with a clear message,
  rather than producing a confusing instantiation error inside spdlog.
- `forward_errors_to` now requires that `destination`'s underlying
  `spdlog::logger` be a different object from the source's; sharing only
  sinks is fine. Asserted in debug builds (the violation would otherwise
  deadlock on spdlog's non-recursive `err_helper` mutex).

### Removed

- `json_logger::set_error_handler(std::function<void(std::string_view)>)` -
  superseded by `silence_errors()` + `forward_errors_to(...)`. Use the
  escape hatch (`spdlog_logger()->set_error_handler(...)`) for raw
  callback installation.
- Free function `jspdlog::forward_errors_to(json_logger&, json_logger)` -
  replaced by the member `json_logger::forward_errors_to(json_logger)`.

## [0.1.0] - 2026-05-13

### Added

- Initial single-header release.
- `jspdlog::json_logger` wraps any `spdlog::logger` and always emits one JSON
  object per log call.
- `jspdlog::json_properties` for typed key/value pairs and a `jspdlog::raw_json`
  wrapper for already-serialized JSON values (objects, arrays, anything from
  any JSON library you like).
- `jspdlog::json_logger::set_error_handler` plus a free `jspdlog::forward_errors_to`
  helper for routing spdlog runtime errors into structured warn lines on a
  fallback logger.
- `char` is serialized as a one-character JSON string (rather than its
  numeric code point); `signed char` / `unsigned char` keep integer behavior.
- CMake `INTERFACE` target `jspdlog::jspdlog`, installable via
  `find_package(jspdlog)`.
- Catch2-based test suite ported from the original HID code, plus tests for
  the new error-handler API.
- Examples for console, rotating file, custom sink, async, properties and
  nlohmann/json interop.
