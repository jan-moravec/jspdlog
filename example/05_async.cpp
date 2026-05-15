// Async logging: spdlog v2 exposes async as a wrapping sink. Hand it to
// jspdlog like any other sink and you get one JSON line per call,
// queued for the worker thread.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/async_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main()
{
    spdlog::sinks::async_sink::config cfg;
    cfg.sinks = {std::make_shared<spdlog::sinks::stdout_color_sink_mt>()};
    cfg.policy = spdlog::sinks::async_sink::overflow_policy::block;

    auto async = std::make_shared<spdlog::sinks::async_sink>(cfg);
    jspdlog::json_logger logger("async", std::move(async));

    logger.info({"queued", true}, "hello from async sink");
    logger.flush();
    return 0;
}
