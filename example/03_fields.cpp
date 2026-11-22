// Bound fields on a parent logger, plus per-call extras. Fields keep the order they were first written in.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

int main()
{
    const jspdlog::json_logger root("svc", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    // with() returns a child logger that shares the underlying spdlog logger and sinks, but carries bound fields.
    const auto request_logger = root.with({"request_id", "abc-123", "user_id", 42});

    request_logger.info("request started");
    request_logger.warn({"latency_ms", 312}, "slow path hit");
    request_logger.info({"items", 7}, "completed");
    return 0;
}
