// Async logging. Formatting happens on spdlog's worker thread; jspdlog has
// already serialized the fields into the message payload, so nothing
// captured by reference can dangle.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#if JSPDLOG_SPDLOG_V2
#include <spdlog/sinks/async_sink.h>
#else
#include <spdlog/async.h>
#include <spdlog/spdlog.h>
#endif

int main()
{
#if JSPDLOG_SPDLOG_V2
    // spdlog v2: async is a wrapping sink.
    spdlog::sinks::async_sink::config cfg;
    cfg.sinks = {std::make_shared<spdlog::sinks::stdout_color_sink_mt>()};
    cfg.policy = spdlog::sinks::async_sink::overflow_policy::block;
    jspdlog::json_logger logger("async", std::make_shared<spdlog::sinks::async_sink>(cfg));
#else
    // spdlog v1: async is a logger type; wrap it with adopt().
    auto logger = jspdlog::json_logger::adopt(spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("async"));
#endif

    logger.info({"queued", true}, "hello from the async path");
    logger.flush();
#if !JSPDLOG_SPDLOG_V2
    spdlog::shutdown();
#endif
    return 0;
}
