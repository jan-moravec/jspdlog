# jspdlog

[![CI](https://github.com/hidglobal/jspdlog/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/hidglobal/jspdlog/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/hidglobal/jspdlog/branch/main/graph/badge.svg)](https://codecov.io/gh/hidglobal/jspdlog)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS-blue)
[![License: MIT](https://img.shields.io/github/license/hidglobal/jspdlog.svg)](LICENSE)

JSON-line logging on top of [spdlog](https://github.com/gabime/spdlog) v2.

`jspdlog` is a tiny, single-header C++17 library that wraps any
`spdlog::logger` so that every log call produces exactly one valid JSON
object per line. The JSON pattern is pinned at construction time — there is
no API for changing it — which makes it structurally impossible to emit
malformed JSON.

```json
{"timestamp":"2026-05-13T09:00:00.000+02:00","logger":"app","level":"info","process":1234,"thread":5678,"user_id":42,"message":"hello world"}
```

(The timezone offset is emitted by spdlog's `%z` flag and may render as
either `+02:00` or `+0200` depending on the platform.)

## Why

Searching for a "JSON logging C++ library" turns up surprisingly little —
most projects either roll something custom or post-process plain spdlog
output. `jspdlog` is the smallest possible solution: one header, no
serializer dependency, and full reuse of spdlog's sinks, levels, async
machinery, and ecosystem.

## Features

- **Single header.** `#include <jspdlog/jspdlog.h>` and you're done.
- **Cannot emit invalid JSON.** The spdlog pattern is set internally and
  there is no public API to change it.
- **Works with every spdlog sink.** Rotating file, daily, syslog, Windows
  event log, custom sinks — any `spdlog::sink_ptr` is accepted. Async
  loggers are supported via `json_logger::adopt(...)`, since the public
  constructors always build a synchronous `spdlog::logger`.
- **Typed structured properties.** Bind key/value pairs to a logger, or
  attach them per call; the wire format is built once at insert time, not
  reparsed on every log.
- **No JSON library dependency.** Bring your own (nlohmann/json, RapidJSON,
  glaze, ...) and pass a `jspdlog::raw_json{ your_lib.dump() }` if you need
  to embed arrays or objects.
- **Snake_case API that matches `spdlog::logger`.** If you know spdlog, you
  already know jspdlog.

## Install

Pick whichever is easiest. The library is a single header that only depends
on spdlog v2 (which itself uses {fmt}).

### 1. Copy the header

You already link spdlog; just drop [`include/jspdlog/jspdlog.h`](include/jspdlog/jspdlog.h)
next to it.

### 2. CMake `FetchContent`

```cmake
include(FetchContent)
FetchContent_Declare(
    jspdlog
    GIT_REPOSITORY https://github.com/hidglobal/jspdlog.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(jspdlog)

target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
```

### 3. `add_subdirectory`

```cmake
add_subdirectory(extern/jspdlog)
target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
```

### 4. `find_package` after `cmake --install`

```cmake
find_package(jspdlog CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
```

## Build options

All options default to `ON` when jspdlog is the top-level CMake project and
`OFF` when it's pulled in via `add_subdirectory` / `FetchContent`, so an
embedding project gets a quiet build by default.

| Option                          | Description                                                           |
| ------------------------------- | --------------------------------------------------------------------- |
| `JSPDLOG_BUILD_TESTS`           | Build the Catch2 unit-test suite.                                     |
| `JSPDLOG_BUILD_EXAMPLES`        | Build the standalone example programs under `example/`.               |
| `JSPDLOG_INSTALL`               | Generate `install` rules for the header and CMake package config.     |
| `JSPDLOG_TEST_NLOHMANN_INTEROP` | Build the optional `raw_json` ↔ nlohmann/json interop test (fetched). |

## 30-second quickstart

```cpp
#include <jspdlog/jspdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main() {
    jspdlog::json_logger logger(
        "app",
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>()
    );
    logger.set_level(spdlog::level::trace);

    logger.info("hello {}", "world");
    logger.warn({"queue_depth", 42, "ok", true}, "slow path hit");

    // Bind per-request properties to a child logger; the parent is unaffected.
    auto request = logger.with_properties({"request_id", "abc-123"});
    request.info("request started");
}
```

Output:

```text
{"timestamp":"...","logger":"app","level":"info","process":...,"thread":...,"message":"hello world"}
{"timestamp":"...","logger":"app","level":"warning","process":...,"thread":...,"ok":true,"queue_depth":42,"message":"slow path hit"}
{"timestamp":"...","logger":"app","level":"info","process":...,"thread":...,"request_id":"abc-123","message":"request started"}
```

## API reference

### `jspdlog::json_logger`

```cpp
// Construction. Any spdlog sink, or a list/range of them.
json_logger(std::string name, spdlog::sink_ptr sink);
json_logger(std::string name, spdlog::sinks_init_list sinks);
template <typename It>
json_logger(std::string name, It sinks_begin, It sinks_end);

// Adopt an existing spdlog::logger (the JSON pattern is re-applied). The
// optional time_type preserves a UTC-configured adopted logger through the
// pattern reapplication; it defaults to local.
static json_logger adopt(std::shared_ptr<spdlog::logger> logger,
                         spdlog::pattern_time_type time_type
                             = spdlog::pattern_time_type::local);

// Logging - same overload set for trace/debug/info/warn/error/critical.
void info(spdlog::format_string_t<Args...> fmt, Args&&... args);  // formatted message
void info(const T& msg);                                          // any fmt-formattable value
void info(const json_properties& props,
          spdlog::format_string_t<Args...> fmt, Args&&... args);  // props + formatted message
void info(const json_properties& props, const T& msg);            // props + value
void info(const json_properties& props);                          // properties only, no message

// Property binding. The rvalue overload mutates *this in place so chained
// `make().with_properties(a).with_properties(b)` avoids a second copy.
json_logger with_properties(json_properties props) const&;
json_logger with_properties(json_properties props) &&;

// Error handling. Two helpers cover the supported use cases:
//   * silence_errors()           - drop spdlog runtime errors entirely.
//   * forward_errors_to(...)     - surface them as JSON warn lines on
//                                  another logger (free function below).
// Lower-level callbacks are not part of the public API; reach through
// spdlog_logger()->set_error_handler(...) if you really need one.
void silence_errors();

// Forward spdlog runtime errors from `source` to `destination` as structured
// JSON warn lines tagged with the source logger's name. The handler is
// guarded against re-entrancy, so it's safe to call even when `destination`
// shares a (failing) sink with `source`.
void jspdlog::forward_errors_to(json_logger& source, json_logger destination);

// spdlog passthroughs.
const std::string& name() const noexcept;
spdlog::level log_level() const noexcept;
void set_level(spdlog::level level);
void flush();
void flush_on(spdlog::level level);
void set_pattern_time(spdlog::pattern_time_type time_type);       // local <-> utc

// Escape hatch. Do NOT call set_pattern() on this.
const std::shared_ptr<spdlog::logger>& spdlog_logger() const;
```

### `jspdlog::json_properties`

```cpp
json_properties();
json_properties(key1, value1, key2, value2, ...);                 // variadic key/value pairs

// Typed inserts. The set is exhaustive: every supported scalar gets a
// dedicated overload so values are serialized once, at insert time, with
// no surprise routing through fmt or to_string().
void insert(const std::string& key, std::nullptr_t);              // -> null
void insert(const std::string& key, std::string_view value);
void insert(const std::string& key, const std::string& value);
void insert(const std::string& key, const char* value);           // null -> null
void insert(const std::string& key, bool value);
void insert(const std::string& key, char value);                  // one-character JSON string
template <typename T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>
                                                 && !std::is_same_v<T, char>, int> = 0>
void insert(const std::string& key, T value);                     // every integral type except bool and char:
                                                                  //   signed/unsigned char, short, int, long,
                                                                  //   long long, size_t, int8_t..int64_t, ...
void insert(const std::string& key, float value);                 // NaN / Inf serialize as null,
void insert(const std::string& key, double value);                // integer-valued floats keep a
                                                                  // trailing `.0` so downstream
                                                                  // JSON parsers see a stable type
void insert(const std::string& key, const raw_json& value);       // empty -> null
void insert(const std::string& key, raw_json&& value);
template <typename T>
void insert(const std::string& key, const T* value);              // null -> null, otherwise *value
                                                                  //                (recurses through pointer chains)

// Wide-character types are deliberately deleted: encoding a single
// wchar_t / char16_t / char32_t to UTF-8 needs a unicode encoder the
// library doesn't bundle. Convert to a UTF-8 std::string first.
void insert(const std::string&, wchar_t)  = delete;
void insert(const std::string&, char16_t) = delete;
void insert(const std::string&, char32_t) = delete;

void merge(const json_properties& other);                         // other overrides this
void merge(json_properties&& other);

bool empty() const;
std::string to_string() const;                                    // serialized fragment, leading ','
void append_merged_to(std::string& out,                           // hot-path: walks two
                      const json_properties& rhs) const;          // sorted maps in lockstep,
                                                                  // appends merged result to `out`
                                                                  // (rhs wins on collisions)
```

`json_properties` overloads `operator+` for composition (`a + b` returns the
merge with `b`'s values winning on collisions).

Keys are emitted in **lexicographic order** rather than insertion order. This
keeps the output deterministic for a given set of keys and makes log-line
regression tests trivial to write, but it does mean callers should not rely
on a specific field ordering when scanning by eye.

Note that plain `char` is serialized as a one-character JSON string (so
`{"c", 'a'}` produces `"c":"a"`, not `"c":97`). `signed char` and
`unsigned char` keep integer behavior because they're the canonical
`int8_t` / `uint8_t` types.

### `jspdlog::raw_json`

```cpp
struct raw_json { std::string value; };
```

A tagged wrapper for pre-serialized JSON. The contents are inserted
verbatim with no validation; the caller guarantees they are valid JSON.

Bridge to any JSON library:

```cpp
nlohmann::json items = {"alpha", "beta", "gamma"};
logger.info(
    {"items", jspdlog::raw_json{items.dump()}},
    "loaded {} items", items.size()
);
```

## How does it compare to plain spdlog?

|                                           | `spdlog::logger` directly           | `jspdlog::json_logger`                     |
| ----------------------------------------- | ----------------------------------- | ------------------------------------------ |
| Output format                             | Whatever pattern you set            | Always one JSON object per line            |
| Pattern footgun                           | Easy to break with `set_pattern`    | API forbids it                             |
| Structured fields                         | Stringify into the message yourself | First-class `json_properties` + `raw_json` |
| Sinks                                     | Any                                 | Any (delegates)                            |
| Levels, async, formatters, error handlers | Yes                                 | Yes (delegates)                            |
| Source location capture                   | Yes                                 | Not yet — open issue if you need it        |

## FAQ

**Can I set my own spdlog pattern?**
No. That's the entire point of the library: the JSON pattern is fixed so
the output is guaranteed to be valid JSON. If you need a different
pattern, use `spdlog::logger` directly.

**Why does `jspdlog::raw_json` exist? Can't I just pass a `std::string`?**
A `std::string` value gets quoted and escaped (because it's a JSON string).
`raw_json` is for inserting values that are *already* JSON-encoded —
arrays, objects, numbers from another serializer, etc.

**Does `with_properties()` allocate a new spdlog logger?**
No. The returned child shares the parent's `std::shared_ptr<spdlog::logger>`
(and therefore its sinks, level, error handler). Only the bound property
strings are copied. As a consequence, *bound properties are isolated per
child*, but configuration changes are not: calling `set_level()`,
`silence_errors()`, or `flush_on()` on the child reconfigures the
underlying spdlog logger and so affects every json_logger derived from the
same root.

**Is this header-only?**
Yes. No `.cpp` files, no `JSPDLOG_COMPILED_LIB` mode, no link step beyond
what spdlog already requires.

**Which C++ standard?**
C++17 minimum. Tested with C++17 and C++20 in CI.

**Which spdlog version?**
spdlog v2.x. The v2 line is currently maintained on the `v2.x` branch and
is not yet tagged; the bundled CMake config pins a known-good commit.

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the developer reference: prerequisites, [build instructions](CONTRIBUTING.md#building) with [CMake presets](CMakePresets.json), how to [run the test suite](CONTRIBUTING.md#running-tests), the [sanitizer and coverage workflows](CONTRIBUTING.md#sanitizers-and-coverage), the [code style and pre-commit](CONTRIBUTING.md#code-style-and-pre-commit) setup, the [pull-request workflow](CONTRIBUTING.md#pull-requests) and the [release process](CONTRIBUTING.md#release-process).

For security disclosures, see [SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE).
