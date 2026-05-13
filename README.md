# jspdlog

JSON-line logging on top of [spdlog](https://github.com/gabime/spdlog) v2.

`jspdlog` is a tiny, single-header C++17 library that wraps any
`spdlog::logger` so that every log call produces exactly one valid JSON
object per line. The JSON pattern is pinned at construction time — there is
no API for changing it — which makes it structurally impossible to emit
malformed JSON.

```json
{"timestamp":"2026-05-13T09:00:00.000+02:00","logger":"app","level":"info","process":1234,"thread":5678,"user_id":42,"message":"hello world"}
```

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
- **Works with every spdlog sink.** Rotating file, daily, async, syslog,
  Windows event log, custom sinks — any `spdlog::sink_ptr` is accepted.
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

// Adopt an existing spdlog::logger (the JSON pattern is re-applied).
static json_logger adopt(std::shared_ptr<spdlog::logger> logger);

// Logging - same overload set for trace/debug/info/warn/error/critical.
void info(spdlog::format_string_t<Args...> fmt, Args&&... args);  // formatted message
void info(const T& msg);                                          // any fmt-formattable value
void info(const json_properties& props,
          spdlog::format_string_t<Args...> fmt, Args&&... args);  // props + formatted message
void info(const json_properties& props, const T& msg);            // props + value
void info(const json_properties& props);                          // properties only, no message

// Property binding.
json_logger with_properties(json_properties props) const;         // returns a child logger
void set_internal_logger(json_logger internal);                   // forward spdlog errors

// spdlog passthroughs.
const std::string& name() const;
spdlog::level level() const;
void set_level(spdlog::level level);
void flush();
void flush_on(spdlog::level level);

// Escape hatch. Do NOT call set_pattern() on this.
const std::shared_ptr<spdlog::logger>& spdlog_logger() const;
```

### `jspdlog::json_properties`

```cpp
json_properties();
json_properties(key1, value1, key2, value2, ...);                 // variadic key/value pairs

// Typed inserts (one per scalar type).
void insert(const std::string& key, std::nullptr_t);
void insert(const std::string& key, std::string_view value);
void insert(const std::string& key, bool value);
void insert(const std::string& key, int|long|long long|unsigned... value);
void insert(const std::string& key, float|double value);
void insert(const std::string& key, const raw_json& value);
template <typename T>
void insert(const std::string& key, const T* value);              // null -> null, otherwise *value

void merge(const json_properties& other);                         // other overrides this
void merge(json_properties&& other);

bool empty() const;
std::string to_string() const;                                    // serialized fragment, leading ','
```

`json_properties` overloads `operator+` for composition (`a + b` returns the
merge with `b`'s values winning on collisions).

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

| | `spdlog::logger` directly | `jspdlog::json_logger` |
|---|---|---|
| Output format | Whatever pattern you set | Always one JSON object per line |
| Pattern footgun | Easy to break with `set_pattern` | API forbids it |
| Structured fields | Stringify into the message yourself | First-class `json_properties` + `raw_json` |
| Sinks | Any | Any (delegates) |
| Levels, async, formatters, error handlers | Yes | Yes (delegates) |
| Source location capture | Yes | Not yet — open issue if you need it |

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
strings are copied.

**Is this header-only?**  
Yes. No `.cpp` files, no `JSPDLOG_COMPILED_LIB` mode, no link step beyond
what spdlog already requires.

**Which C++ standard?**  
C++17 minimum. Tested with C++17 and C++20 in CI.

**Which spdlog version?**  
spdlog v2.x. The v2 line is currently maintained on the `v2.x` branch and
is not yet tagged; the bundled CMake config pins a known-good commit.

## License

[MIT](LICENSE).
