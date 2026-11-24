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
- **Context for inner code.** `with()` gives a child logger that adds fields to every line it writes.
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
    auto request = logger.with({"request_id", "abc-123"});
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

| CMake option                 | Default            | Description                                                       |
| ---------------------------- | ------------------ | ----------------------------------------------------------------- |
| `JSPDLOG_SPDLOG_GIT_TAG`     | pinned v2.x commit | spdlog tag or commit to fetch when none is installed.             |
| `JSPDLOG_BUILD_TESTS`        | top-level only     | Build the Catch2 tests.                                           |
| `JSPDLOG_BUILD_EXAMPLES`     | top-level only     | Build the programs in [example/](example).                        |
| `JSPDLOG_INSTALL`            | top-level only     | Generate install rules for the header and the CMake package.      |
| `JSPDLOG_TEST_WITH_NLOHMANN` | `ON`               | Build the tests that need nlohmann/json: interop and fuzz replay. |
| `JSPDLOG_BUILD_FUZZERS`      | `OFF`              | Build the libFuzzer targets (Clang only).                         |
| `JSPDLOG_BUILD_BENCH`        | `OFF`              | Build the benchmark program.                                      |

## Usage

Every topic below has a short snippet. The linked examples are complete programs; CI builds and runs them and checks
that their output is valid JSON lines.

### Logging calls

Each level (`trace`, `debug`, `info`, `warn`, `error`, `critical`) takes five forms, and `log(level, ...)` takes the
same forms with the level as data:

```cpp
logger.info("loaded {} items", n);    // formatted message, checked at compile time
logger.info(text);                    // a std::string, std::string_view or const char* as the message
logger.info({"items", n});            // fields only, no message
logger.info({"items", n}, "loaded");  // fields and a formatted message
logger.info({"items", n}, text);      // fields and a runtime string as the message
logger.log(jspdlog::level::warn, "disk {}% full", pct);
```

The message is always a JSON string. A string literal is a format string, checked at compile time; a `std::string`,
`std::string_view` or `const char*` variable, or a writable `char` buffer, is written as it is, without interpreting
braces. A single argument that
is not a string, such as `info(42)`, does not compile: values that should keep their type belong in fields,
`info({"value", 42})` writes `"value":42`. Other string types (`std::pmr::string`, `QString`, a `fmt::memory_buffer`)
are converted by the caller, for example `info(fmt::to_string(buf))`. `fmt::runtime(s)` works with arguments.

Every call also records its file, line and function. They are written when `json_options::source_key` is set, as
`"source":{"file":"main.cpp","line":12,"function":"handle"}`. The function is the short name your compiler gives, so it
can differ between compilers. No macro is needed: [08_source_location.cpp](example/08_source_location.cpp).

Example: [01_console.cpp](example/01_console.cpp).

### Fields

Fields are a `jspdlog::fields`, usually written as a braced list of keys and values:

```cpp
logger.info({"user_id", 42, "admin", false, "ratio", 0.25, "name", "ada"}, "login");
// ...,"user_id":42,"admin":false,"ratio":0.25,"name":"ada","message":"login"}
```

| Value type                                       | JSON                                             |
| ------------------------------------------------ | ------------------------------------------------ |
| `std::string`, `std::string_view`, `const char*` | string (a null `const char*` is `null`)          |
| `char`                                           | one-character string                             |
| `bool`                                           | `true` / `false`                                 |
| integers, including `int8_t` / `uint8_t`         | number                                           |
| `float`, `double`, `long double`                 | number; `2.0` keeps `.0`; NaN and Inf are `null` |
| `std::nullptr_t`                                 | `null`                                           |
| `jspdlog::fields`                                | nested object                                    |
| `jspdlog::raw_json{"..."}`                       | inserted as is (an empty string is `null`)       |

- Keys are written in the order they were first inserted. Inserting a key that exists replaces its value and keeps its
  position. When sets are merged, the right-hand side wins.
- Lines read: header, fields of outer loggers, fields of inner loggers, fields of the call, message. A key that appears
  again keeps its first position, so `with({"step", "a"})` followed by `info({"step", "b"})` writes `step` once, where
  the logger's field was.
