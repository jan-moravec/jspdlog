#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <type_traits>

using test_support::fixture;

namespace
{

jspdlog::json_options source_options()
{
    auto opts = test_support::deterministic_options();
    opts.source_key = "src";
    return opts;
}

// Logs for its caller with every call form.
void log_every_form(const jspdlog::json_logger &log, jspdlog::call_site where = {})
{
    const std::runtime_error ex("boom");
    const std::string text = "runtime";
    log.at(where).info("plain");
    log.at(where).warn("{} formatted", 1);
    log.at(where).error(text);
    log.at(where).debug({"k", 1});
    log.at(where).trace({"k", "v"}, text);
    log.at(where).info({"k", 1}, "with {}", "fields");
    log.at(where).error(ex);
    log.at(where).error(ex, {"k", 1});
    log.at(where).error(ex, "message");
    log.at(where).error(ex, {"k", 1}, "all {}", 3);
    log.at(where).log(jspdlog::level::info, "level first");
    log.at(where).log(jspdlog::level::info, {"k", 1});
}

// Calls log_every_form and stores the line of that call.
void audit_caller(const jspdlog::json_logger &log, int &line)
{
    line = __LINE__ + 1;
    log_every_form(log);
}

} // namespace

static_assert(!std::is_convertible_v<const char *, jspdlog::call_site>, "a string must not become a call site");
static_assert(!std::is_convertible_v<int, jspdlog::call_site>, "a number must not become a call site");
static_assert(jspdlog::call_site::of("f.cpp", 3, "g").line() == 3, "of keeps the line");

TEST_CASE("source location: a function logs for its caller with at", "[source]")
{
    const fixture f("t", source_options());
    int line = 0;
    audit_caller(f.logger, line);
    REQUIRE(f.sink->lines.size() == 12);
    const std::string expected =
        R"("src":{"file":"test_source_location.cpp","line":)" + std::to_string(line) + R"(,"function":"audit_caller"})";
    for (const std::string &written : f.sink->lines)
    {
        CAPTURE(written);
        CHECK(written.find(expected) != std::string::npos);
    }
    // The lines hold what the same calls on the logger would.
    CHECK(f.sink->lines[1].find(R"("level":"warning")") != std::string::npos);
    CHECK(f.sink->lines[1].find(R"("message":"1 formatted"})") != std::string::npos);
    CHECK(f.sink->lines[9].find(R"("k":1,"message":"all 3"})") != std::string::npos);
}

TEST_CASE("source location: a default-constructed call_site records where it is made", "[source]")
{
    const fixture f("t", source_options());
    const int line = __LINE__ + 1;
    const jspdlog::call_site here;
    f.logger.at(here).info("x");
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);
    CHECK(here.line() == line);
}

TEST_CASE("source location: call sites given explicitly", "[source]")
{
    const fixture f("t", source_options());
    SECTION("by parts")
    {
        f.logger.at(jspdlog::call_site::of("some/dir/c.cpp", 12, "other")).info("x");
        CHECK(f.last().find(R"("src":{"file":"c.cpp","line":12,"function":"other"})") != std::string::npos);
    }
    SECTION("from an spdlog::source_loc")
    {
        f.logger.at(jspdlog::call_site::of(spdlog::source_loc{"a/b.cpp", 7, "fn"})).info("x");
        CHECK(f.last().find(R"("src":{"file":"b.cpp","line":7,"function":"fn"})") != std::string::npos);
    }
    SECTION("an empty spdlog::source_loc writes no call site")
    {
        f.logger.at(jspdlog::call_site::of(spdlog::source_loc{})).info("x");
        CHECK(f.last() == R"({"logger":"t","level":"info","message":"x"})");
    }
    SECTION("a null file writes no call site, and a null function an empty one")
    {
        f.logger.at(jspdlog::call_site::of(nullptr, 5, "fn")).info("x");
        CHECK(f.last() == R"({"logger":"t","level":"info","message":"x"})");
        f.logger.at(jspdlog::call_site::of("a.cpp", 5, nullptr)).info("x");
        CHECK(f.last().find(R"("src":{"file":"a.cpp","line":5,"function":""})") != std::string::npos);
    }
#if defined(__cpp_lib_source_location)
    SECTION("from a std::source_location")
    {
        const int line = __LINE__ + 1;
        const auto location = std::source_location::current();
        f.logger.at(jspdlog::call_site::of(location)).info("x");
        CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);
        CHECK(f.last().find(R"("file":"test_source_location.cpp")") != std::string::npos);
    }
#endif
}

TEST_CASE("source location: at keeps the level check and lazy values", "[source]")
{
    fixture f("t", source_options());
    const jspdlog::call_site where = jspdlog::call_site::of("w.cpp", 1, "w");
    int runs = 0;
    const auto count = [&runs] { return ++runs; };

    f.logger.set_level(jspdlog::level::warn);
    f.logger.at(where).info({"n", count}, "dropped");
    f.logger.at(where).debug({"n", count});
    CHECK(runs == 0);
    CHECK(f.sink->lines.empty());

    f.logger.with({"a", 1}).at(where).error({"n", count}, "kept");
    CHECK(runs == 1);
    CHECK(
        f.last() == R"({"logger":"t","level":"error","src":{"file":"w.cpp","line":1,"function":"w"},)"
                    R"("a":1,"n":1,"message":"kept"})"
    );
}

TEST_CASE("source location: every call form records its own line", "[source]")
{
    fixture f("t", source_options());
    const std::string text = "runtime";

    int line = __LINE__ + 1;
    f.logger.info("plain");
    CHECK(
        f.last().find(R"("file":"test_source_location.cpp","line":)" + std::to_string(line) + ",") != std::string::npos
    );

    line = __LINE__ + 1;
    f.logger.warn("{} formatted", 1);
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.error(text);
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.debug({"k", 1});
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.critical({"k", 1}, "with {}", "fields");
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.trace({"k", 1}, text);
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);
}

TEST_CASE("source location: log(level, ...) and the level functions record their own lines", "[source]")
{
    fixture f("t", source_options());

    const int log_line = __LINE__ + 1;
    f.logger.log(jspdlog::level::info, "x");
    const std::string first = f.last();
    const int info_line = __LINE__ + 1;
    f.logger.info("x");
    const std::string second = f.last();

    CHECK(first.find(R"("line":)" + std::to_string(log_line) + ",") != std::string::npos);
    CHECK(second.find(R"("line":)" + std::to_string(info_line) + ",") != std::string::npos);
    CHECK(first.find("test_source_location.cpp") != std::string::npos);
}

TEST_CASE("source location: written between the header and the fields, and only when asked for", "[source]")
{
    fixture f("t", source_options());
    f.logger.with({"a", 1}).info("m");
    const std::string line = f.last();
    CHECK(line.find(R"({"logger":"t","level":"info","src":{)") == 0);
    CHECK(line.find(R"(},"a":1,"message":"m"})") != std::string::npos);

    fixture off;
    off.logger.info("m");
    CHECK(off.last() == R"({"logger":"t","level":"info","message":"m"})");
}
