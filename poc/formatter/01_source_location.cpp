// PoC 1: call sites and nested context without macros or globals.
//
// Inner code receives a logger, adds its own context with with_properties()
// and passes the child down. Every call records its file/line/function via
// defaulted arguments.

#include "jspdlog_formatter.h"

#include <spdlog/sinks/stdout_sinks.h>

using jspdlog::poc::json_logger;

namespace
{

void charge_card(const json_logger &log, int amount)
{
    auto payment = log.with_properties({"step", "charge", "amount", amount});
    payment.info("charging card");
    payment.warn({"retry", 1}, "gateway slow: {} ms", 312);
}

void handle_order(const json_logger &log, const char *order_id)
{
    auto order = log.with_properties({"order_id", order_id});
    order.info("order received");
    charge_card(order, 42);
    order.info({"status", "done"});
}

} // namespace

int main()
{
    jspdlog::poc::formatter_options opts;
    opts.process_key = std::nullopt;
    opts.thread_key = std::nullopt;
    opts.eol = "\n";
    const json_logger root("shop", std::make_shared<spdlog::sinks::stdout_sink_mt>(), opts);

    auto request = root.with_properties({"request_id", "abc-123", "user", 7});
    handle_order(request, "A-1001");

    // An inner scope overriding an outer key keeps the key's position.
    request.with_properties({"user", 8}).info("impersonating");

    // Renamed message key, UTC timestamps, no source.
    jspdlog::poc::formatter_options compact;
    compact.process_key = std::nullopt;
    compact.thread_key = std::nullopt;
    compact.source_key = std::nullopt;
    compact.message_key = "msg";
    compact.time_type = spdlog::pattern_time_type::utc;
    compact.eol = "\n";
    json_logger small("compact", std::make_shared<spdlog::sinks::stdout_sink_mt>(), compact);
    small.error({"code", 7}, "failed");
    return 0;
}