- Fields are meant to be small (a handful of keys); lookups scan the keys.
- Wide characters and pointers other than `const char*` don't compile.
- A key equal to a header key (`timestamp`, `level`, ...), to the source key or to `message` produces a duplicate key.
  Avoid them. Debug builds check this through `JSPDLOG_ASSERT`, which defaults to `assert`; define it before including
  jspdlog to use your own handler. Release builds do not check.
- Values are serialized when the list is built, before the level check. Guard expensive fields:
  `if (logger.should_log(jspdlog::level::debug)) { ... }`.

### Context: child loggers

`with()` returns a copy of the logger that adds fields to every line it writes. This is how inner code adds
context without changing its caller's logger:

```cpp
void handle(const jspdlog::json_logger &log, const request &req)
{
    const auto req_log = log.with({"request_id", req.id, "user_id", req.user});
    req_log.info("started");
    process(req_log, req);  // everything inside carries request_id and user_id
}
```

Copies are cheap (two pointers) and share the underlying `spdlog::logger`, so the level, sinks and error handler are
shared. The bound fields are immutable and shared too, which makes logging through copies and children from several
threads safe. `context()` returns them. The logging calls are `const`, so pass loggers as `const jspdlog::json_logger &`.

Example: [03_fields.cpp](example/03_fields.cpp).

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

The header fields always come in the order timestamp, logger, level, process, thread, source. They are followed by the
fields and then the message. With all of them removed, a line holds only the fields and the message, or `{}`.

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

jspdlog is a thin layer, and almost all of spdlog stays available. This section explains how the two fit together and
which spdlog features change the output.

### How it works

```text
logger.info({"k", 1}, "hi")
  │  jspdlog writes a payload: a token, the fields ,"k":1 and the escaped message "hi"
  ▼
spdlog::logger                   level check, error handling, async queue
  ▼
each sink's json_formatter       {"timestamp":...,"logger":"app","level":"info",...,"k":1,"message":"hi"}
                                  └── header fields, from the options ──┘ └── from the payload ──┘
```

When you create a `json_logger` (or `adopt()` one), jspdlog installs a `json_formatter` on every sink of the
`spdlog::logger`. The formatter renders the whole line from spdlog's message: the header fields, the call site when
`source_key` is set, the fields and the message. Two facts follow:

1. **The formatter lives on the sink, not on the logger.** Everything that shares a sink shares its formatter, and so
   its options.
1. **Every payload becomes valid JSON.** The payload of a `json_logger` call starts with a random token that is
   created once per process. The formatter splits such a payload into fields and message. Any other payload, whether
   from `spdlog_logger()->info(...)`, from `SPDLOG_INFO`, from third-party code or from spdlog itself, is escaped into
   the message field. A damaged payload also ends up there, never as broken JSON.

To get the same guarantee for loggers and sinks that jspdlog does not wrap, install the formatter yourself:

```cpp
// spdlog v1: the default logger, often used by libraries
jspdlog::install_json_formatter(*spdlog::default_logger());
// spdlog v2
jspdlog::install_json_formatter(*spdlog::global_logger());

// a sink added to a json_logger's spdlog::logger later
jspdlog::install_json_formatter(*extra_sink, options);
```

`install_json_formatter` takes a `spdlog::logger&` or a `spdlog::sinks::sink&` and the same `json_options` as a
`json_logger`. Example: [07_third_party.cpp](example/07_third_party.cpp).

### At a glance

#### Safe

- any sink that formats lines: console, files, custom sinks derived from `base_sink`
- levels, `flush_on()`, `flush()` and the error handler
- async through `async_sink` (v2) or `adopt()` (v1); the payload, with its token, is copied to the worker
- several json_loggers sharing a sink, several wrappers of one `spdlog::logger`, and `spdlog::logger::clone()`
- logging through `spdlog_logger()`, `SPDLOG_LOGGER_INFO` and similar macros: the message is escaped
- `dup_filter_sink`'s "Skipped N duplicate messages.." line and v1 `dump_backtrace()`: valid lines
- registering in the spdlog v1 registry, `spdlog::get()`, registry levels

#### Needs care

