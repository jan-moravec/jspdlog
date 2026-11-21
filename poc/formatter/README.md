# Proof of concept: jspdlog on a custom `spdlog::formatter`

Not part of the installed library. It exists to evaluate the design proposed
in [ROADMAP.md](../../ROADMAP.md) before the real header is rewritten.

| File                                                 | Shows                                                                                                                               |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| [`jspdlog_formatter.h`](jspdlog_formatter.h)         | `json_formatter`, insertion-ordered `json_properties`, `json_logger` with shared child context and macro-free call sites            |
| [`01_source_location.cpp`](01_source_location.cpp)   | Inner functions adding context with `with_properties()`; file/line/function captured without macros                                 |
| [`02_foreign_messages.cpp`](02_foreign_messages.cpp) | Direct `spdlog::logger` calls: broken JSON with today's pattern approach, escaped `"message"` with the formatter; injection attempt |
| [`03_async.cpp`](03_async.cpp)                       | Async on v1 (own thread pool, no registry) and v2 (`async_sink`)                                                                    |
| [`04_bench.cpp`](04_bench.cpp)                       | Formatting throughput vs. plain spdlog and the current header                                                                       |
| [`test_poc.cpp`](test_poc.cpp)                       | Ordering, overrides, call-site capture, foreign messages                                                                            |

Build with the main project (`JSPDLOG_BUILD_POC`, on by default at top level):

```sh
cmake -S . -B build -DJSPDLOG_SPDLOG_GIT_TAG=v1.17.0   # or leave the default (v2.x)
cmake --build build --config Release
build/poc/formatter/Release/poc_formatter_01_source_location
```

Example line from `01_source_location`:

```json
{"timestamp":"2026-11-21T16:35:21.199+01:00","logger":"shop","level":"warning","source":{"file":"...01_source_location.cpp","line":20,"function":"charge_card"},"request_id":"abc-123","user":7,"order_id":"A-1001","step":"charge","amount":42,"retry":1,"message":"gateway slow: 312 ms"}
```
