// The call site is captured by every logging call, without macros. Set source_key to write it.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

namespace
{
void charge_card(const jspdlog::json_logger &log)
{
    log.info({"amount", 12.5}, "charging");
}
} // namespace

int main()
{
    jspdlog::json_options options;
    options.source_key = "source";
    const jspdlog::json_logger logger("pay", std::make_shared<spdlog::sinks::stdout_color_sink_mt>(), options);

    // {"timestamp":...,"logger":"pay","level":"info",...,"source":{"file":"08_source_location.cpp","line":10,
    //  "function":"charge_card"},"amount":12.5,"message":"charging"}
    charge_card(logger);
    return 0;
}