- sinks that write the raw message instead of the formatted line
- sinks added after the json_logger was created, and sinks shared with a plain spdlog logger
- the default error handler, which writes plain text to stderr
- the lifetime of the spdlog v1 async thread pool
- `SPDLOG_LEVEL_NAMES`, which changes the `level` values

#### Breaks the JSON output

- `set_pattern()` or `set_formatter()` on the logger or its sinks, or globally on a registered logger (v1). They
  replace the `json_formatter`, and the lines then show jspdlog's payload text, which starts with `jspdlog:`.

### Sinks

- **Shared sinks.** Two json_loggers can share a sink: each line carries the name of the logger that wrote it. The sink
  has one formatter, though, so the `json_options` of the logger created last apply to both. Use one set of options
  per sink.

- **Sinks shared with a plain spdlog logger.** Call `install_json_formatter` on the plain logger (or the sink) after
  the json_logger was created, and both write valid lines with the same options.

- **Sinks added later**, through `spdlog_logger()->sinks().push_back(...)` or `dist_sink::add_sink`, keep spdlog's
  default text pattern. Call `install_json_formatter(*sink, options)` on the new sink.

- **Sinks that write the raw message** instead of the formatted line see jspdlog's payload, not JSON. Each of them has
  a route:

  | Sink                          | What to do                                                        |
  | ----------------------------- | ----------------------------------------------------------------- |
  | `syslog_sink`, `systemd_sink` | Create them with `enable_formatting = true`.                      |
  | `android_sink`                | Keep the default `use_raw_msg = false`.                           |
  | `ringbuffer_sink`             | Use `last_formatted()`, not `last_raw()`.                         |
  | `callback_sink`               | Format in the callback with your own `json_formatter`, see below. |
  | `qt_sinks`                    | Safe; they call the formatter.                                    |
  | `mongo_sink`, `kafka_sink`    | Not supported; they always store the raw payload.                 |

  A `callback_sink` receives the raw message, so give the callback its own formatter:

  ```cpp
  auto formatter = std::make_shared<jspdlog::json_formatter>(options);
  auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>([formatter](const spdlog::details::log_msg &msg) {
      spdlog::memory_buf_t line;
      formatter->format(msg, line);  // callback_sink_mt calls this under its mutex
      send(std::string_view{line.data(), line.size()});
  });
  ```

  Don't share one `json_formatter` between sinks: like spdlog's formatters it keeps a timestamp cache and relies on the
  sink's mutex. Don't send the raw payload of a jspdlog logger to a place that untrusted parties can read, because the
  token is then visible and a message that starts with it would be taken for fields.

- **Custom sinks** should render the line with `formatter_` (example 04). `msg.payload` holds the payload protocol
  of jspdlog, not JSON, and the protocol may change between versions.

### Errors and the error handler

spdlog reports problems such as a failed file write to the logger's error handler instead of throwing. jspdlog keeps
that contract: **logging calls do not throw.** If formatting a message fails, for example because a user formatter
throws, the error goes to the same handler and the line is dropped.

The default handler prints a plain-text line such as `[*** LOG ERROR ***] ...` to stderr, at most once per second. If
stderr is collected together with your JSON logs, install a handler that writes JSON:

