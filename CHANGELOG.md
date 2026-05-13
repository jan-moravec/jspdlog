# Changelog

All notable changes to **jspdlog** are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
