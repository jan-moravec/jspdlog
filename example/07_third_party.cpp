// Library code that logs through plain spdlog next to a json_logger. install_json_formatter makes the plain logger
// write JSON lines too, so the whole output is one JSON object per line.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace
{

// Stands in for a third-party library that only knows spdlog.
void library_function(spdlog::logger &logger)
{
    logger.info("library says {} with \"quotes\"", "hello");
    logger.warn("two\nlines");
}

} // namespace

int main()
{
    const auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
    jspdlog::json_options options;
    options.eol = "\n";
    options.source_key = "source";

    const jspdlog::json_logger app("app", sink, options);

    // The plain logger shares the sink, so installing the formatter on it also covers the json_logger's lines.
    spdlog::logger library("library", sink);
    jspdlog::install_json_formatter(library, options);

    app.info({"user_id", 42}, "started");
    library_function(library);
    SPDLOG_LOGGER_INFO(&library, "with a call site");
    app.info({"user_id", 42}, "done");
    return 0;
}
