# jspdlog

[![CI](https://github.com/jan-moravec/jspdlog/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jan-moravec/jspdlog/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/jan-moravec/jspdlog/branch/main/graph/badge.svg)](https://codecov.io/gh/jan-moravec/jspdlog)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS-blue)
[![License: MIT](https://img.shields.io/github/license/jan-moravec/jspdlog.svg)](LICENSE)

JSON-lines logging on top of [spdlog](https://github.com/gabime/spdlog), in a single C++17 header, with small optional
headers for [factories](#factories) and [nlohmann/json](#arrays-and-objects-from-a-json-library).

jspdlog wraps an `spdlog::logger` so that every call made through it writes one JSON object per line, with typed
fields that you add per call or bind to a child logger. You keep spdlog's sinks, levels and async support. It works
with spdlog **v1.x (1.12 or newer)** and **v2.x**.

```json
{"timestamp":"2026-05-13T09:00:00.000+02:00","logger":"app","level":"info","process":1234,"thread":5678,"user_id":42,"message":"hello world"}
```

- **No macros required, no globals.** Loggers are plain objects that you create and pass around. Optional macros skip
  the arguments of a call whose level is off.
- **Typed fields.** Numbers and booleans stay numbers and booleans, so log tools can filter on them. Optionals, enums,
  durations, time points, containers and your own types have a JSON form too.
- **Context for inner code.** `with()` gives a child logger that adds fields to every line it writes.
- **Exceptions and schema presets.** `logger.error(ex, "...")` writes the exception with its causes, and one function
  call gives the field names of Elastic ECS, OpenTelemetry, Google Cloud Logging or Datadog.
- **Trace context.** Parse a W3C `traceparent` header and bind the trace and span id to a child logger, under the names
  those backends read. No tracing library needed.
- **Always valid JSON, and valid UTF-8 by default** for lines written through jspdlog: messages, keys, values and
  logger names are escaped, and invalid UTF-8 is replaced.

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
#include <jspdlog/factories.h>

int main()
{
    const auto logger = jspdlog::make_stdout_logger("app");

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

The library is one header, [include/jspdlog/jspdlog.h](include/jspdlog/jspdlog.h), and depends only on spdlog. Two
optional headers next to it include it and add to it:

| Header                                               | Adds                                       | Needs besides the core header                  |
| ---------------------------------------------------- | ------------------------------------------ | ---------------------------------------------- |
| [`jspdlog/factories.h`](include/jspdlog/factories.h) | [Factories](#factories) for common loggers | spdlog's stdout, rotating file and async parts |
| [`jspdlog/nlohmann.h`](include/jspdlog/nlohmann.h)   | `nlohmann::json` values as field values    | nlohmann/json 3.10 or newer, which you provide |

The optional headers and the [trace context](#trace-context) are newer than the latest tagged release. Until 0.6.0 is
tagged, take them from `main`.

Pick one:

- **Copy the directory** [include/jspdlog](include/jspdlog) into your project.

- **CMake FetchContent:**

  ```cmake
  include(FetchContent)
  FetchContent_Declare(jspdlog GIT_REPOSITORY https://github.com/jan-moravec/jspdlog.git GIT_TAG v0.4.0)
  FetchContent_MakeAvailable(jspdlog)
  target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
  ```

- **add_subdirectory(extern/jspdlog)**, then link `jspdlog::jspdlog`.

- **find_package(jspdlog CONFIG REQUIRED)** after `cmake --install`.

- **[vcpkg or Conan](#vcpkg-and-conan)**, from a checkout of this repository.

With CMake, an installed spdlog of either major version is used if `find_package(spdlog CONFIG)` finds one. Otherwise
spdlog is fetched at `JSPDLOG_SPDLOG_GIT_TAG` (default: a pinned v2.x commit; set for example `v1.17.0` for v1).

| CMake option                     | Default            | Description                                                                                          |
| -------------------------------- | ------------------ | ---------------------------------------------------------------------------------------------------- |
| `JSPDLOG_SPDLOG_GIT_TAG`         | pinned v2.x commit | spdlog tag or commit to fetch when none is installed.                                                |
| `JSPDLOG_BUILD_TESTS`            | top-level only     | Build the Catch2 tests.                                                                              |
| `JSPDLOG_BUILD_EXAMPLES`         | top-level only     | Build the programs in [example/](example).                                                           |
| `JSPDLOG_INSTALL`                | top-level only     | Generate install rules for the headers and the CMake package.                                        |
| `JSPDLOG_TEST_WITH_NLOHMANN`     | `ON`               | Build the tests that need nlohmann/json: interop and fuzz replay.                                    |
| `JSPDLOG_BUILD_FUZZERS`          | `OFF`              | Build the libFuzzer targets (Clang only).                                                            |
| `JSPDLOG_BUILD_BENCH`            | `OFF`              | Build the benchmark program.                                                                         |
| `JSPDLOG_UTF8_POLICY`            | `replace`          | Invalid UTF-8 in logged strings: `replace`, `escape` or `pass`. See [Text encoding](#text-encoding). |
| `JSPDLOG_ESCAPE_LINE_SEPARATORS` | `OFF`              | Write U+2028 and U+2029 as escapes. See [Text encoding](#text-encoding).                             |

### vcpkg and Conan

jspdlog is not in the vcpkg registry or in ConanCenter yet. This repository holds a vcpkg port and a Conan recipe, and
CI tests both on Linux and Windows. Each uses its package manager's spdlog, which is v1, installs the headers with the
default [text encoding](#text-encoding) settings, and gives you the target `jspdlog::jspdlog`:

```cmake
find_package(jspdlog CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE jspdlog::jspdlog)
```

**vcpkg:** the [port](packaging/vcpkg/ports/jspdlog) is written for the registry, where it downloads a release. A script
turns it into an overlay port that builds your checkout instead. Run it from the checkout, with Git Bash on Windows:

```sh
scripts/ci/vcpkg-local-port.sh . build/overlay
vcpkg install jspdlog --overlay-ports=build/overlay
```

After updating the checkout, run `vcpkg remove jspdlog` and then both commands again.

**Conan 2:** create the package in your local cache from the checkout, then require `jspdlog/<version>` in your project,
with the version that [conanfile.py](conanfile.py) states. Your profile needs C++17 or newer.

```sh
conan create . --build=missing -s compiler.cppstd=17
```

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

A braced list is not built into a `fields` object. It is kept as references and serialized only when the line is
written, so a call at a disabled level serializes nothing (see [Expensive values](#expensive-values)).

Every call also records its file, line and function. They are written when `json_options::source_key` is set, as
`"source":{"file":"main.cpp","line":12,"function":"handle"}`. The function is the short name your compiler gives, so it
can differ between compilers. No macro is needed: [08_source_location.cpp](example/08_source_location.cpp). With
`source_flat` the three values are fields of the line instead of an object, as ECS and OpenTelemetry want them;
`source_file_key`, `source_line_key` and `source_function_key` rename them in both layouts (see
[Schema presets](#schema-presets)).

Example: [01_console.cpp](example/01_console.cpp).

### Factories

[`jspdlog/factories.h`](include/jspdlog/factories.h) makes the usual loggers without any spdlog sink header:

```cpp
#include <jspdlog/factories.h>

const auto console = jspdlog::make_stdout_logger("app");
const auto errors = jspdlog::make_stderr_logger("app");
const auto file = jspdlog::make_rotating_file_logger("app", "app.log", 10 * 1024 * 1024, 5);  // size, old files kept
```

Each takes `json_options` as its last argument and returns an ordinary `json_logger`. The sinks are spdlog's
thread-safe `stdout_sink_mt`, `stderr_sink_mt` and `rotating_file_sink_mt`, without colors, which a JSON line has no
use for. Nothing is registered with spdlog.

`make_async_logger` takes the sinks and gives the logger a worker thread of its own: an spdlog thread pool of one
thread with v1, an `async_sink` with v2. There is no global thread pool, and `spdlog::shutdown()` does not affect it.

```cpp
auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("app.log", 10 * 1024 * 1024, 5);
jspdlog::async_options async;
async.queue_size = 16384;
async.overflow = jspdlog::overflow_policy::overrun_oldest;
const auto logger = jspdlog::make_async_logger("app", {sink}, async);  // json_options go last
```

| `async_options` | Default | Meaning                                                                                                                                                     |
| --------------- | ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `queue_size`    | 8192    | Lines the queue holds, at least 1. spdlog v2 accepts at most 250000.                                                                                        |
| `overflow`      | `block` | What a call does when the queue is full: `block`, `overrun_oldest` or `discard_new`.                                                                        |
| `json_errors`   | `true`  | Report the logger's errors as JSON lines on stderr, with [`make_error_handler`](#errors-and-the-error-handler) and the keys of the logger's `json_options`. |

- The worker and its queue live as long as the last copy of the logger, counting children and the pointers returned by
  `spdlog_logger()`. When the last one goes away, the queued lines are written to the sinks and the thread is joined,
  so let that happen before `main` returns. With spdlog v1, `spdlog::logger::clone()` does not keep the worker alive.
- spdlog 1.12 has no `discard_new` policy, and `make_async_logger` throws `std::invalid_argument` for it there. It
  throws the same for a null sink, for a queue size of 0 and, with spdlog v2, for one above 250000.
- `flush()` only queues a flush behind the waiting lines and returns at once. Every line is on the sinks when the last
  copy of the logger is gone.

Example: [12_factories.cpp](example/12_factories.cpp).

### Fields

Fields are keys and values, usually written as a braced list. A `jspdlog::fields` object holds the same, serialized
when it is built, for fields you reuse or bind to a child logger with `with()`. For example:

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
| `nlohmann::json`, with `jspdlog/nlohmann.h`      | the JSON value                                   |

- Keys are written in the order they were first inserted. Inserting a key that exists replaces its value and keeps its
  position. When sets are merged, the right-hand side wins.
- Lines read: header, fields of outer loggers, fields of inner loggers, fields of the call, message. A key that appears
  again keeps its first position, so `with({"step", "a"})` followed by `info({"step", "b"})` writes `step` once, where
  the logger's field was.
- Fields are meant to be small (a handful of keys); lookups scan the keys. A `fields` object keeps the text of all its
  fields in one buffer, so building one costs two allocations however many fields it has.
- Wide characters and pointers other than `const char*` don't compile. More types are covered under
  [Value types](#value-types).
- A key equal to a header key (`timestamp`, `level`, ...), to the source key or to `message` produces a duplicate key.
  Avoid them. Debug builds check this through `JSPDLOG_ASSERT`, which defaults to `assert`; define it before including
  jspdlog to use your own handler. Release builds do not check.
- A braced list passed to a logging call is serialized only when the line is written. A `fields` object is
  serialized when it is built. A braced list holds at most 16 pairs; build a `fields` object for more.
- A key that appears twice in one braced list takes the last value at the first position, as in a `fields` object.

#### Checked keys

A key written as `"name"_key` is checked when the program is compiled and then copied into the line without a scan for
characters that need escaping. A key that needs escaping (a control character, a quote, a backslash or a byte of 0x80 or
more) does not compile in C++20. In C++17 the check runs when the key is created and throws `std::invalid_argument`;
declare the key `constexpr` to check it at compile time there too. It works wherever a key does:

```cpp
using namespace jspdlog::literals;
logger.info({"user_id"_key, 42, "ok"_key, true}, "login");
constexpr auto user = "user_id"_key;  // checked at compile time in C++17 as well
fields f{user, 42};
```

String-literal keys without the suffix work as before and are escaped when the line is written. On the benchmark in
[CONTRIBUTING](CONTRIBUTING.md#benchmarks), checked keys make a braced list of six fields several percent faster, more
with long keys.

#### Expensive values

A braced list is serialized only when the line is written, but the expressions in it run first, as for any function
argument. Pass a callable instead of an expensive value; it runs only when the line is written:

```cpp
logger.debug({"id", q.id, "plan", [&] { return explain(q); }}, "planned");
```

The callable runs in the calling thread before the call returns, also with async logging, so capturing by reference
is safe. It runs whenever the logger's level lets the call through, even if every sink then drops the line because of
its own, higher level: spdlog formats a line before its sinks check their levels. Callables are allowed only in the
braced list of a call, not in a fields object or `with()`: bound fields are serialized once, when the child logger is
created. An exception thrown by the callable, or by a serializer of a value in a braced list, goes to the error handler
and the line is dropped, like an exception from a formatter. A type that has a JSON form is always a value, even when
it is callable. Format arguments are evaluated before the call too; use the [macros](#level-macros-optional) when they
are expensive. Example: [10_lazy_values.cpp](example/10_lazy_values.cpp).

### Level macros (optional)

`JSPDLOG_TRACE`, `JSPDLOG_DEBUG`, `JSPDLOG_INFO`, `JSPDLOG_WARN`, `JSPDLOG_ERROR`, `JSPDLOG_CRITICAL` and
`JSPDLOG_LOG(logger, level, ...)` take the same arguments as the member functions. They evaluate nothing in the call
when the level is off, neither fields nor format arguments:

```cpp
JSPDLOG_DEBUG(logger, {"plan", explain(q)}, "planned {}", describe(q));
```

They are optional: every feature works through the member functions. Each is one statement, a `do { } while (false)`,
so clang-tidy's `cppcoreguidelines-avoid-do-while` reports every call unless its `IgnoreMacros` option is set. The
source line a macro records is the line of the macro. A macro is also the only way to remove calls at compile time. Define
`JSPDLOG_ACTIVE_LEVEL` to one of spdlog's `SPDLOG_LEVEL_*` values, and the macros below it are removed:

```cmake
target_compile_definitions(app PRIVATE JSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
```

A removed call is still type-checked, so it cannot rot, and a variable that only logging uses is still used. Only the
macros read the setting, so translation units with different values link together. It is independent of
`SPDLOG_ACTIVE_LEVEL`: spdlog defaults that one to info, which would remove every `JSPDLOG_DEBUG` silently, so
jspdlog defaults to trace and removes nothing.

### Value types

Values keep their JSON type, however deeply they are nested. Everything below also works inside containers, so
`std::map<std::string, std::optional<std::vector<int>>>` and `std::vector<jspdlog::fields>` are fine.

| C++ type                                                         | JSON                                                                                                |
| ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| `std::optional<T>`                                               | the value, or `null` when empty                                                                     |
| enums                                                            | the name if {fmt} has a formatter for the enum, otherwise the number                                |
| `std::chrono::duration`                                          | milliseconds as a number                                                                            |
| `std::chrono::system_clock::time_point` (any duration)           | RFC 3339 UTC string with milliseconds, `"2026-11-23T08:00:00.123Z"`                                 |
| ranges (`vector`, `array`, `span`, `list`, `set`, C arrays)      | array                                                                                               |
| maps whose key converts to `std::string_view`                    | object, in the iteration order of the map                                                           |
| `std::pair`, `std::tuple`, maps with other keys                  | array (`[key, value]` pairs for such maps)                                                          |
| `std::variant`, `std::monostate`                                 | the active alternative; `null` for `monostate` and a valueless variant                              |
| `std::exception` and types derived from it, `std::exception_ptr` | object with `type`, `message` and `cause`, see [Exceptions](#exceptions); `null` for a null pointer |
| your own types                                                   | through `jspdlog_serialize` or `jspdlog::serializer`, see below                                     |

- **Enums.** An enum written as a name needs a `fmt::formatter` specialization, or `format_as` with {fmt} 10 and newer.
  `format_as` that returns an integer gives a number. {fmt} 9, which spdlog 1.12 bundles, formats every unscoped enum
  as an integer, and jspdlog still writes a number for it, so the output does not depend on the {fmt} version.
- **Durations** are milliseconds. A duration with an integer representation and a period that is a whole number of
  milliseconds (`milliseconds`, `seconds`, `minutes`, `hours`) is an integer. Any other (`microseconds`, `nanoseconds`,
  floating-point) is a float, which keeps `.0`: `1500us` is `1.5` and `1000000ns` is `1.0`. The JSON type depends on the
  C++ type only, so an index that first sees `1500` does not later reject `1.5`. Put the unit in the key
  (`elapsed_ms`), or convert to `std::chrono::duration<double>` first for seconds. An integer duration that does not fit
  `long long` stops at its limit instead of wrapping.
- **Time points** are written in UTC with milliseconds whatever the logger's `time_type`, rounded toward the past. A
  year outside 0000 to 9999 cannot be written as RFC 3339 and becomes `null`. Other clocks are rejected at compile
  time: convert to `system_clock` first.
- **Maps** are written in the iteration order of the map, which is the key order for `std::map` and unspecified for
  `std::unordered_map`. A `std::multimap` can write a key twice.
- `std::filesystem::path` is rejected, because it is a range of paths. Write `path.string()` or `path.u8string()`.
- A class that only converts implicitly to `bool`, `char` or `const char *` is not accepted as a value; cast it or give
  it a serializer. A class that converts to `std::string_view` is written as a string.

#### Your own types

Write a `jspdlog_serialize` function next to the type, in its namespace. It finds the type by argument-dependent lookup
and needs no JSON library:

```cpp
namespace shop
{
struct money { std::int64_t cents; std::string currency; };

inline void jspdlog_serialize(jspdlog::json_writer &w, const money &m)
{
    w.begin_object();
    w.member("cents", m.cents);
    w.member("currency", m.currency);
    w.end_object();
}
} // namespace shop

logger.info({"price", shop::money{1250, "EUR"}}, "checkout");
// ...,"price":{"cents":1250,"currency":"EUR"},"message":"checkout"}
```

For a type whose namespace you do not own (a third-party library, the standard library), specialize `jspdlog::serializer`:

```cpp
template <>
struct jspdlog::serializer<QString>
{
    static void write(jspdlog::json_writer &w, const QString &s) { w.value(s.toStdString()); }
};
```

The writer inserts commas for you: inside an object call `key` before each value, inside an array call `value` for each
element, and `member(k, v)` is `key` followed by `value`. A serializer must write exactly one value. The writer does not
check its structure in release builds, as with `raw_json`; debug builds check what is cheap to check through
`JSPDLOG_ASSERT`. A serializer takes precedence over every built-in form except scalars, so a type that is also a range
can choose its own JSON form.

Example: [09_value_types.cpp](example/09_value_types.cpp).

### Exceptions

An exception is a value. It is written as an object with its type name and message, and its nested causes (the ones
`std::throw_with_nested` adds) under `cause`, at most eight deep:

```cpp
try { db.connect(); }
catch (const std::exception &ex)
{
    logger.error({"error", ex, "host", host}, "connect failed");
}
// {...,"level":"error","error":{"type":"std::system_error","message":"Connection refused"},"host":"db1","message":"connect failed"}
```

A `catch (...)` handler has `std::current_exception()`, which is written the same way: `{"error", std::current_exception()}`.
A null pointer is `null`, and an exception that is not a `std::exception` is `{"type":"unknown"}`.

Every level and `log(level, ...)` also take the exception as the first argument, with the same endings as the other
forms. The exception is then written first among the fields of the call, under `json_options::error_key`:

```cpp
logger.error(ex);                                  // no message
logger.error(ex, "connect failed");                // a message, or a format string with arguments
logger.error(ex, {"host", host}, "connect failed"); // fields and a message
JSPDLOG_ERROR(logger, ex, {"host", host}, "connect failed");
```

The exception is held by reference and written only when the line is, so a call at a disabled level computes nothing,
not even the type name. `error_key` (default `error`), `error_type_key` (`type`) and `error_message_key` (`message`)
rename the parts for these call forms; the value form always writes `type` and `message`. A bound field with the error
key is replaced by the exception at its position. A per-call field with the error key is a mistake: debug builds report
it through `JSPDLOG_ASSERT`, and release builds write the field's value in the exception's place.

The type name is the dynamic type, demangled, and the same on GCC, Clang and MSVC for standard exceptions and your own
types in a named namespace: `std::runtime_error`, `std::system_error`, `shop::payment_error`. For the type that
`std::throw_with_nested` throws it is the type you passed. Without RTTI the type is `std::exception`, and without
RTTI or exceptions the causes are not inspected. A type with its own `jspdlog_serialize` or `serializer` is written by
that, in the value form. Example: [11_presets.cpp](example/11_presets.cpp).

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

The core header has no JSON library dependency. Serialize with yours and pass the text as `raw_json`:

```cpp
nlohmann::json items = {"alpha", "beta"};
logger.info({"items", jspdlog::raw_json{items.dump()}}, "loaded {} items", items.size());
```

The text is not validated, neither as JSON nor as UTF-8; it must be valid JSON.

#### nlohmann/json

With [`jspdlog/nlohmann.h`](include/jspdlog/nlohmann.h), `nlohmann::json`, `nlohmann::ordered_json` and every other
`nlohmann::basic_json` are values like any other, also inside containers, fields objects and bound context:

```cpp
#include <jspdlog/nlohmann.h>

logger.info({"items", items}, "loaded {} items", items.size());
// ...,"items":["alpha","beta"],"message":"loaded 2 items"}
```

- The value is dumped to compact text and not escaped again. An `ordered_json` keeps its order.
- Invalid UTF-8 in its strings is replaced by U+FFFD **whatever `JSPDLOG_UTF8_POLICY` is**, because nlohmann/json
  cannot escape such bytes or pass them through. `JSPDLOG_ESCAPE_LINE_SEPARATORS` does not apply either.
- A discarded value (a failed `parse` without exceptions) is `null`. A discarded value nested inside another one is not
  looked for and makes the line invalid.
- Include the header in every translation unit that logs such values: without it jspdlog has no JSON form for them,
  and the call does not compile.

Example: [06_nlohmann_raw_json.cpp](example/06_nlohmann_raw_json.cpp).

#### Glaze

[Glaze](https://github.com/stephenberry/glaze) needs no header of jspdlog's, only a serializer. This one writes every
type that glaze can reflect or has `glz::meta` for. It is written against glaze 9.1, which needs C++23:

```cpp
#include <jspdlog/jspdlog.h>

#include <glaze/glaze.hpp>

template <typename T>
struct jspdlog::serializer<T, std::enable_if_t<glz::reflectable<T> || glz::glaze_t<T>>>
{
    static void write(jspdlog::json_writer &w, const T &v) { w.raw(glz::write_json(v).value_or("null")); }
};
```

A serializer wins over `jspdlog_serialize`, so narrow the condition if some of those types have one. As with
`raw_json`, the text is not checked, UTF-8 included.

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

The header fields always come in the order timestamp, logger, level, severity, process, thread, source. They are
followed by the fields and then the message. With all of them removed, a line holds only the fields and the message, or
`{}`.

**Level names and severity.** `level_names` replaces the seven names, from trace to off. Without it the names are
those of spdlog, including names set with `SPDLOG_LEVEL_NAMES`. `severity_key` adds a number after the level, by
default the OpenTelemetry severity number of the level: trace 1, debug 5, info 9, warn 13, error 17, fatal 21. Change
the numbers with `severity_numbers`.

```cpp
jspdlog::json_options opts;
opts.level_names = {{"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"}};
opts.severity_key = "severity";
// {"timestamp":"...","logger":"app","level":"WARN","severity":13,...}
```

**Timestamps.** `timestamp_style` chooses how the time is written:

| `timestamp_format`        | Example                                 |
| ------------------------- | --------------------------------------- |
| `rfc3339_millis`, default | `"2026-11-23T08:00:00.123+01:00"`       |
| `rfc3339_micros`          | `"2026-11-23T08:00:00.123456+01:00"`    |
| `rfc3339_nanos`           | `"2026-11-23T08:00:00.123456789+01:00"` |
| `epoch_millis`            | `1795417200123`, a number               |

The digits below the resolution of the clock are zero: `spdlog::log_clock` is `std::chrono::system_clock`, which counts
nanoseconds with libstdc++ and libc++ and 100 ns ticks with MSVC. `epoch_millis` ignores `time_type`, because an epoch
has no zone.

### Schema presets

A preset is a function that returns a filled `json_options`. Change any member afterwards:

```cpp
auto options = jspdlog::presets::ecs();
options.source_key = "source";  // the call site is off in every preset; any text switches it on in the flat layouts
const jspdlog::json_logger logger("app", sink, options);
```

Each row shows what the preset writes. Example: [11_presets.cpp](example/11_presets.cpp).

| Preset               | Header fields                                                                                                                   | Call site (when enabled)                                                                                 | Exceptions                     | Documentation checked on 2026-11-25                                                                                                                                                                                 |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `presets::ecs()`     | `@timestamp` (UTC, ms), `log.logger`, `log.level` (lowercase), `process.pid`, `process.thread.id`                               | flat: `log.origin.file.name`, `log.origin.file.line`, `log.origin.function`                              | `error`: `type`, `message`     | [ECS base, log, process and error field sets](https://www.elastic.co/docs/reference/ecs/ecs-log). The pages show no ECS version; the names are those of ECS 8.x.                                                    |
| `presets::gcp()`     | `time` (UTC, ns), `severity` (`DEBUG`, `INFO`, `WARNING`, `ERROR`, `CRITICAL`; trace is `DEBUG`), `logger`, `process`, `thread` | set `source_key = "logging.googleapis.com/sourceLocation"` for an object with `file`, `line`, `function` | `error`: `type`, `message`     | [Structured logging](https://cloud.google.com/logging/docs/structured-logging) and [LogEntry](https://cloud.google.com/logging/docs/reference/v2/rest/v2/LogEntry).                                                 |
| `presets::datadog()` | `timestamp` (epoch ms), `logger.name`, `status`, `logger.thread_name`                                                           | object under the `source_key` you choose                                                                 | `error`: `kind`, `message`     | [Attributes and aliasing](https://docs.datadoghq.com/logs/log_configuration/attributes_naming_convention/), [status remapper](https://docs.datadoghq.com/logs/log_configuration/processors/log_status_remapper.md). |
| `presets::otel()`    | `timestamp` (UTC, ns), `logger`, `severity_text` (`TRACE` ... `FATAL`), `severity_number`, `process.pid`, `thread.id`, `body`   | flat: `code.file.path`, `code.line.number`, `code.function.name`                                         | `exception`: `type`, `message` | [Logs data model](https://opentelemetry.io/docs/specs/otel/logs/data-model/) and the [code](https://opentelemetry.io/docs/specs/semconv/registry/attributes/code/) and exception attribute registries.              |

Things to know about each:

- **ECS.** The keys are written flat and dotted, as ECS spells its field names, so `log.level` is a key with a dot in
  it, not a nested object. The ECS pages list `error.type` and `error.message`, which the exception object provides
  after the dots are expanded. ECS requires `ecs.version` in every event, and the preset cannot know which version your
  index templates use: bind it once with `with({"ecs.version", "8.11.0"})`. `log.origin.file.line` and
  `process.thread.id` are numbers, as ECS types them.
- **Google Cloud Logging.** The `severity` names are those of `LogSeverity`; `WARN` is not one of them, so warnings are
  `WARNING`, and `off` is written `DEFAULT`. The documentation types `sourceLocation.line` as an int64 in string form, and jspdlog
  writes a number, which the protobuf JSON mapping accepts for int64; this was not tried against a live agent. The
  structured logging page does not say which format `time` takes; the `LogEntry` reference defines timestamps as RFC 3339
  with up to nanoseconds, which is what the preset writes. For trace correlation see
  [Trace context](#trace-context).
- **Datadog.** The reserved `status` is remapped by its first letters, so `trace` becomes debug, `warn` warning and
  `critical` critical. `timestamp` as epoch milliseconds is a format its date remapper recognizes. `logger.thread_name` is a
  string attribute, and jspdlog has the numeric thread id only, so it writes the number; remove the key with
  `thread_key = std::nullopt` if your pipeline is strict about the type. The documentation names `error.kind`; the
  message stays `message` inside the object, so the line has `error.message` too. Only the exception call forms use
  `kind`: an exception passed as a value, `{"error", ex}`, always has `type` and `message`.
- **OpenTelemetry.** The keys are the names of the log data model (`SeverityText`, `SeverityNumber`, `Body`) in
  snake case. They are not a convention that the collector's `filelog` receiver applies by itself: configure its
  `json_parser` to move `timestamp` and `severity_text` into place. `code.file.path` is meant to be a path, and jspdlog
  writes the base name of the file. `code.function.name` is meant to be fully qualified, and jspdlog writes the name
  the compiler gives the function, which GCC and Clang leave unqualified. `thread.id` is defined as a managed thread id
  and jspdlog writes the operating system's; it and `process.pid` are not stable in the semantic conventions yet.

### Trace context

Bind the trace and span id of a request to a child logger, and every line of the request can be found from its trace.
jspdlog reads the W3C [`traceparent`](https://www.w3.org/TR/trace-context/#traceparent-header) header itself and needs
no tracing library:

```cpp
void handle(const jspdlog::json_logger &logger, const request &req)
{
    // Without a valid header the context is empty, and trace_fields binds nothing.
    const auto ctx = jspdlog::parse_traceparent(req.header("traceparent")).value_or(jspdlog::trace_context{});
    const auto log = logger.with(jspdlog::trace_fields(ctx));
    log.info("handling request");
    // {...,"trace_id":"4bf92f3577b34da6a3ce929d0e0e4736","span_id":"00f067aa0ba902b7","trace_flags":"01",...}
}
```

`parse_traceparent` accepts version `00` only: exactly `00-<32 hex>-<16 hex>-<2 hex>`, lowercase, with ids that are not
all zeros and no surrounding whitespace. W3C Trace Context suggests that a receiver also tries to read a header of a
later version; jspdlog gives an empty optional for one. A `trace_context` is a plain struct (`trace_id`, `span_id` and
`trace_flags` as bytes), so you can fill it from any tracing library.

The names and the value formats depend on the backend, so each preset schema has its own function. All ids are
lowercase hex: 32 digits for the trace id, 16 for the span id. A context that is not `valid()` gives no fields.

| Function                                  | Fields                                                                                                                                                                       | Documentation checked on 2026-11-26                                                                                                                               |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `trace_fields(ctx, keys)` (OpenTelemetry) | `trace_id`, `span_id`, `trace_flags` (two hex digits)                                                                                                                        | [Trace context in non-OTLP log formats](https://opentelemetry.io/docs/specs/otel/compatibility/logging_trace_context/)                                            |
| `presets::ecs_trace_fields(ctx)`          | `trace.id`, `span.id`                                                                                                                                                        | [ECS tracing fields](https://www.elastic.co/docs/reference/ecs/ecs-tracing)                                                                                       |
| `presets::gcp_trace_fields(ctx, project)` | `logging.googleapis.com/trace` (the trace id, or `projects/<project>/traces/<id>`), `logging.googleapis.com/spanId`, `logging.googleapis.com/trace_sampled` (`true`/`false`) | [Link logs and traces](https://cloud.google.com/trace/docs/trace-log-integration), [structured logging](https://cloud.google.com/logging/docs/structured-logging) |
| `presets::datadog_trace_fields(ctx)`      | `trace_id`, `span_id`                                                                                                                                                        | [Correlating OpenTelemetry traces and logs](https://docs.datadoghq.com/tracing/other_telemetry/connect_logs_and_traces/opentelemetry/)                            |

- **OpenTelemetry.** `trace_keys` renames the three fields, and `std::nullopt` as the `trace_flags` key leaves the flags out.
- **ECS** has no field for the flags. Its pages show lowercase hex examples and state no format.
- **Google Cloud.** `project` is optional. Google prefers the bare trace id, which is what you get without it, and
  calls the `projects/<project>/traces/<id>` form legacy. Pass a project to name the one the trace belongs to.
  `trace_sampled` is bit 0 of the flags.
- **Datadog** is given the whole 128-bit trace id in hex under the OpenTelemetry names, the format its documentation
  states for logs of such a context. Datadog's own SDKs log `dd.trace_id` and `dd.span_id`; use `trace_fields` with
  your own `trace_keys` if your pipeline expects other names.

With the OpenTelemetry C++ SDK, convert its span context. No header of jspdlog's is needed for it:

```cpp
#include <jspdlog/jspdlog.h>

#include <opentelemetry/trace/span_context.h>

jspdlog::trace_context from_otel(const opentelemetry::trace::SpanContext &sc)
{
    jspdlog::trace_context ctx;
    sc.trace_id().CopyBytesTo(ctx.trace_id);
    sc.span_id().CopyBytesTo(ctx.span_id);
    ctx.trace_flags = sc.trace_flags().flags();
    return ctx;
}

// With an active span:
const auto log = logger.with(jspdlog::trace_fields(from_otel(span->GetContext())));
```

Example: [13_trace_context.cpp](example/13_trace_context.cpp).

### Text encoding

Every string jspdlog writes goes through one rule for invalid UTF-8: keys, string values, messages, logger names, level
names, file and function names, and the payload of a message logged directly on the `spdlog::logger`. The rule is a
compile-time setting, `JSPDLOG_UTF8_POLICY`. A `fields` object and bound context are serialized before any logger sees
them, so a per-logger option could not cover them, and the same data must give the same line as a braced list.

| Policy                          | Invalid bytes                                                                                                                            |
| ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `JSPDLOG_UTF8_REPLACE`, default | One U+FFFD for each maximal invalid subsequence, as the Unicode standard recommends. WHATWG and Python's `errors="replace"` do the same. |
| `JSPDLOG_UTF8_ESCAPE`           | Each invalid byte as a `\u00XX` escape. A consumer reads U+00XX, not the original byte.                                                  |
| `JSPDLOG_UTF8_PASS`             | Copied unchanged. The line is then not valid JSON for strict parsers. Costs what it cost before 0.4.                                     |

Invalid are the bytes C0, C1 and F5 to FF, overlong forms, surrogates, code points above U+10FFFF, truncated sequences
and lone continuation bytes. With CMake, set the cache variable, for example `-DJSPDLOG_UTF8_POLICY=escape`: the
`jspdlog::jspdlog` target, installed or not, passes the value on to everything that links it. Without CMake, define
`JSPDLOG_UTF8_POLICY` to one of the three macros before including the header. Use the same value in every translation
unit: inline functions of the header read it, so a mismatch breaks the one-definition rule. This differs from
`JSPDLOG_ACTIVE_LEVEL`, which only the level macros read.

U+2028 and U+2029 are valid in JSON but end a line in JavaScript source before ES2019. Set
`JSPDLOG_ESCAPE_LINE_SEPARATORS` (`ON` in CMake, or define it to 1) to write them as the JSON escapes `\u2028` and
`\u2029`.

`raw_json` is not checked: it is your JSON by contract, UTF-8 included. Values written through `jspdlog/nlohmann.h`
always get replacement (see [nlohmann/json](#nlohmannjson)). The payload protocol is ASCII.

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

[`make_async_logger`](#factories) works with both spdlog versions and owns its worker. To build the async part
yourself: spdlog v2 makes async a sink; spdlog v1 makes it a logger type, which you wrap with `adopt()`:

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
- the default error handler, which writes plain text to stderr; `make_error_handler` writes JSON
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
stderr is collected together with your JSON logs, install the handler that jspdlog makes:

```cpp
logger.spdlog_logger()->set_error_handler(jspdlog::make_error_handler(logger.name()));
// {"timestamp":"...","logger":"app","level":"error","dropped":3,"message":"Failed writing to file ..."}
```

`make_error_handler` writes each error as one JSON line to stderr, or to another stream or sink through
`jspdlog::error_report_options`. It never logs through spdlog, so a sink that fails again cannot recurse or deadlock:
spdlog v2 calls a handler with its error mutex held, and a handler that logs through the failing logger would wait on
that mutex forever, while in v1 it would recurse until the stack ends. The handler does not throw; if the report itself
fails it prints one plain-text line to stderr. Reports closer together than `min_interval` (one second by default) are
counted, and the next one carries the number as `dropped`. Its state lives in the handler, which shares it between
copies, and a mutex of its own serializes the worker threads of an async logger. With a report sink, give it a
`json_formatter` (`install_json_formatter(*sink)`); the report sink must not be one of the failing logger's sinks.

- The handler belongs to the `spdlog::logger`, so it applies to every copy and child of the json_logger.
- **spdlog v2 async:** errors on the worker thread go to the `async_sink`'s own handler, set through
  `async_sink::config::custom_err_handler`, not to the logger's.
- **spdlog v1 async:** an async logger holds only a weak reference to its thread pool. After `spdlog::shutdown()`, or
  after `spdlog::init_thread_pool()` replaces the pool, every call fails with "thread pool doesn't exist anymore".
  Keep the pool alive until you stop logging. A logger made by [`make_async_logger`](#factories) owns its pool, and
  has this handler already, for both spdlog versions, unless `json_errors` is off.
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

| Setting                 | Effect on jspdlog                                                                  |
| ----------------------- | ---------------------------------------------------------------------------------- |
| `SPDLOG_LEVEL_NAMES`    | Changes the `level` values. The defaults are `trace` ... `warning` ... `critical`. |
| `SPDLOG_ACTIVE_LEVEL`   | No effect on jspdlog's calls and macros; it only strips spdlog's macros.           |
| `JSPDLOG_ACTIVE_LEVEL`  | jspdlog's own: removes the `JSPDLOG_*` macros below a level. Default: trace.       |
| `SPDLOG_NO_THREAD_ID`   | `thread` is always 0.                                                              |
| `SPDLOG_EOL`            | Used when `json_options::eol` is not set.                                          |
| `SPDLOG_USE_STD_FORMAT` | Supported with spdlog v1 and C++20 (spdlog v2 has no such mode). See below.        |
| `SPDLOG_NO_EXCEPTIONS`  | Not supported; jspdlog's constructors throw on a null sink or logger.              |

With `SPDLOG_USE_STD_FORMAT`, which needs spdlog v1 built with `-DSPDLOG_USE_STD_FORMAT=ON` and C++20, jspdlog uses
`std::format` where it otherwise uses {fmt}: a format string is checked at compile time where the standard library has
`std::format_string` (`__cpp_lib_format` 202207L), and at run time before that. A format string that is not a literal
cannot be passed with `fmt::runtime`, which has no `std::format` counterpart before C++26; pass the runtime string as the
message, or build the text yourself. To write an enum as its name, specialize `std::formatter` for it instead of
`fmt::formatter`.

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
    void info(Fields f) const;
    void info(Fields f, format_here_t<Args...> fmt, Args &&...args) const;
    void info(Fields f, const S &msg) const;
    // Fields: a braced list {"key", value, ...} of at most 16 pairs, serialized when the line is written and with
    // callables allowed as values, or a jspdlog::fields object.

    // Each level and log(level, ...) also take an exception first, with the same five endings and a sixth, none:
    void info(const E &ex) const;                           // E: anything derived from std::exception
    void info(const E &ex, format_here_t<Args...> fmt, Args &&...args) const;
    void info(const E &ex, const S &msg) const;
    void info(const E &ex, Fields f) const;
    void info(const E &ex, Fields f, format_here_t<Args...> fmt, Args &&...args) const;
    void info(const E &ex, Fields f, const S &msg) const;

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
    std::optional<std::array<std::string, 7>> level_names;  // trace to off; nullopt: spdlog's names
    std::optional<std::string> severity_key;                 // number after the level; off by default
    std::array<int, 7> severity_numbers = {1, 5, 9, 13, 17, 21, 0};
    std::optional<std::string> process_key = "process";
    std::optional<std::string> thread_key = "thread";
    std::optional<std::string> source_key;  // {"file":...,"line":...,"function":...} when known; off by default
    std::string source_file_key = "file";   // the members of that object, or the fields of the line with source_flat
    std::string source_line_key = "line";
    std::string source_function_key = "function";
    bool source_flat = false;               // write them as fields of the line; source_key then only switches them on
    std::string message_key = "message";
    std::string error_key = "error";        // where the exception of an exception call goes
    std::string error_type_key = "type";
    std::string error_message_key = "message";
    timestamp_format timestamp_style = timestamp_format::rfc3339_millis;  // or _micros, _nanos, epoch_millis
    spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local;
    std::optional<std::string> eol;  // nullopt: spdlog's default
};

// Filled json_options for common schemas: change any member afterwards.
namespace presets {
json_options ecs();      // Elastic Common Schema
json_options gcp();      // Google Cloud Logging
json_options datadog();  // Datadog
json_options otel();     // OpenTelemetry log data model

// The trace context under the names of a schema; OpenTelemetry's are those of trace_fields.
fields ecs_trace_fields(const trace_context &ctx);
fields gcp_trace_fields(const trace_context &ctx, std::string_view project_id = {});
fields datadog_trace_fields(const trace_context &ctx);
}  // namespace presets

// W3C Trace Context.
struct trace_context {
    std::array<std::uint8_t, 16> trace_id{};
    std::array<std::uint8_t, 8> span_id{};
    std::uint8_t trace_flags = 0;
    bool valid() const noexcept;  // both ids non-zero
};
std::optional<trace_context> parse_traceparent(std::string_view header) noexcept;  // version 00
struct trace_keys {
    std::string trace_id = "trace_id";
    std::string span_id = "span_id";
    std::optional<std::string> trace_flags = "trace_flags";  // nullopt leaves them out
};
fields trace_fields(const trace_context &ctx, const trace_keys &keys = {});  // empty when not valid

class fields {
public:
    fields(K &&key, V &&value, Rest &&...rest);  // key, value, key, value, ...
    template <typename T> void insert(std::string_view key, const T &value);  // any type with a JSON form
    void merge(const fields &other);  // other wins on collisions
    friend fields operator+(fields a, const fields &b);
    bool empty() const;
    std::size_t size() const;
    bool contains(std::string_view key) const;
    void clear();
    std::string to_json() const;  // {"key":value,...} in insertion order
};

struct raw_json { std::string value; };

// A field key checked at compile time (C++20; when created in C++17): no escape scan when it is written.
class key {
public:
    template <std::size_t N> explicit consteval key(const char (&s)[N]);
    constexpr std::string_view view() const noexcept;
};
namespace literals { consteval key operator""_key(const char *s, std::size_t n); }  // "user_id"_key

// Reports a logging failure as one JSON line, without calling back into spdlog. Install it with set_error_handler
// (or in the config of a v2 async_sink). Rate limited; the next report carries the number dropped.
struct error_report_options {
    json_options format{};                        // keys and time format of the report line
    spdlog::sink_ptr sink;                        // where to write it (give it a json_formatter), or null for stream
    std::FILE *stream = stderr;                   // used when sink is null; null too discards the reports
    std::chrono::milliseconds min_interval{1000};
};
spdlog::err_handler make_error_handler(std::string logger_name, error_report_options options = {});

// Optional macros: JSPDLOG_TRACE, _DEBUG, _INFO, _WARN, _ERROR, _CRITICAL (logger, ...) and
// JSPDLOG_LOG(logger, level, ...). Nothing in the call is evaluated when the level is off; JSPDLOG_ACTIVE_LEVEL
// removes the ones below a level at compile time.

// Writes one JSON value. Passed to your serializers; fields uses it for every value.
class json_writer {
public:
    explicit json_writer(std::string &out) noexcept;
    template <typename T> void value(const T &v);  // any type with a JSON form, recursively
    void raw(std::string_view json);               // pre-serialized JSON; empty is null
    void begin_object();
    void key(std::string_view k);                  // inside an object, before each value
    void end_object();
    void begin_array();
    void end_array();
    template <typename T> void member(std::string_view k, const T &v);  // key(k), then value(v)
};

// Specialize to give a type you do not own a JSON form. Your own types: a jspdlog_serialize function found by ADL.
template <typename T, typename Enable = void> struct serializer {};
// void jspdlog_serialize(jspdlog::json_writer &w, const T &v);  // next to T, in its namespace

// Renders every spdlog message as one JSON object. One instance per sink.
class json_formatter final : public spdlog::formatter {
public:
    explicit json_formatter(json_options options = {});
    void format(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest) override;
    std::unique_ptr<spdlog::formatter> clone() const override;
};

void install_json_formatter(spdlog::logger &logger, const json_options &options = {});
void install_json_formatter(spdlog::sinks::sink &sink, const json_options &options = {});

// <jspdlog/factories.h>
json_logger make_stdout_logger(std::string name, const json_options &options = {});
json_logger make_stderr_logger(std::string name, const json_options &options = {});
json_logger make_rotating_file_logger(std::string name, const spdlog::filename_t &path, std::size_t max_size,
                                      std::size_t max_files, const json_options &options = {});
enum class overflow_policy { block, overrun_oldest, discard_new };
struct async_options {
    std::size_t queue_size = 8192;
    overflow_policy overflow = overflow_policy::block;
    bool json_errors = true;
};
// Owns its worker thread. Throws on a null sink, a queue size that spdlog cannot take or a policy it does not have.
json_logger make_async_logger(std::string name, std::vector<spdlog::sink_ptr> sinks, const async_options &async = {},
                              const json_options &options = {});

}  // namespace jspdlog

// <jspdlog/nlohmann.h> specializes jspdlog::serializer for every nlohmann::basic_json.
```

## Guarantees and limits

Every line written by a `json_formatter` is one valid JSON object and valid UTF-8, whoever logged it, as long as
`raw_json` contents are valid JSON and UTF-8 and the UTF-8 policy is not `pass`. These can still produce invalid or
surprising output:

- sinks that write the raw message instead of the formatted line, and `set_pattern()` or `set_formatter()` on a sink
  that replaces the formatter (see [Working with spdlog](#working-with-spdlog));
- field keys equal to a header key or the message key (duplicate keys);
- bytes that are not valid UTF-8 under `JSPDLOG_UTF8_PASS`, which are copied unchanged, and in `raw_json`, which is
  never checked (see [Text encoding](#text-encoding));
- two different invalid keys of one object that are replaced by the same text, which then appear twice;
- a serializer that writes more or less than one value, or leaves an object or array open: release builds do not check;
- a discarded nlohmann/json value nested inside another one, logged through `jspdlog/nlohmann.h`;
- time points outside the years 0000 to 9999, which are `null`, and maps, which are written in iteration order;
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
