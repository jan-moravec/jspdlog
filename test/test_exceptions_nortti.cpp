// Exceptions in a translation unit built without run-time type information (-fno-rtti, /GR-). The dynamic type of an
// exception cannot be named then, so the type is std::exception and nested causes are not inspected, but the message
// and the call forms work. It has its own main, because Catch2 is built with RTTI.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <cstdio>
#include <stdexcept>
#include <string>

#if JSPDLOG_HAS_RTTI
#error "this unit must be built without RTTI"
#endif

namespace
{

int failures = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): counted by check

void check(const std::string &actual, const std::string &expected, const char *what)
{
    if (actual != expected)
    {
        ++failures;
        std::fprintf(stderr, "FAILED: %s\n  actual:   %s\n  expected: %s\n", what, actual.c_str(), expected.c_str());
    }
}

} // namespace

int main()
{
    const test_support::fixture f;
    const std::runtime_error ex("boom");
    const std::string head = R"({"logger":"t","level":"error","error":{"type":"std::exception","message":"boom"})";

    f.logger.error(ex);
    check(f.last(), head + "}", "no message");
    f.logger.error(ex, {"k", 1}, "connect {}", "failed");
    check(f.last(), head + R"(,"k":1,"message":"connect failed"})", "fields and a format string");
    f.logger.info({"e", ex});
    check(f.last(), R"({"logger":"t","level":"info","e":{"type":"std::exception","message":"boom"}})", "value form");

    // The nesting of an exception is not inspected without RTTI.
    try
    {
        try
        {
            throw std::runtime_error("inner");
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("outer"));
        }
    }
    catch (const std::exception &nested)
    {
        f.logger.error(nested);
        check(
            f.last(), R"({"logger":"t","level":"error","error":{"type":"std::exception","message":"outer"}})", "nested"
        );
    }

    if (failures == 0)
    {
        std::printf("ok\n");
    }
    return failures == 0 ? 0 : 1;
}
