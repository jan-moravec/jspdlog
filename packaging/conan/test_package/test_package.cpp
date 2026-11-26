// A consumer of the Conan package: "this file builds, links and writes one line" is the test.

#include <jspdlog/factories.h>

#include <cstdio>
#include <exception>

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
