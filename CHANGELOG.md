# Changelog

All notable changes to **jspdlog** are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

First release, 0.1.0.

### Added

- Single header `jspdlog/jspdlog.h`. `jspdlog::json_logger` wraps an
  `spdlog::logger` so that every call made through it emits one JSON object
  per line.
- Works with spdlog **v1.x (>= 1.12)** and **v2.x** from the same header.
  `jspdlog::level` aliases the level type of whichever spdlog is in use.
- Five call shapes per level (`trace` ... `critical`) and `log(level, ...)`:
  format string, value, fields only, fields plus format string, fields plus
  value. Format strings are checked at compile time. The logging calls are
  `const`, so code can log through a `const json_logger &`.
- `with_properties()` creates a child logger with additional bound properties;
  copies share the underlying `spdlog::logger`.
- `json_logger::adopt()` wraps an existing `spdlog::logger`, for example an
  async one. Null loggers and null sinks throw `std::invalid_argument`.
- `jspdlog::json_options`: rename or omit the `timestamp`, `logger`, `level`,
  `process` and `thread` fields, local or UTC time (UTC ends in `Z`), and the
  line terminator.
- `jspdlog::json_properties` with typed values: strings, `char`, `bool`, all
  integers, floating point (NaN and infinity become `null`), `nullptr`,
  nested objects and `jspdlog::raw_json` for pre-serialized JSON. Wide
  characters and pointers other than `const char *` are rejected at compile
  time.
- CMake `INTERFACE` target `jspdlog::jspdlog`, installable and usable through
  `find_package(jspdlog)`. An installed spdlog of either major version is
  used, otherwise `JSPDLOG_SPDLOG_GIT_TAG` is fetched. The installed package
  adds `/utf-8` on MSVC, which {fmt} requires.
- Catch2 tests and examples for console, rotating file, custom sink, async,
  properties and nlohmann/json interop.
