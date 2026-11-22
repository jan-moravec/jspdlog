#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <spdlog/sinks/ostream_sink.h>

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using test_support::fixture;

TEST_CASE("json_logger: default header fields", "[json_logger]")
{
    std::ostringstream oss;
    const jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::ostream_sink_mt>(oss));
    logger.info("hello {}", "world");

    // spdlog's %z always renders as +hh:mm.
    const std::regex expected(
        R"(\{"timestamp":"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}[+-]\d{2}:\d{2}","logger":"app","level":"info",)"
        R"("process":\d+,"thread":\d+,"message":"hello world"\}\r?\n)"
    );
    REQUIRE(std::regex_match(oss.str(), expected));
}

TEST_CASE("json_logger: the five call shapes", "[json_logger]")
{
    fixture f;

    f.logger.info("plain");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"plain"})");

    f.logger.warn("{} + {}", 1, 2);
    CHECK(f.last() == R"({"logger":"t","level":"warning","message":"1 + 2"})");

    f.logger.debug(std::string{"runtime"});
    CHECK(f.last() == R"({"logger":"t","level":"debug","message":"runtime"})");

    f.logger.trace({"k", 1});
    CHECK(f.last() == R"({"logger":"t","level":"trace","k":1})");

    f.logger.error({"a", "x", "b", true}, "{} done", "job");
    CHECK(f.last() == R"({"logger":"t","level":"error","a":"x","b":true,"message":"job done"})");

    f.logger.critical({"k", 2}, std::string{"value form"});
    CHECK(f.last() == R"({"logger":"t","level":"critical","k":2,"message":"value form"})");
}

TEST_CASE("json_logger: runtime strings are messages, typed values are not", "[json_logger]")
{
    fixture f;
    const char *c_string = "runtime";
    f.logger.info(c_string);
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"runtime"})");
    f.logger.info(std::string_view{"view {}"});
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"view {}"})");
    f.logger.info(static_cast<const char *>(nullptr));
    CHECK(f.last() == R"({"logger":"t","level":"info","message":""})");
    f.logger.info({"k", 1}, std::string{"braces {} stay"});
    CHECK(f.last() == R"({"logger":"t","level":"info","k":1,"message":"braces {} stay"})");
    f.logger.info(spdlog::fmt_lib::runtime("{} and {}"), 1, 2);
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"1 and 2"})");
}

TEST_CASE("json_logger: log(level, ...) takes the level at runtime", "[json_logger]")
{
    fixture f;
    const std::vector<jspdlog::level> levels{jspdlog::level::info, jspdlog::level::err};
    for (const auto lvl : levels)
    {
        f.logger.log(lvl, {"n", 1}, "x");
    }
    REQUIRE(f.sink->lines.size() == 2);
    CHECK(f.sink->lines[0] == R"({"logger":"t","level":"info","n":1,"message":"x"})");
    CHECK(f.sink->lines[1] == R"({"logger":"t","level":"error","n":1,"message":"x"})");
}

TEST_CASE("json_logger: logging through a const reference", "[json_logger]")
{
    fixture f;
    const jspdlog::json_logger &logger = f.logger;
    logger.info({"k", 1}, "const {}", "ref");
    CHECK(f.last() == R"({"logger":"t","level":"info","k":1,"message":"const ref"})");
    logger.log(jspdlog::level::warn, std::string{"value"});
    CHECK(f.last() == R"({"logger":"t","level":"warning","message":"value"})");
}

TEST_CASE("version macros match the CMake project version", "[version]")
{
    const std::string from_macros = std::to_string(JSPDLOG_VERSION_MAJOR) + "." +
                                    std::to_string(JSPDLOG_VERSION_MINOR) + "." + std::to_string(JSPDLOG_VERSION_PATCH);
    CHECK(from_macros == JSPDLOG_TEST_PROJECT_VERSION);
    CHECK(JSPDLOG_VERSION == (JSPDLOG_VERSION_MAJOR * 10000) + (JSPDLOG_VERSION_MINOR * 100) + JSPDLOG_VERSION_PATCH);
}

