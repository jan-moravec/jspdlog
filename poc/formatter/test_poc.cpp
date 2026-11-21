// Checks for the formatter PoC.

#include "../../test/test_support.h"
#include "jspdlog_formatter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{

jspdlog::poc::formatter_options deterministic(bool source = false)
{
    jspdlog::poc::formatter_options o;
    o.timestamp_key = std::nullopt;
    o.process_key = std::nullopt;
    o.thread_key = std::nullopt;
    if (!source)
    {
        o.source_key = std::nullopt;
    }
    o.eol = "\n";
    return o;
}

struct poc_fixture
{
    std::shared_ptr<test_support::capture_sink> sink = std::make_shared<test_support::capture_sink>();
    jspdlog::poc::json_logger logger;

    explicit poc_fixture(jspdlog::poc::formatter_options o = deterministic())
        : logger("t", sink, o)
    {
        logger.set_level(jspdlog::level::trace);
    }
    std::string last() const { return sink->last(); }
};

} // namespace

TEST_CASE("poc: call shapes and insertion order", "[poc]")
{
    poc_fixture f;
    f.logger.info("hi {}", 1);
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"hi 1"})");
    f.logger.warn({"b", 1, "a", 2}, "x");
    CHECK(f.last() == R"({"logger":"t","level":"warning","b":1,"a":2,"message":"x"})");
    f.logger.error({"only", true});
    CHECK(f.last() == R"({"logger":"t","level":"error","only":true})");
}

TEST_CASE("poc: nested context, overrides keep position", "[poc]")
{
    poc_fixture f;
    auto outer = f.logger.with_properties({"req", "r1", "user", 1});
    auto inner = outer.with_properties({"step", "s", "user", 2});
    inner.info("x");
    CHECK(f.last() == R"({"logger":"t","level":"info","req":"r1","user":2,"step":"s","message":"x"})");
    inner.info({"user", 3, "extra", 0}, "y");
    CHECK(f.last() == R"({"logger":"t","level":"info","req":"r1","step":"s","user":3,"extra":0,"message":"y"})");
    outer.info("z");
    CHECK(f.last() == R"({"logger":"t","level":"info","req":"r1","user":1,"message":"z"})");
}

TEST_CASE("poc: call site is captured without macros", "[poc]")
{
    poc_fixture f(deterministic(true));
    const int line = __LINE__ + 1;
    f.logger.info("here");
    const std::string expected_line = R"("line":)" + std::to_string(line) + ",";
    CHECK(f.last().find(expected_line) != std::string::npos);
    CHECK(f.last().find("test_poc.cpp") != std::string::npos);

    const int line2 = __LINE__ + 1;
    f.logger.info({"k", 1});
    CHECK(f.last().find(R"("line":)" + std::to_string(line2) + ",") != std::string::npos);
}

TEST_CASE("poc: foreign messages are escaped, not spliced", "[poc]")
{
    poc_fixture f;
    f.logger.spdlog_logger()->info(R"(,"admin":true)");
    const std::string expected = R"({"logger":"t","level":"info","message":",\"admin\":true"})";
    CHECK(f.last() == expected);
}

TEST_CASE("poc: all header fields omitted", "[poc]")
{
    auto o = deterministic();
    o.logger_key = std::nullopt;
    o.level_key = std::nullopt;
    poc_fixture f(o);
    f.logger.info({"k", 1});
    CHECK(f.last() == R"({"k":1})");
    f.logger.info("m");
    CHECK(f.last() == R"({"message":"m"})");
    f.logger.spdlog_logger()->info("raw");
    CHECK(f.last() == R"({"message":"raw"})");
}
