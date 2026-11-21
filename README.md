# jspdlog

[![CI](https://github.com/jan-moravec/jspdlog/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jan-moravec/jspdlog/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/jan-moravec/jspdlog/branch/main/graph/badge.svg)](https://codecov.io/gh/jan-moravec/jspdlog)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS-blue)
[![License: MIT](https://img.shields.io/github/license/jan-moravec/jspdlog.svg)](LICENSE)

JSON-lines logging on top of [spdlog](https://github.com/gabime/spdlog).

`jspdlog` is a single-header C++17 library that wraps an `spdlog::logger` so
that every call made through it produces one JSON object per line, with typed
structured fields. It works with spdlog **v1.x (>= 1.12)** and **v2.x**, and
keeps spdlog's sinks, levels and async machinery.

```json
{"timestamp":"2026-05-13T09:00:00.000+02:00","logger":"app","level":"info","process":1234,"thread":5678,"user_id":42,"message":"hello world"}
```

## Features

- **Single header**, no dependency beyond spdlog (and the {fmt} it uses).
- **Escaped and structurally valid output.** Messages, keys, string values and
  the logger name are JSON-escaped; the closing `}` is part of the pattern.
- **Typed properties.** Bind key/value pairs to a logger or pass them per call.
  Values are serialized once, at insert time.
- **Any sink**: console, rotating file, syslog, custom sinks, async.
- **Configurable header**: rename or drop `timestamp`, `logger`, `level`,
  `process`, `thread`; local or UTC time; custom line terminator.
- **Bring your own JSON library** for arrays and objects via `raw_json`.

The roadmap, including the planned move to a custom `spdlog::formatter`
(source location, safe handling of messages that bypass jspdlog), is in
[ROADMAP.md](ROADMAP.md).

## Quickstart

```cpp
#include <jspdlog/jspdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main() {
    jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger.set_level(jspdlog::level::trace);

    logger.info("hello {}", "world");
    logger.warn({"queue_depth", 42, "ok", true}, "slow path hit");

    // A child logger with bound properties; the parent is unaffected.
    auto request = logger.with_properties({"request_id", "abc-123"});
    request.info("request started");
}
```

```text
{"timestamp":"...","logger":"app","level":"info","process":...,"thread":...,"message":"hello world"}
{"timestamp":"...","logger":"app","level":"warning","process":...,"thread":...,"ok":true,"queue_depth":42,"message":"slow path hit"}
{"timestamp":"...","logger":"app","level":"info","process":...,"thread":...,"request_id":"abc-123","message":"request started"}
```

## Install

The library is one header that depends only on spdlog.

1. **Copy** [`include/jspdlog/jspdlog.h`](include/jspdlog/jspdlog.h) into your
   project.

1. **CMake `FetchContent`**:

   ```cmake
   include(FetchContent)
   FetchContent_Declare(jspdlog GIT_REPOSITORY https://github.com/jan-moravec/jspdlog.git GIT_TAG v0.1.0)
   FetchContent_MakeAvailable(jspdlog)
   target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
   ```

1. **`add_subdirectory(extern/jspdlog)`**, then link `jspdlog::jspdlog`.

1. **`find_package(jspdlog CONFIG REQUIRED)`** after `cmake --install`.

If an spdlog package is already available (`find_package(spdlog CONFIG)`), it
is used, whichever major version it is. Otherwise `JSPDLOG_SPDLOG_GIT_TAG` is
fetched.

## Build options

Tests, examples and install rules default to `ON` only when jspdlog is the
top-level project.

| Option                          | Description                                                                                        |
| ------------------------------- | -------------------------------------------------------------------------------------------------- |
| `JSPDLOG_BUILD_TESTS`           | Build the Catch2 unit tests.                                                                       |
| `JSPDLOG_BUILD_EXAMPLES`        | Build the programs under `example/`.                                                               |
| `JSPDLOG_INSTALL`               | Generate install rules for the header and CMake package config.                                    |
| `JSPDLOG_TEST_NLOHMANN_INTEROP` | Build the `raw_json` / nlohmann/json interop test (fetched).                                       |
| `JSPDLOG_SPDLOG_GIT_TAG`        | spdlog tag or commit fetched when none is installed (e.g. `v1.17.0`). Default: pinned v2.x commit. |

## API

### `jspdlog::json_logger`

```cpp
// Construction: one sink, a list, or an iterator range. Throws
// std::invalid_argument on a null sink.
json_logger(std::string name, spdlog::sink_ptr sink, const json_options &options = {});
json_logger(std::string name, spdlog::sinks_init_list sinks, const json_options &options = {});
template <typename It>
json_logger(std::string name, It first, It last, const json_options &options = {});

// Wrap an existing spdlog::logger (e.g. a v1 async logger). Its formatter is
// replaced; sinks, level and error handler are kept.
static json_logger adopt(std::shared_ptr<spdlog::logger> logger, const json_options &options = {});

// Same five shapes for trace / debug / info / warn / error / critical,
// and for log(level, ...). All of them are const.
void info(spdlog::format_string_t<Args...> fmt, Args&&... args);   // formatted message
void info(const T& value);                                          // any fmt-formattable value
void info(const json_properties& props);                            // properties only
void info(const json_properties& props, spdlog::format_string_t<Args...> fmt, Args&&... args);
void info(const json_properties& props, const T& value);
void log(jspdlog::level lvl, ...);                                  // level as data

// A copy with additional bound properties (new values win on collisions).
json_logger with_properties(json_properties props) const;
const json_properties& properties() const noexcept;

// Passthroughs to the shared spdlog::logger.
const std::string& name() const noexcept;
jspdlog::level log_level() const noexcept;
void set_level(jspdlog::level lvl);
bool should_log(jspdlog::level lvl) const noexcept;
void flush();
void flush_on(jspdlog::level lvl);
const std::shared_ptr<spdlog::logger>& spdlog_logger() const noexcept;
```

The message is always a JSON string: `info(42)` emits `"message":"42"`. Put
typed values in properties: `info({"value", 42})` emits `"value":42`.

Copies (including `with_properties()` children) share the underlying
`spdlog::logger`, so `set_level`, `flush_on` and the error handler apply to
all of them; bound properties are per copy.

The logging calls are `const`, so functions can take a
`const jspdlog::json_logger &`, and several threads can log through the same
object. As with spdlog, use the `_mt` sinks for multi-threaded logging.

### `jspdlog::json_options`

```cpp
struct json_options {
    std::optional<std::string> timestamp_key = "timestamp";  // nullopt omits the field
    std::optional<std::string> logger_key    = "logger";
    std::optional<std::string> level_key     = "level";
    std::optional<std::string> process_key   = "process";
    std::optional<std::string> thread_key    = "thread";
    spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local;
    std::optional<std::string> eol;   // nullopt: spdlog default ("\r\n" on Windows)
};
```

```cpp
jspdlog::json_options opts;
opts.timestamp_key = "ts";
opts.process_key = std::nullopt;
opts.time_type = spdlog::pattern_time_type::utc;
opts.eol = "\n";
jspdlog::json_logger logger("app", sink, opts);
// {"ts":"2026-05-13T07:00:00.000Z","logger":"app","level":"info","thread":5678,"message":"hi"}
```

Field order is fixed; keys are escaped. With every field omitted a line is
`{"message":"..."}`, or `{}` for an empty properties-only call.

### `jspdlog::json_properties`

```cpp
json_properties{"key1", value1, "key2", value2, ...};
void insert(std::string key, V value);
void merge(json_properties other);                 // other wins on collisions
json_properties operator+(json_properties, json_properties);
bool empty() const; std::size_t size() const; void clear();
std::string to_json() const;                       // {"key1":...,"key2":...}
```

| Value type                                       | JSON                                              |
| ------------------------------------------------ | ------------------------------------------------- |
| `std::string`, `std::string_view`, `const char*` | string (`nullptr` C string: `null`)               |
| `char`                                           | one-character string                              |
| `bool`                                           | `true` / `false`                                  |
| integers (incl. `int8_t` / `uint8_t`)            | number                                            |
| `float`, `double`, `long double`                 | number; `2.0` keeps `.0`; NaN / Inf become `null` |
| `std::nullptr_t`                                 | `null`                                            |
| `json_properties`                                | nested object                                     |
| `raw_json{"..."}`                                | inserted verbatim (empty: `null`)                 |

Wide characters (`wchar_t`, `char8_t`, `char16_t`, `char32_t`) and pointers
other than `const char*` do not compile. Keys are emitted sorted byte by byte
(alphabetically for ASCII keys).

Values are serialized when the properties are built, before the level check.
For expensive fields on a level that is often off, check
`logger.should_log(level)` first.

### `jspdlog::raw_json`

```cpp
struct raw_json { std::string value; };

nlohmann::json items = {"alpha", "beta"};
logger.info({"items", jspdlog::raw_json{items.dump()}}, "loaded {} items", items.size());
```

The value is not validated; the caller guarantees it is valid JSON.

## Guarantees and limits

Every line written **through jspdlog** is one valid JSON object, provided
`raw_json` contents are valid and the input strings are UTF-8. Things that
can still produce invalid or surprising output in this version:

- Logging through the underlying `spdlog::logger` directly (via
  `spdlog_logger()`, the spdlog registry, or code that holds the adopted
  logger): the message lands in the pattern unescaped.
- Calling `set_pattern()` / `set_formatter()` on the underlying logger, or
  sharing a sink with a non-JSON logger (spdlog stores the formatter per
  sink, so the last one set wins).
- Property keys equal to a header key or `message` produce duplicate keys.
- Bytes that are not valid UTF-8 are passed through.

[ROADMAP.md](ROADMAP.md) explains how the planned formatter-based design
addresses these.

## FAQ

**How do I handle sink errors?** Use spdlog's error handler:
`logger.spdlog_logger()->set_error_handler(...)`. To silence errors on spdlog
v2, install a no-op lambda; an empty `std::function` restores spdlog's
default stderr reporter.

**Why `\r\n` on Windows?** That is spdlog's default; set `opts.eol = "\n"`.

**Which C++ standard?** C++17 or newer. CI covers C++17 and C++20 with GCC,
Clang, AppleClang and MSVC.

**Does spdlog's `SPDLOG_USE_STD_FORMAT` work?** Not yet; jspdlog requires
{fmt}. See the roadmap.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for building, testing, sanitizers,
code style and the release process. Security disclosures:
[SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE).
