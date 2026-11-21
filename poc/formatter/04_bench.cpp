// PoC 4: rough single-thread throughput of the formatting path.
//
// The sink formats every message into a buffer and discards it, so the
// numbers measure jspdlog's serialization plus spdlog's formatter, not I/O.
// Run a Release build; treat the result as a ratio, not an absolute figure.

#include <jspdlog/jspdlog.h>

#include "jspdlog_formatter.h"

#include <spdlog/sinks/base_sink.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>

namespace
{

class formatting_null_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    std::size_t bytes = 0;

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        bytes += buf.size();
    }
    void flush_() override {}
};

void run(const char *label, int iterations, const std::function<void(int)> &body)
{
    for (int i = 0; i < iterations / 10; ++i) // warm-up
    {
        body(i);
    }
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        body(i);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
    std::printf("%-44s %7.1f ns/msg\n", label, static_cast<double>(ns.count()) / iterations);
}

} // namespace

int main()
{
    constexpr int iterations = 1'000'000;

    {
        auto sink = std::make_shared<formatting_null_sink>();
        spdlog::logger logger("plain", sink);
        logger.set_pattern("[%Y-%m-%dT%H:%M:%S.%e%z] [%n] [%l] [%P] [%t] %v");
        run("spdlog pattern, plain text (reference)", iterations, [&](int i) {
            logger.info("request {} done user_id={} ok={}", i, 42, true);
        });
    }
    {
        auto sink = std::make_shared<formatting_null_sink>();
        jspdlog::json_logger logger("pattern", sink);
        auto bound = logger.with_properties({"service", "api"});
        run("jspdlog (pattern), bound + per-call props", iterations, [&](int i) {
            bound.info({"user_id", 42, "ok", true}, "request {} done", i);
        });
        run("jspdlog (pattern), message only", iterations, [&](int i) { logger.info("request {} done", i); });
    }
    {
        auto sink = std::make_shared<formatting_null_sink>();
        jspdlog::poc::formatter_options no_source;
        no_source.source_key = std::nullopt;
        jspdlog::poc::json_logger logger("formatter", sink, no_source);
        auto bound = logger.with_properties({"service", "api"});
        run("PoC (formatter), bound + per-call props", iterations, [&](int i) {
            bound.info({"user_id", 42, "ok", true}, "request {} done", i);
        });
        run("PoC (formatter), message only", iterations, [&](int i) { logger.info("request {} done", i); });
    }
    {
        auto sink = std::make_shared<formatting_null_sink>();
        jspdlog::poc::json_logger logger("formatter", sink); // default: with "source"
        auto bound = logger.with_properties({"service", "api"});
        run("PoC (formatter) + source, bound + per-call", iterations, [&](int i) {
            bound.info({"user_id", 42, "ok", true}, "request {} done", i);
        });
        run("PoC (formatter) + source, message only", iterations, [&](int i) { logger.info("request {} done", i); });
    }
    return 0;
}
