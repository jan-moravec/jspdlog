// The call site is captured by every logging call, without macros. Set source_key to write it. A function that logs
// for its caller passes the caller's call site on with at().

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <string_view>

namespace
{

void charge_card(const jspdlog::json_logger &log)
{
    log.info({"amount", 12.5}, "charging");
}

// The default argument records the place of each call of audit.
void audit(const jspdlog::json_logger &log, std::string_view what, jspdlog::call_site where = {})
{
    log.at(where).info({"what", what}, "audit");
}

void refund(const jspdlog::json_logger &log)
{
    audit(log, "refund");
}

} // namespace

int main()
{
    jspdlog::json_options options;
    options.source_key.emplace("source");
    const jspdlog::json_logger logger("pay", std::make_shared<spdlog::sinks::stdout_color_sink_mt>(), options);

    // {"timestamp":...,"logger":"pay","level":"info",...,"source":{"file":"08_source_location.cpp","line":15,
    //  "function":"charge_card"},"amount":12.5,"message":"charging"}
    charge_card(logger);
    // The line and function of the call in refund, not those inside audit:
    // {...,"source":{"file":"08_source_location.cpp","line":26,"function":"refund"},"what":"refund","message":"audit"}
    refund(logger);
    return 0;
}
