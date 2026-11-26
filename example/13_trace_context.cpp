// Trace context: parse the traceparent header of a request and bind it to a child logger, so that every line of the
// request can be found from its trace. The presets have a form for each log backend.

#include <jspdlog/factories.h>

#include <string_view>

namespace
{

// The header would come from the HTTP request; it is not always there, or not always well formed.
void handle(const jspdlog::json_logger &logger, std::string_view traceparent)
{
    if (const auto ctx = jspdlog::parse_traceparent(traceparent))
    {
        // trace_id, span_id and trace_flags, the names of OpenTelemetry, on every line of the request.
        const auto request_logger = logger.with(jspdlog::trace_fields(*ctx));
        request_logger.info("handling request");
        return;
    }
    logger.info("handling request without a trace");
}

} // namespace

int main()
{
    constexpr std::string_view HEADER = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
    const auto logger = jspdlog::make_stdout_logger("app");
    handle(logger, HEADER);
    handle(logger, "not a traceparent");

    // The names and value formats of one backend, together with its preset.
    if (const auto ctx = jspdlog::parse_traceparent(HEADER))
    {
        namespace presets = jspdlog::presets;
        const auto ecs = jspdlog::make_stdout_logger("app", presets::ecs());
        ecs.with(presets::ecs_trace_fields(*ctx)).info("Elastic Common Schema");

        const auto gcp = jspdlog::make_stdout_logger("app", presets::gcp());
        gcp.with(presets::gcp_trace_fields(*ctx)).info("Google Cloud Logging");

        const auto datadog = jspdlog::make_stdout_logger("app", presets::datadog());
        datadog.with(presets::datadog_trace_fields(*ctx)).info("Datadog");
    }
    return 0;
}