TEST_CASE("json_logger: level filtering", "[json_logger]")
{
    fixture f;
    f.logger.set_level(jspdlog::level::warn);
    CHECK(f.logger.log_level() == jspdlog::level::warn);
    CHECK_FALSE(f.logger.should_log(jspdlog::level::info));
    CHECK(f.logger.should_log(jspdlog::level::err));

    f.logger.info("dropped");
    f.logger.info({"k", 1});
    f.logger.error("kept");
    REQUIRE(f.sink->lines.size() == 1);
    CHECK(f.last() == R"({"logger":"t","level":"error","message":"kept"})");
}

TEST_CASE("json_logger: messages are escaped", "[json_logger]")
{
    fixture f;
    // Expected values with backslashes live outside CHECK(): MSVC mis-stringifies
    // such raw literals inside Catch2's expression capture.
    f.logger.info("quote \" backslash \\ newline \n tab \t ctl \x01");
    const std::string escaped =
        R"({"logger":"t","level":"info","message":"quote \" backslash \\ newline \n tab \t ctl \u0001"})";
    CHECK(f.last() == escaped);

    f.logger.info("{}", "braces {} stay");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"braces {} stay"})");
}

TEST_CASE("json_logger: logger names cannot break the pattern", "[json_logger]")
{
    SECTION("JSON specials")
    {
        fixture f("we\"ird\\name");
        f.logger.info("hi");
        const std::string expected = R"({"logger":"we\"ird\\name","level":"info","message":"hi"})";
        CHECK(f.last() == expected);
    }
    SECTION("spdlog pattern flags")
    {
        fixture f("50%off-%v-%n");
        f.logger.info("hi");
        CHECK(f.last() == R"({"logger":"50%off-%v-%n","level":"info","message":"hi"})");
    }
}

TEST_CASE("json_logger: with", "[json_logger]")
{
    fixture f;
    auto child = f.logger.with({"request_id", "abc", "user", 1});

    SECTION("child carries the bound properties, parent does not")
    {
        child.info("in child");
        CHECK(f.last() == R"({"logger":"t","level":"info","request_id":"abc","user":1,"message":"in child"})");
        f.logger.info("in parent");
        CHECK(f.last() == R"({"logger":"t","level":"info","message":"in parent"})");
        CHECK(child.context().size() == 2);
        CHECK(f.logger.context().empty());
    }
    SECTION("properties-only call on a child")
    {
        child.info(jspdlog::fields{});
        CHECK(f.last() == R"({"logger":"t","level":"info","request_id":"abc","user":1})");
    }
    SECTION("per-call properties merge with, and override, bound ones")
    {
        child.info({"user", 2, "a", true}, "merged");
        CHECK(f.last() == R"({"logger":"t","level":"info","request_id":"abc","user":2,"a":true,"message":"merged"})");
    }
    SECTION("a per-call field that replaces a bound one keeps the bound position")
    {
        child.info({"extra", 1, "user", 9}, "x");
        CHECK(f.last() == R"({"logger":"t","level":"info","request_id":"abc","user":9,"extra":1,"message":"x"})");
    }
    SECTION("nested with: newer values win")
    {
        const auto grandchild = child.with({"user", 3});
        grandchild.info("x");
        CHECK(f.last() == R"({"logger":"t","level":"info","request_id":"abc","user":3,"message":"x"})");
    }
    SECTION("a replaced key keeps its position in the child")
    {
        const auto grandchild = child.with({"new", true, "request_id", "r2"});
        grandchild.info("x");
        CHECK(f.last() == R"({"logger":"t","level":"info","request_id":"r2","user":1,"new":true,"message":"x"})");
        CHECK(grandchild.context().size() == 3);
    }
    SECTION("children share the spdlog logger")
    {
        CHECK(child.spdlog_logger() == f.logger.spdlog_logger());
        child.set_level(jspdlog::level::off);
        CHECK(f.logger.log_level() == jspdlog::level::off);
    }
}