```cpp
logger.spdlog_logger()->set_error_handler([](const std::string &msg) {
    const jspdlog::fields line{"level", "error", "logger", "spdlog", "message", msg};
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

| Registry call                                                  | Effect on a registered jspdlog logger                        |
| -------------------------------------------------------------- | ------------------------------------------------------------ |
| `register_logger`, `get`, `drop`                               | Safe. Logging through what `get` returns writes valid lines. |
| `set_level`, `flush_on`, `flush_every`, `cfg::load_env_levels` | Safe and useful: they manage levels and flushing.            |
| `set_error_handler`                                            | Safe.                                                        |
| `set_pattern`, `set_formatter`, `initialize_logger`            | **Breaks JSON**: replaces the formatter on its sinks.        |
| a factory such as `create_async`, then `adopt()`               | Safe, because `adopt()` installs the formatter last.         |

**spdlog v2** has no registry, only a global logger. `spdlog::set_pattern()` and the other global calls act on that
logger only.

**The default (v1) or global (v2) logger** is used by `spdlog::info(...)` and often by third-party libraries. Call
`install_json_formatter` on it, as shown above, and their output becomes JSON lines too. If it shares a sink with a
json_logger, the options of the last call apply to that sink.

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
`with()` instead.

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

    // Wraps an existing logger and installs a json_formatter on its sinks. Throws on a null logger.
    static json_logger adopt(std::shared_ptr<spdlog::logger> logger, const json_options &options = {});

    // trace, debug, info, warn, error, critical: five forms each, all const. log(level, ...) takes the same forms.
    // Each also takes the call site as a trailing default argument, which you never write.
    void info(format_here_t<Args...> fmt, Args &&...args) const;   // a string literal, or fmt::runtime(...)
    void info(const S &msg) const;                                  // S: std::string, std::string_view, const char*, char[N]
    void info(const fields &f) const;
    void info(const fields &f, format_here_t<Args...> fmt, Args &&...args) const;
    void info(const fields &f, const S &msg) const;

    json_logger with(const fields &f) const;  // new values win on collisions; keys keep their position
    const fields &context() const noexcept;

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
    std::optional<std::string> source_key;  // {"file":...,"line":...,"function":...} when known; off by default
    std::string message_key = "message";
    spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local;
    std::optional<std::string> eol;  // nullopt: spdlog's default
};

class fields {
public:
    fields(K &&key, V &&value, Rest &&...rest);  // key, value, key, value, ...
    void insert(std::string_view key, V value);
    void merge(const fields &other);  // other wins on collisions
    friend fields operator+(fields a, const fields &b);
    bool empty() const;
    std::size_t size() const;
    bool contains(std::string_view key) const;
    void clear();
    std::string to_json() const;  // {"key":value,...} in insertion order
};

struct raw_json { std::string value; };

// Renders every spdlog message as one JSON object. One instance per sink.
class json_formatter final : public spdlog::formatter {
public:
    explicit json_formatter(json_options options = {});
    void format(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest) override;
    std::unique_ptr<spdlog::formatter> clone() const override;
};

void install_json_formatter(spdlog::logger &logger, const json_options &options = {});
void install_json_formatter(spdlog::sinks::sink &sink, const json_options &options = {});

}  // namespace jspdlog
```

## Guarantees and limits

Every line written by a `json_formatter` is one valid JSON object, whoever logged it, as long as `raw_json` contents are
valid JSON and strings are UTF-8. These can still produce invalid or surprising output:

- sinks that write the raw message instead of the formatted line, and `set_pattern()` or `set_formatter()` on a sink
  that replaces the formatter (see [Working with spdlog](#working-with-spdlog));
- field keys equal to a header key or the message key (duplicate keys);
- bytes that are not valid UTF-8, which are passed through unchanged;
- a `long double` beyond the range of `double`: it is a valid JSON number, but many parsers cannot read it;
- a `json_logger` and a formatter installed from another module on one sink, where each module has its own copy of
  the header, such as Windows DLLs: the line stays valid JSON, but the fields appear inside the message;
- a `spdlog::source_loc` whose file name is a temporary: the formatter caches the escaped name per pointer.

The message is a single formatted string, and the call site is written only when `source_key` is set and the message
has one: `json_logger` calls always record it, and for other loggers spdlog's macros and `logger->log(source_loc, ...)`
provide it.

## FAQ

**Why `\r\n` on Windows?** It is spdlog's default line ending. Set `opts.eol = "\n"`.

**Which C++ standard?** C++17 or newer. CI covers C++17 and C++20 with GCC, Clang, AppleClang and MSVC.

**Can I still use spdlog directly for other loggers?** Yes. Call `install_json_formatter` on them and their output
becomes JSON lines, also when they share a sink with a json_logger.

**How do I set the level from the environment?** On spdlog v1, register the logger and call
`spdlog::cfg::load_env_levels()`. On v2, read the variable yourself and call `set_level()`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for building, testing, code style and releases, and [SECURITY.md](SECURITY.md)
for reporting vulnerabilities.

## License

[MIT](LICENSE)
