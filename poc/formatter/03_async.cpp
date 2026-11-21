// PoC 3: async logging without spdlog's registry or global thread pool.
// Formatting runs on the worker thread; the token and the serialized
// properties travel inside the copied log_msg payload.

#include "jspdlog_formatter.h"

#include <spdlog/sinks/stdout_sinks.h>

#if JSPDLOG_SPDLOG_V2
#include <spdlog/sinks/async_sink.h>
#else
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#endif

#include <thread>
#include <vector>

int main()
{
    jspdlog::poc::formatter_options opts;
    opts.eol = "\n";
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();

#if JSPDLOG_SPDLOG_V2
    // v2: async is a sink that owns its worker thread.
    spdlog::sinks::async_sink::config cfg;
    cfg.sinks = {stdout_sink};
    jspdlog::poc::json_logger logger("async", std::make_shared<spdlog::sinks::async_sink>(cfg), opts);
#else
    // v1: a thread pool we own (the logger only holds a weak_ptr to it, so it
    // must outlive the logging below), instead of spdlog's global pool.
    auto pool = std::make_shared<spdlog::details::thread_pool>(8192, 1);
    auto logger = jspdlog::poc::json_logger::adopt(
        std::make_shared<spdlog::async_logger>("async", stdout_sink, pool, spdlog::async_overflow_policy::block), opts
    );
#endif

    std::vector<std::thread> workers;
    for (int id = 0; id < 3; ++id)
    {
        workers.emplace_back([logger, id] {
            auto worker = logger.with_properties({"worker", id});
            for (int i = 0; i < 2; ++i)
            {
                worker.info({"i", i}, "tick");
            }
        });
    }
    for (auto &t : workers)
    {
        t.join();
    }
    logger.flush();
    return 0;
}
