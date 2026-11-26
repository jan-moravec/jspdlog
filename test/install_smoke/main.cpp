// Smallest possible consumer of the installed jspdlog package. The test is "this file builds and runs", plus two
// checks: the compile definitions of the package reach the consumer (the default package replaces invalid UTF-8), and
// the consumer can set JSPDLOG_ACTIVE_LEVEL for its own target.

#include <jspdlog/factories.h>
#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/ostream_sink.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <sstream>
#include <string>

#if JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_REPLACE || JSPDLOG_ESCAPE_LINE_SEPARATORS != 0
#error "the installed package must carry the default text policy to its consumers"
#endif

static_assert(JSPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_INFO, "the consumer's JSPDLOG_ACTIVE_LEVEL must reach the header");

int main()
{
    try
    {
        std::ostringstream out;
        const jspdlog::json_logger logger("install_smoke", std::make_shared<spdlog::sinks::ostream_sink_mt>(out));
        logger.info(jspdlog::fields{"ok", true}, "hello {}", "world");
        logger.info("bad \xFF byte");
        // The logger writes every level, so only JSPDLOG_ACTIVE_LEVEL can remove this call.
        logger.spdlog_logger()->set_level(jspdlog::level::trace);
        JSPDLOG_DEBUG(logger, "removed at compile time");
        const std::string text = out.str();
        if (text.find("removed at compile time") != std::string::npos)
        {
            (void)std::fputs("a call below JSPDLOG_ACTIVE_LEVEL was written\n", stderr);
            return 1;
        }
        if (text.find("bad \xEF\xBF\xBD byte") == std::string::npos)
        {
            (void)std::fputs("invalid UTF-8 was not replaced\n", stderr);
            return 1;
        }
        // The optional factory header is installed next to the core and links against the installed spdlog.
        jspdlog::make_stdout_logger("install_smoke").info("installed");
        return 0;
    }
    catch (const std::exception &e)
    {
        (void)std::fputs(e.what(), stderr);
        return 1;
    }
    catch (...)
    {
        return 1;
    }
}
