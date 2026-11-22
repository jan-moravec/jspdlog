# jspdlog

[![CI](https://github.com/jan-moravec/jspdlog/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jan-moravec/jspdlog/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/jan-moravec/jspdlog/branch/main/graph/badge.svg)](https://codecov.io/gh/jan-moravec/jspdlog)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS-blue)
[![License: MIT](https://img.shields.io/github/license/jan-moravec/jspdlog.svg)](LICENSE)

JSON-lines logging on top of [spdlog](https://github.com/gabime/spdlog), in a single C++17 header.

jspdlog wraps an `spdlog::logger` so that every call made through it writes one JSON object per line, with typed
fields that you add per call or bind to a child logger. You keep spdlog's sinks, levels and async support. It works
with spdlog **v1.x (1.12 or newer)** and **v2.x**.

```json
{"timestamp":"2026-05-13T09:00:00.000+02:00","logger":"app","level":"info","process":1234,"thread":5678,"user_id":42,"message":"hello world"}
```

- **No macros, no globals.** Loggers are plain objects that you create and pass around.
- **Typed fields.** Numbers and booleans stay numbers and booleans, so log tools can filter on them.
- **Context for inner code.** `with_properties()` gives a child logger that adds fields to every line it writes.
- **Always valid JSON** for lines written through jspdlog: messages, keys, values and logger names are escaped.

## Contents

- [Quick start](#quick-start)
- [Install](#install)
- [Usage](#usage)
- [Working with spdlog](#working-with-spdlog)
- [API reference](#api-reference)
- [Guarantees and limits](#guarantees-and-limits)
- [FAQ](#faq)

## Quick start

```cpp
#include <jspdlog/jspdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main()
{
    jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    logger.info("hello {}", "world");
    logger.warn({"queue_depth", 42, "ok", true}, "slow path hit");

    // A child logger with bound fields. The parent is not changed.
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

jspdlog is one header, [include/jspdlog/jspdlog.h](include/jspdlog/jspdlog.h), and depends only on spdlog. Pick one:

- **Copy the header** into your project.

- **CMake FetchContent:**

  ```cmake
  include(FetchContent)
  FetchContent_Declare(jspdlog GIT_REPOSITORY https://github.com/jan-moravec/jspdlog.git GIT_TAG v0.1.0)
  FetchContent_MakeAvailable(jspdlog)
  target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
  ```

- **add_subdirectory(extern/jspdlog)**, then link `jspdlog::jspdlog`.

- **find_package(jspdlog CONFIG REQUIRED)** after `cmake --install`.

With CMake, an installed spdlog of either major version is used if `find_package(spdlog CONFIG)` finds one. Otherwise
spdlog is fetched at `JSPDLOG_SPDLOG_GIT_TAG` (default: a pinned v2.x commit; set for example `v1.17.0` for v1).

| CMake option                    | Default            | Description                                                  |
| ------------------------------- | ------------------ | ------------------------------------------------------------ |
| `JSPDLOG_SPDLOG_GIT_TAG`        | pinned v2.x commit | spdlog tag or commit to fetch when none is installed.        |
| `JSPDLOG_BUILD_TESTS`           | top-level only     | Build the Catch2 tests.                                      |
| `JSPDLOG_BUILD_EXAMPLES`        | top-level only     | Build the programs in [example/](example).                   |
| `JSPDLOG_INSTALL`               | top-level only     | Generate install rules for the header and the CMake package. |
| `JSPDLOG_TEST_NLOHMANN_INTEROP` | `ON`               | Build the `raw_json` / nlohmann/json interop test.           |

## Usage

Every topic below has a short snippet. The linked examples are complete programs; CI builds and runs them and checks
that their output is valid JSON lines.

### Logging calls

Each level (`trace`, `debug`, `info`, `warn`, `error`, `critical`) takes five forms, and `log(level, ...)` takes the
same forms with the level as data:

```cpp
logger.info("loaded {} items", n);    // formatted message, checked at compile time
logger.info(text);                    // any {fmt}-formattable value as the message
logger.info({"items", n});            // fields only, no message
logger.info({"items", n}, "loaded");  // fields and a formatted message
logger.info({"items", n}, text);      // fields and a value as the message
logger.log(jspdlog::level::warn, "disk {}% full", pct);
```

The message is always a JSON string: `info(42)` writes `"message":"42"`. Values that should keep their type belong in
fields: `info({"value", 42})` writes `"value":42`.

Example: [01_console.cpp](example/01_console.cpp).

### Fields

Fields are a `jspdlog::json_properties`, usually written as a braced list of keys and values:

```cpp
logger.info({"user_id", 42, "admin", false, "ratio", 0.25, "name", "ada"}, "login");
// ...,"admin":false,"name":"ada","ratio":0.25,"user_id":42,"message":"login"}
```

| Value type                                       | JSON                                             |
| ------------------------------------------------ | ------------------------------------------------ |
| `std::string`, `std::string_view`, `const char*` | string (a null `const char*` is `null`)          |
| `char`                                           | one-character string                             |
| `bool`                                           | `true` / `false`                                 |
| integers, including `int8_t` / `uint8_t`         | number                                           |
| `float`, `double`, `long double`                 | number; `2.0` keeps `.0`; NaN and Inf are `null` |
| `std::nullptr_t`                                 | `null`                                           |
| `json_properties`                                | nested object                                    |
| `jspdlog::raw_json{"..."}`                       | inserted as is (an empty string is `null`)       |

- Keys are written sorted byte by byte, which is alphabetical for ASCII keys.
- Inserting a key that exists replaces its value. When sets are merged, the right-hand side wins.
- Wide characters and pointers other than `const char*` don't compile.
- A key equal to a header key (`timestamp`, `level`, ...) or to `message` produces a duplicate key. Avoid them.
- Values are serialized when the list is built, before the level check. Guard expensive fields:
  `if (logger.should_log(jspdlog::level::debug)) { ... }`.

### Context: child loggers

`with_properties()` returns a copy of the logger that adds fields to every line it writes. This is how inner code adds
context without changing its caller's logger:

```cpp
void handle(const jspdlog::json_logger &log, const request &req)
{
    const auto req_log = log.with_properties({"request_id", req.id, "user_id", req.user});
    req_log.info("started");
    process(req_log, req);  // everything inside carries request_id and user_id
}
```

Copies are cheap and share the underlying `spdlog::logger`, so the level, sinks and error handler are shared. The bound
fields belong to each copy. The logging calls are `const`, so pass loggers as `const jspdlog::json_logger &`.

Example: [03_properties.cpp](example/03_properties.cpp).

### Arrays and objects from a JSON library

jspdlog has no JSON library dependency. Serialize with yours and pass the text as `raw_json`:

```cpp
nlohmann::json items = {"alpha", "beta"};
logger.info({"items", jspdlog::raw_json{items.dump()}}, "loaded {} items", items.size());
```

The text is not validated; it must be valid JSON. Example: [06_nlohmann_raw_json.cpp](example/06_nlohmann_raw_json.cpp).

### Configuring the line

`jspdlog::json_options` renames or removes the header fields and sets the clock and line ending:

```cpp
jspdlog::json_options opts;
opts.timestamp_key = "ts";                        // rename
opts.process_key = std::nullopt;                  // remove
opts.time_type = spdlog::pattern_time_type::utc;  // timestamps end in Z
opts.eol = "\n";                                  // spdlog's default is "\r\n" on Windows
jspdlog::json_logger logger("app", sink, opts);
// {"ts":"2026-05-13T07:00:00.000Z","logger":"app","level":"info","thread":5678,"message":"hi"}
```

The header fields always come in the order timestamp, logger, level, process, thread. With all of them removed, a line
holds only the fields and the message, or `{}`.

### Sinks

Any spdlog sink works: console, files, rotating and daily files, custom sinks. Pass one sink, a list, or an iterator
range:

```cpp
auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("app.log", 10 * 1024 * 1024, 5);
jspdlog::json_logger logger("app", {console, file});
```

A few sinks need care; see [Sinks](#sinks-1) under Working with spdlog. Examples:
[02_rotating_file.cpp](example/02_rotating_file.cpp), [04_custom_sink.cpp](example/04_custom_sink.cpp).

### Async logging

spdlog v2 makes async a sink; spdlog v1 makes it a logger type, which you wrap with `adopt()`:

```cpp
// spdlog v2
spdlog::sinks::async_sink::config cfg;
cfg.sinks = {std::make_shared<spdlog::sinks::stdout_color_sink_mt>()};
jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::async_sink>(cfg));

// spdlog v1
auto logger = jspdlog::json_logger::adopt(spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("app"));
```

Fields are serialized on the calling thread, so nothing captured by reference can dangle on the worker thread.
Example: [05_async.cpp](example/05_async.cpp).

### Threads

Several threads can log through the same `json_logger`. As in spdlog, use the `_mt` sinks when they do.

## Working with spdlog

jspdlog is a thin layer, and almost all of spdlog stays available. A few spdlog features change the format behind
jspdlog's back. This section explains which ones and why.

### How it works

```text
logger.info({"k", 1}, "hi")
  │  jspdlog serializes the fields and the message:  ,"k":1,"message":"hi"
  ▼
spdlog::logger                   level check, error handling, async queue
  ▼
each sink's formatter            {"timestamp":...,"logger":"app","level":"info",...  ,"k":1,"message":"hi"  }
                                  └── header fields from the pattern ──────────┘  └── from jspdlog ──┘
```

When you create a `json_logger` (or `adopt()` one), jspdlog installs a JSON pattern on every sink of the
`spdlog::logger`. Two spdlog facts follow from this design:

1. **The pattern lives on the sink, not on the logger.** Everything that shares a sink shares its pattern.
1. **Only messages that jspdlog serialized fit the pattern.** A message passed to spdlog in any other way ends up in
   the line unescaped.

### At a glance

**Safe**

- any sink that formats lines: console, files, custom sinks derived from `base_sink`
- levels, `flush_on()`, `flush()` and the error handler
- async through `async_sink` (v2) or `adopt()` (v1)
- several json_loggers sharing a sink, and `spdlog::logger::clone()`
- registering in the spdlog v1 registry, `spdlog::get()`, registry levels

**Needs care**

- sinks that write the raw message instead of the formatted line
- sinks shared with a plain spdlog logger, and sinks added after the json_logger was created
- the default error handler, which writes plain text to stderr
- the lifetime of the spdlog v1 async thread pool
- `SPDLOG_LEVEL_NAMES`, which changes the `level` values

**Breaks the JSON output**

- `set_pattern()` or `set_formatter()` on the logger or its sinks, or globally on a registered logger (v1)
- logging through `spdlog_logger()` directly, or pointing the default logger at it
- `dup_filter_sink`'s "Skipped N duplicate messages.." line and v1 `dump_backtrace()` markers

### Sinks

- **Shared sinks.** Two json_loggers can share a sink: each line carries the name of the logger that wrote it. The
  sink has one pattern, though, so the `json_options` of the logger created last apply to both. Use one set of options
  per sink.

- **Sinks shared with a plain spdlog logger** get whichever pattern was set last. One of the two loggers then writes
  the wrong format. Give each its own sink.

- **Sinks added later**, through `spdlog_logger()->sinks().push_back(...)` or `dist_sink::add_sink`, keep spdlog's
  default text pattern. Add all sinks before creating the json_logger, or create a new json_logger afterwards.

- **Sinks that write the raw message** instead of the formatted line receive jspdlog's internal fragment
  (`,"k":1,"message":"hi"`) without the header fields:

  | Sink                          | What to do                                                        |
  | ----------------------------- | ----------------------------------------------------------------- |
  | `syslog_sink`, `systemd_sink` | Create them with `enable_formatting = true`.                      |
  | `android_sink`                | Keep the default `use_raw_msg = false`.                           |
  | `callback_sink`               | Not suitable. Write a sink derived from `base_sink` (example 04). |
  | `mongo_sink`, `kafka_sink`    | Not suitable; they always send the raw message.                   |
  | `ringbuffer_sink`             | Use the formatted lines, not the raw messages.                    |

- **`dup_filter_sink`** writes its own "Skipped N duplicate messages.." line, which is not JSON in this version.

### Errors and the error handler

spdlog reports problems such as a failed file write to the logger's error handler instead of throwing. jspdlog keeps
that contract: **logging calls do not throw.** If formatting a message fails, for example because a user formatter
throws, the error goes to the same handler and the line is dropped.

The default handler prints a plain-text line such as `[*** LOG ERROR ***] ...` to stderr, at most once per second. If
stderr is collected together with your JSON logs, install a handler that writes JSON:

```cpp
logger.spdlog_logger()->set_error_handler([](const std::string &msg) {
    const jspdlog::json_properties line{"level", "error", "logger", "spdlog", "message", msg};
    std::fputs((line.to_json() + "\n").c_str(), stderr);
});
```

- The handler belongs to the `spdlog::logger`, so it applies to every copy and child of the json_logger.
- **spdlog v2 async:** errors on the worker thread go to the `async_sink`'s own handler, set through
  `async_sink::config::custom_err_handler`, not to the logger's.
- **spdlog v1 async:** an async logger holds only a weak reference to its thread pool. After `spdlog::shutdown()`, or
  after `spdlog::init_thread_pool()` replaces the pool, every call fails with "thread pool doesn't exist anymore".
  Keep the pool alive until you stop logging.
- On spdlog v2, an empty `std::function` restores the default handler. To silence errors, install a lambda that does
  nothing.

### Registry and default logger

jspdlog never registers its loggers and never touches spdlog's global state, so global spdlog calls don't affect a
json_logger unless you connect the two.

**spdlog v1** has a registry. A logger gets into it when you call `spdlog::register_logger()`, or automatically when it
is created by a factory such as `spdlog::stdout_color_mt()` or `spdlog::create_async()`.

| Registry call                                                  | Effect on a registered jspdlog logger                      |
| -------------------------------------------------------------- | ---------------------------------------------------------- |
| `register_logger`, `get`, `drop`                               | Safe. Logging through what `get` returns bypasses jspdlog. |
| `set_level`, `flush_on`, `flush_every`, `cfg::load_env_levels` | Safe and useful: they manage levels and flushing.          |
| `set_error_handler`                                            | Safe.                                                      |
| `set_pattern`, `set_formatter`, `initialize_logger`            | **Breaks JSON**: replaces the pattern on its sinks.        |
| a factory such as `create_async`, then `adopt()`               | Safe, because `adopt()` installs the pattern last.         |

**spdlog v2** has no registry, only a global logger. `spdlog::set_pattern()` and the other global calls act on that
logger only.

**The default (v1) or global (v2) logger** is used by `spdlog::info(...)` and often by third-party libraries. Don't
point it at a jspdlog logger's `spdlog::logger` in this version: those messages are not escaped and produce invalid
lines. Keep the default logger separate, for example on stderr.

### Compile-time settings

| spdlog setting          | Effect on jspdlog                                                                  |
| ----------------------- | ---------------------------------------------------------------------------------- |
| `SPDLOG_LEVEL_NAMES`    | Changes the `level` values. The defaults are `trace` ... `warning` ... `critical`. |
| `SPDLOG_ACTIVE_LEVEL`   | No effect; it only strips spdlog's macros. Use `should_log()`.                     |
| `SPDLOG_NO_THREAD_ID`   | `thread` is always 0.                                                              |
| `SPDLOG_EOL`            | Used when `json_options::eol` is not set.                                          |
| `SPDLOG_USE_STD_FORMAT` | Not supported; the header stops with an error.                                     |
| `SPDLOG_NO_EXCEPTIONS`  | Not supported; jspdlog's constructors throw on a null sink or logger.              |

spdlog's MDC (the `%&` flag, spdlog 1.13+) is thread-local global state and is not included in jspdlog's lines. Use
`with_properties()` instead.

## API reference

The header's Doxygen comments are the full reference. In short:

```cpp
namespace jspdlog {

using level = /* spdlog::level (v2) or spdlog::level::level_enum (v1) */;

class json_logger {
public:
    // Throw std::invalid_argument on a null sink.
    json_logger(std::string name, spdlog::sink_ptr sink, const json_options &options = {});
    json_logger(std::string name, spdlog::sinks_init_list sinks, const json_options &options = {});
    template <typename It> json_logger(std::string name, It first, It last, const json_options &options = {});

    // Wraps an existing logger and installs the JSON pattern on its sinks. Throws on a null logger.
    static json_logger adopt(std::shared_ptr<spdlog::logger> logger, const json_options &options = {});

    // trace, debug, info, warn, error, critical: five forms each, all const. log(level, ...) takes the same forms.
    void info(spdlog::format_string_t<Args...> fmt, Args &&...args) const;
    void info(const T &value) const;
    void info(const json_properties &props) const;
    void info(const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args) const;
    void info(const json_properties &props, const T &value) const;

    json_logger with_properties(json_properties props) const;  // new values win on collisions
    const json_properties &properties() const noexcept;

    // Pass through to the shared spdlog::logger.
    const std::string &name() const noexcept;
    level log_level() const noexcept;
    void set_level(level lvl);
    bool should_log(level lvl) const noexcept;
    void flush();
    void flush_on(level lvl);
    const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept;
};

struct json_options {
    std::optional<std::string> timestamp_key = "timestamp";  // nullopt removes the field
    std::optional<std::string> logger_key = "logger";
    std::optional<std::string> level_key = "level";
    std::optional<std::string> process_key = "process";
    std::optional<std::string> thread_key = "thread";
    spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local;
    std::optional<std::string> eol;  // nullopt: spdlog's default
};

class json_properties {
public:
    json_properties(K &&key, V &&value, Rest &&...rest);  // key, value, key, value, ...
    void insert(std::string key, V value);
    void merge(json_properties other);  // other wins on collisions
    friend json_properties operator+(json_properties a, json_properties b);
    bool empty() const;
    std::size_t size() const;
    void clear();
    std::string to_json() const;  // {"key":value,...}
};

struct raw_json { std::string value; };

}  // namespace jspdlog
```

## Guarantees and limits

Every line written **through jspdlog** is one valid JSON object, as long as `raw_json` contents are valid JSON and
strings are UTF-8. These can still produce invalid or surprising output:

- messages that reach spdlog without going through jspdlog, and the other cases in
  [Working with spdlog](#working-with-spdlog);
- field keys equal to a header key or `message` (duplicate keys);
- bytes that are not valid UTF-8, which are passed through unchanged.

[ROADMAP.md](ROADMAP.md) describes the planned custom `spdlog::formatter`, which escapes messages that bypass jspdlog
and adds source locations.

## FAQ

**Why `\r\n` on Windows?** It is spdlog's default line ending. Set `opts.eol = "\n"`.

**Which C++ standard?** C++17 or newer. CI covers C++17 and C++20 with GCC, Clang, AppleClang and MSVC.

**Can I still use spdlog directly for other loggers?** Yes. Plain spdlog loggers work as before; just don't share
their sinks with a json_logger.

**How do I set the level from the environment?** On spdlog v1, register the logger and call
`spdlog::cfg::load_env_levels()`. On v2, read the variable yourself and call `set_level()`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for building, testing, code style and releases, and [SECURITY.md](SECURITY.md)
for reporting vulnerabilities.

## License

[MIT](LICENSE)
