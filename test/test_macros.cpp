// The optional JSPDLOG_* macros evaluate none of their arguments when the level is off. This file uses the default
// JSPDLOG_ACTIVE_LEVEL; test_active_level.cpp builds with another one and links into the same executable.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using test_support::fixture;

// Defined here, with the default active level, and called from test_active_level.cpp.
bool default_unit_debug_macro_writes();
bool default_unit_debug_macro_writes()
{
    fixture f;
    f.logger.set_level(jspdlog::level::trace);
    JSPDLOG_DEBUG(f.logger, {"unit", "default"}, "debug in a unit with the default active level");
    return f.sink->lines.size() == 1;
}

namespace
{

int evaluations = 0;
int next()
{
    return ++evaluations;
}

} // namespace

TEST_CASE("macros: nothing in the call is evaluated when the level is off", "[macros]")
{
    fixture f;
    evaluations = 0;

    f.logger.set_level(jspdlog::level::off);
    JSPDLOG_INFO(f.logger, {"v", next()}, "format argument {}", next());
    JSPDLOG_INFO(f.logger, {"v", next()});
    JSPDLOG_INFO(f.logger, "format argument {}", next());
    JSPDLOG_LOG(f.logger, jspdlog::level::err, {"v", next()}, "x {}", next());
    CHECK(evaluations == 0);
    CHECK(f.sink->lines.empty());

    f.logger.set_level(jspdlog::level::warn);
    JSPDLOG_DEBUG(f.logger, {"v", next()}, "below the logger level {}", next());
    CHECK(evaluations == 0);

    // The same calls as plain member functions do evaluate the format arguments.
    f.logger.info("format argument {}", next());
    CHECK(evaluations == 1);
}

TEST_CASE("macros: a call that is on is written like the member function", "[macros]")
{
    const fixture f;
    evaluations = 0;
    const std::string text = "runtime";

    JSPDLOG_TRACE(f.logger, "plain");
    CHECK(f.last() == R"({"logger":"t","level":"trace","message":"plain"})");
    JSPDLOG_DEBUG(f.logger, "{} + {}", 1, 2);
    CHECK(f.last() == R"({"logger":"t","level":"debug","message":"1 + 2"})");
    JSPDLOG_INFO(f.logger, {"a", 1, "b", "x"});
    CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":"x"})");
    JSPDLOG_WARN(f.logger, {"v", next()}, "format {}", 7);
    CHECK(f.last() == R"({"logger":"t","level":"warning","v":1,"message":"format 7"})");
    JSPDLOG_WARN(f.logger, "format {}", next());
    CHECK(f.last() == R"({"logger":"t","level":"warning","message":"format 2"})");
    JSPDLOG_ERROR(f.logger, {"k", true}, text);
    CHECK(f.last() == R"({"logger":"t","level":"error","k":true,"message":"runtime"})");
    JSPDLOG_CRITICAL(f.logger, text);
    CHECK(f.last() == R"({"logger":"t","level":"critical","message":"runtime"})");
    JSPDLOG_INFO(f.logger, jspdlog::fields{"named", 1}, "with a fields object");
    CHECK(f.last() == R"({"logger":"t","level":"info","named":1,"message":"with a fields object"})");
    JSPDLOG_INFO(f.logger, {"v", [] { return 5; }});
    CHECK(f.last() == R"({"logger":"t","level":"info","v":5})");
    CHECK(f.sink->lines.size() == 9);
}

TEST_CASE("macros: JSPDLOG_LOG takes the level at run time and evaluates the logger and level once", "[macros]")
{
    const fixture f;
    int logger_evaluations = 0;
    int level_evaluations = 0;
    const auto logger = [&]() -> const jspdlog::json_logger & {
        ++logger_evaluations;
        return f.logger;
    };
    const auto level = [&] {
        ++level_evaluations;
        return jspdlog::level::warn;
    };

    JSPDLOG_LOG(logger(), level(), {"k", 1}, "x");
    CHECK(f.last() == R"({"logger":"t","level":"warning","k":1,"message":"x"})");
    CHECK(logger_evaluations == 1);
    CHECK(level_evaluations == 1);

    JSPDLOG_INFO(logger(), "y");
    CHECK(logger_evaluations == 2);
}

TEST_CASE("macros: a macro is one statement, also in an if without braces", "[macros]")
{
    const fixture f;
    for (const bool yes : {true, false})
    {
        // No braces on purpose: a macro that is not one statement would not compile or would bind the else wrongly.
        // clang-format would add the braces, so it is switched off here.
        // clang-format off
        // NOLINTBEGIN(readability-braces-around-statements,google-readability-braces-around-statements)
        if (yes)
            JSPDLOG_INFO(f.logger, "yes");
        else
            JSPDLOG_INFO(f.logger, "no");
        // NOLINTEND(readability-braces-around-statements,google-readability-braces-around-statements)
        // clang-format on
    }
    REQUIRE(f.sink->lines.size() == 2);
    CHECK(f.sink->lines[0] == R"({"logger":"t","level":"info","message":"yes"})");
    CHECK(f.sink->lines[1] == R"({"logger":"t","level":"info","message":"no"})");
}

TEST_CASE("macros: a macro inside a lazy value of another macro", "[macros]")
{
    const fixture f;
    JSPDLOG_INFO(
        f.logger,
        {"v",
         [&f] {
             JSPDLOG_DEBUG(f.logger, "inner");
             return 1;
         }},
        "outer"
    );
    REQUIRE(f.sink->lines.size() == 2);
    CHECK(f.sink->lines[0] == R"({"logger":"t","level":"debug","message":"inner"})");
    CHECK(f.sink->lines[1] == R"({"logger":"t","level":"info","v":1,"message":"outer"})");
}

TEST_CASE("macros: the source line of a one-line call is the line of the macro", "[macros]")
{
    auto options = test_support::deterministic_options();
    options.source_key = "src";
    const fixture f("t", options);

    JSPDLOG_WARN(f.logger, "where");
    const int line = __LINE__ - 1;
    const std::string expected = R"("line":)" + std::to_string(line) + ",";
    INFO(f.last());
    CHECK(f.last().find(expected) != std::string::npos);
    CHECK(f.last().find(R"("file":"test_macros.cpp")") != std::string::npos);
}

TEST_CASE("macros: a variable that only a macro uses is used", "[macros]")
{
    const fixture f;
    const int only_logged = 42;
    JSPDLOG_INFO(f.logger, {"v", only_logged});
    CHECK(f.last() == R"({"logger":"t","level":"info","v":42})");
}