TEST_CASE("json_options: header fields can be renamed and omitted", "[json_options]")
{
    SECTION("rename")
    {
        auto opts = test_support::deterministic_options();
        opts.logger_key = "svc";
        opts.level_key = "severity";
        fixture f("t", opts);
        f.logger.info("hi");
        CHECK(f.last() == R"({"svc":"t","severity":"info","message":"hi"})");
    }
    SECTION("keys are escaped for JSON and for spdlog")
    {
        auto opts = test_support::deterministic_options();
        opts.logger_key = "a\"b";
        opts.level_key = "%v";
        fixture f("t", opts);
        f.logger.info("hi");
        const std::string expected = R"({"a\"b":"t","%v":"info","message":"hi"})";
        CHECK(f.last() == expected);
    }
    SECTION("all fields omitted")
    {
        auto opts = test_support::deterministic_options();
        opts.logger_key = std::nullopt;
        opts.level_key = std::nullopt;
        fixture f("t", opts);

        f.logger.info("hi");
        CHECK(f.last() == R"({"message":"hi"})");
        f.logger.info({"k", 1});
        CHECK(f.last() == R"({"k":1})");
        f.logger.info(jspdlog::fields{});
        CHECK(f.last() == "{}");
        f.logger.with({"b", 2}).info("x");
        CHECK(f.last() == R"({"b":2,"message":"x"})");
    }
}

TEST_CASE("json_options: eol and time type", "[json_options]")
{
    std::ostringstream oss;
    jspdlog::json_options opts;
    opts.eol = "\n";
    opts.time_type = spdlog::pattern_time_type::utc;
    const jspdlog::json_logger logger("t", std::make_shared<spdlog::sinks::ostream_sink_mt>(oss), opts);
    logger.info("x");

    const std::string out = oss.str();
    REQUIRE(out.size() > 2);
    CHECK(out.back() == '\n');
    CHECK(out[out.size() - 2] == '}');
    const std::regex utc_timestamp(R"("timestamp":"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z")");
    CHECK(std::regex_search(out, utc_timestamp));

    SECTION("empty eol suppresses the terminator")
    {
        std::ostringstream oss2;
        jspdlog::json_options no_eol;
        no_eol.eol = "";
        const jspdlog::json_logger l2("t", std::make_shared<spdlog::sinks::ostream_sink_mt>(oss2), no_eol);
        l2.info("x");
        CHECK(oss2.str().back() == '}');
    }
}

TEST_CASE("json_logger: construction", "[json_logger]")
{
    SECTION("multiple sinks via init list and iterator range")
    {
        const auto s1 = std::make_shared<test_support::capture_sink>();
        const auto s2 = std::make_shared<test_support::capture_sink>();
        const jspdlog::json_logger a("a", {s1, s2}, test_support::deterministic_options());
        a.info("x");
        CHECK(s1->last() == s2->last());

        std::vector<spdlog::sink_ptr> sinks{s1, s2};
        const jspdlog::json_logger b("b", sinks.begin(), sinks.end(), test_support::deterministic_options());
        b.info("y");
        CHECK(s1->last() == R"({"logger":"b","level":"info","message":"y"})");
        CHECK(s2->last() == s1->last());
    }
    SECTION("null sinks are rejected")
    {
        CHECK_THROWS_AS(jspdlog::json_logger("x", spdlog::sink_ptr{}), std::invalid_argument);
        CHECK_THROWS_AS(
            jspdlog::json_logger("x", {std::make_shared<test_support::capture_sink>(), spdlog::sink_ptr{}}),
            std::invalid_argument
        );
    }
    SECTION("adopt replaces the formatter of an existing logger")
    {
        const auto sink = std::make_shared<test_support::capture_sink>();
        auto raw = std::make_shared<spdlog::logger>("raw", sink);
        raw->set_pattern("%v");
        const auto logger = jspdlog::json_logger::adopt(raw, test_support::deterministic_options());
        logger.info("adopted");
        CHECK(sink->last() == R"({"logger":"raw","level":"info","message":"adopted"})");
        CHECK(logger.spdlog_logger() == raw);
    }
    SECTION("adopt rejects null")
    {
        CHECK_THROWS_AS(jspdlog::json_logger::adopt(nullptr), std::invalid_argument);
    }
}

TEST_CASE("json_logger: copies are small and share their context", "[json_logger]")
{
    STATIC_CHECK(sizeof(jspdlog::json_logger) <= 4 * sizeof(void *));
    fixture f;
    const auto child = f.logger.with({"a", 1});
    const auto copy = child;
    CHECK(&copy.context() == &child.context());
    CHECK(&f.logger.context() != &child.context());
}
