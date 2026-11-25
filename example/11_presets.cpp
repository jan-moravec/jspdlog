// Schema presets and exceptions: the same events written with the field names of Elastic ECS and of OpenTelemetry, and
// an exception with its cause.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_sinks.h>

#include <exception>
#include <memory>
#include <stdexcept>

namespace
{

// Connecting fails, and the reason is the cause of the error.
void connect()
{
    try
    {
        throw std::runtime_error("connection refused");
    }
    catch (...)
    {
        std::throw_with_nested(std::runtime_error("cannot reach the database"));
    }
}

void log_failure(const jspdlog::json_logger &logger)
{
    try
    {
        connect();
    }
    catch (const std::exception &ex)
    {
        // The exception goes first and is written under the key of the schema, with its causes.
        logger.error(ex, {"host", "db1"}, "connect failed");
    }
}

} // namespace

int main()
{
    const auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();

    // Elastic Common Schema. The presets leave the call site off; this also writes it, as log.origin.*.
    auto ecs = jspdlog::presets::ecs();
    ecs.source_key = "source";
    const jspdlog::json_logger ecs_logger("app", sink, ecs);
    // Fields that belong to every line are bound once.
    const jspdlog::json_logger ecs_service = ecs_logger.with({"ecs.version", "8.11.0", "service.name", "orders"});
    ecs_service.info({"user", 42}, "order placed");
    log_failure(ecs_service);

    // OpenTelemetry log data model, as read by a collector's filelog receiver.
    const jspdlog::json_logger otel_logger("app", sink, jspdlog::presets::otel());
    otel_logger.info({"user", 42}, "order placed");
    log_failure(otel_logger);
    return 0;
}
