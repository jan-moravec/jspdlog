// Smallest possible consumer of the installed jspdlog package. The test is "this file builds and runs", plus one check
// that the compile definitions of the package reach the consumer: the default package replaces invalid UTF-8.

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

int main()
{
    try
    {
        std::ostringstream out;
        const jspdlog::json_logger logger("install_smoke", std::make_shared<spdlog::sinks::ostream_sink_mt>(out));
        logger.info(jspdlog::fields{"ok", true}, "hello {}", "world");
        logger.info("bad \xFF byte");
        const std::string text = out.str();
        if (text.find("bad \xEF\xBF\xBD byte") == std::string::npos)
        {
            (void)std::fputs("invalid UTF-8 was not replaced\n", stderr);
            return 1;
        }
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
