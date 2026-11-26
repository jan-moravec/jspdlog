// A consumer of the Conan package: "this file builds, links and writes one line" is the test, plus a check that the
// package's default text policy reaches the consumer.

#include <jspdlog/factories.h>

#include <cstdio>
#include <exception>

#if JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_REPLACE || JSPDLOG_ESCAPE_LINE_SEPARATORS != 0
#error "the package must carry the default text policy to its consumers"
#endif

int main()
{
    try
    {
        jspdlog::make_stdout_logger("test_package").info({"ok", true}, "hello {}", "world");
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
