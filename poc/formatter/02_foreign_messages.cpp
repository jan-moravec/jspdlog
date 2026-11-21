// PoC 2: messages that bypass jspdlog.
//
// Real programs have code that logs through the spdlog::logger directly:
// libraries using the default logger, code holding the adopted logger, or
// `spdlog_logger()->info(...)`. Compare what the current pattern-based
// jspdlog and the formatter-based PoC produce for the same calls.

#include <jspdlog/jspdlog.h>

#include "jspdlog_formatter.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>

namespace
{

void heading(const char *text)
{
    std::fflush(stdout);
    std::printf("\n--- %s\n", text);
    std::fflush(stdout);
}

} // namespace

int main()
{
    auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
    const char *tricky = R"(user said "hi"\ and left)";

    heading("current jspdlog (pattern): direct spdlog call breaks the line");
    {
        jspdlog::json_options opts;
        opts.eol = "\n";
        jspdlog::json_logger logger("pattern", sink, opts);
        logger.info({"k", 1}, "through jspdlog");
        logger.spdlog_logger()->info("direct: {}", tricky); // unescaped quotes, no leading comma
    }

    heading("PoC (formatter): the same direct call is escaped into \"message\"");
    {
        jspdlog::poc::formatter_options opts;
        opts.eol = "\n";
        jspdlog::poc::json_logger logger("formatter", sink, opts);
        logger.info({"k", 1}, "through jspdlog");
        logger.spdlog_logger()->info("direct: {}", tricky);
        // Attempted injection: a payload that looks like a property fragment.
        logger.spdlog_logger()->info(R"(,"level":"critical","admin":true)");
    }

    heading("PoC: JSON output for code you don't control (default logger)");
    {
        jspdlog::poc::formatter_options opts;
        opts.eol = "\n";
        auto default_logger = std::make_shared<spdlog::logger>("default", sink);
#if JSPDLOG_SPDLOG_V2
        spdlog::set_global_logger(default_logger); // v2 name
#else
        spdlog::set_default_logger(default_logger);
#endif
        jspdlog::poc::install_json_formatter(*default_logger, opts);
        spdlog::warn("third-party library says {}", "hello");
        SPDLOG_ERROR("macro call with source location"); // spdlog's own macro passes source_loc
    }
    return 0;
}
