// How json_logger behaves next to the rest of spdlog: shared sinks, cloned loggers, the error handler and the
// registry. README.md, section "Working with spdlog", documents these rules.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_test_macros.hpp>

#if JSPDLOG_SPDLOG_V2
#include <spdlog/sinks/async_sink.h>
#else
#include <spdlog/async.h>
#endif

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

// A type whose {fmt} formatter throws, standing in for a user formatter with a bug.
struct throws_when_formatted
{
};

} // namespace

template <>
struct fmt::formatter<throws_when_formatted>
{
    static constexpr auto parse(fmt::format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

    template <typename Context>
    auto format(const throws_when_formatted & /*value*/, Context &ctx) const -> decltype(ctx.out())
    {
        throw std::runtime_error("formatter failed");
    }
};

using test_support::capture_sink;
using test_support::deterministic_options;

TEST_CASE("interop: json_loggers sharing a sink keep their own names", "[interop]")
{
    const auto sink = std::make_shared<capture_sink>();
    const jspdlog::json_logger a("a", sink, deterministic_options());
    const jspdlog::json_logger b("b", sink, deterministic_options());

    a.info("x");
    CHECK(sink->last() == R"({"logger":"a","level":"info","message":"x"})");
    b.info("y");
    CHECK(sink->last() == R"({"logger":"b","level":"info","message":"y"})");
}

TEST_CASE("interop: a cloned spdlog::logger and its original keep their own names", "[interop]")
{
    const auto sink = std::make_shared<capture_sink>();
    const jspdlog::json_logger original("original", sink, deterministic_options());
    const auto clone = jspdlog::json_logger::adopt(original.spdlog_logger()->clone("cl\"one"), deterministic_options());

    clone.info("x");
    const std::string expected = R"({"logger":"cl\"one","level":"info","message":"x"})";
    CHECK(sink->last() == expected);
    original.info("y");
    CHECK(sink->last() == R"({"logger":"original","level":"info","message":"y"})");
}

TEST_CASE("interop: a shared sink stays valid JSON when the loggers' options differ", "[interop]")
{
    auto bare = deterministic_options();
    bare.logger_key = std::nullopt;
    bare.level_key = std::nullopt;

    SECTION("the last logger has no header fields")
    {
        const auto sink = std::make_shared<capture_sink>();
        const jspdlog::json_logger with_header("a", sink, deterministic_options());
        const jspdlog::json_logger without_header("b", sink, bare);
        with_header.info({"k", 1}, "x");
        CHECK(sink->last() == R"({"k":1,"message":"x"})");
    }
    SECTION("the last logger has header fields")
    {
        const auto sink = std::make_shared<capture_sink>();
        const jspdlog::json_logger without_header("b", sink, bare);
        const jspdlog::json_logger with_header("a", sink, deterministic_options());
        without_header.info({"k", 1}, "x");
        CHECK(sink->last() == R"({"logger":"b","level":"info","k":1,"message":"x"})");
    }
}

TEST_CASE("interop: two json_loggers adopting one spdlog::logger both keep their fields", "[interop]")
{
    const test_support::fixture f;
    const auto shared = f.logger.spdlog_logger();
    const auto a = jspdlog::json_logger::adopt(shared, deterministic_options()).with({"a", 1});
    const auto b = jspdlog::json_logger::adopt(shared, deterministic_options()).with({"b", 2});

    a.info("x");
    CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"message":"x"})");
    b.info("y");
    CHECK(f.last() == R"({"logger":"t","level":"info","b":2,"message":"y"})");
}

TEST_CASE("interop: a sink added later gets JSON lines from install_json_formatter", "[interop]")
{
    const test_support::fixture f;
    const auto extra = std::make_shared<capture_sink>();
    f.logger.spdlog_logger()->sinks().push_back(extra);
    jspdlog::install_json_formatter(*extra, deterministic_options());

    f.logger.info({"k", 1}, "x");
    CHECK(f.last() == R"({"logger":"t","level":"info","k":1,"message":"x"})");
    CHECK(extra->last() == f.last());
    f.logger.spdlog_logger()->info("direct");
    CHECK(extra->last() == R"({"logger":"t","level":"info","message":"direct"})");
}

TEST_CASE(
    "interop: a plain spdlog logger sharing a sink writes valid lines once install_json_formatter ran", "[interop]"
)
{
    const test_support::fixture f;
    const auto plain = test_support::make_plain_logger("plain", f.sink);

    plain->info("x {}", 1);
    CHECK(f.last() == R"({"logger":"plain","level":"info","message":"x 1"})");
    f.logger.info({"k", 1}, "y");
    CHECK(f.last() == R"({"logger":"t","level":"info","k":1,"message":"y"})");
}

TEST_CASE("interop: install_json_formatter on a logger covers all of its sinks", "[interop]")
{
    const auto first = std::make_shared<capture_sink>();
    const auto second = std::make_shared<capture_sink>();
    spdlog::logger plain("third_party", {first, second});
    jspdlog::install_json_formatter(plain, deterministic_options());

    plain.info("hello");
    const std::string expected = R"({"logger":"third_party","level":"info","message":"hello"})";
    CHECK(first->last() == expected);
    CHECK(second->last() == expected);
}

TEST_CASE("interop: the dup_filter_sink skip notice is valid JSON", "[interop]")
{
    const auto capture = std::make_shared<capture_sink>();
    const auto dup = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(std::chrono::seconds(5));
    dup->add_sink(capture);
    const jspdlog::json_logger logger("t", dup, deterministic_options());

    logger.info("same");
    logger.info("same");
    logger.info("same");
    logger.info("different");

    bool saw_notice = false;
    for (const auto &line : capture->lines)
    {
        INFO(line);
        CHECK(test_support::is_single_json_object(line));
        saw_notice = saw_notice || line.find("duplicate messages") != std::string::npos;
    }
    CHECK(saw_notice);
}

#if JSPDLOG_SPDLOG_V2
TEST_CASE("interop: an async_sink keeps the fields and the message", "[interop]")
{
    const auto capture = std::make_shared<capture_sink>();
    spdlog::sinks::async_sink::config config;
    config.sinks.push_back(capture);
    const auto async = std::make_shared<spdlog::sinks::async_sink>(std::move(config));
    jspdlog::json_logger logger("t", async, deterministic_options());
    logger.set_level(jspdlog::level::trace);

    logger.info({"k", 1}, "x");
    logger.spdlog_logger()->info("direct");
    async->wait_all();

    REQUIRE(capture->lines.size() == 2);
    CHECK(capture->lines[0] == R"({"logger":"t","level":"info","k":1,"message":"x"})");
    CHECK(capture->lines[1] == R"({"logger":"t","level":"info","message":"direct"})");
}
#else
TEST_CASE("interop: an async logger keeps the fields and the message", "[interop]")
{
    const auto capture = std::make_shared<capture_sink>();
    {
        auto pool = std::make_shared<spdlog::details::thread_pool>(64, 1);
        auto async = std::make_shared<spdlog::async_logger>("t", capture, pool, spdlog::async_overflow_policy::block);
        const auto logger = jspdlog::json_logger::adopt(async, deterministic_options());
        logger.info({"k", 1}, "x");
        async->info("direct");
        pool.reset(); // The pool drains its queue before its worker stops.
    }
    REQUIRE(capture->lines.size() == 2);
    CHECK(capture->lines[0] == R"({"logger":"t","level":"info","k":1,"message":"x"})");
    CHECK(capture->lines[1] == R"({"logger":"t","level":"info","message":"direct"})");
}

TEST_CASE("interop: the spdlog v1 backtrace keeps every line valid", "[interop]")
{
    const test_support::fixture f;
    f.logger.spdlog_logger()->enable_backtrace(4);
    f.logger.debug({"k", 1}, "one");
    f.logger.debug({"k", 1}, "two");
    f.logger.debug({"k", 1}, "three");
    f.logger.spdlog_logger()->dump_backtrace();

    std::size_t with_fields = 0;
    for (const auto &line : f.sink->lines)
    {
        INFO(line);
        CHECK(test_support::is_single_json_object(line));
        if (line.find(R"("k":1)") != std::string::npos)
        {
            ++with_fields;
        }
    }
    CHECK(with_fields == 6); // Each line was written once live and once from the backtrace.
    CHECK(f.sink->lines.size() == 8);
}

TEST_CASE("interop: spdlog::set_pattern on a registered logger replaces the formatter", "[interop]")
{
    test_support::fixture f("replaced");
    spdlog::register_logger(f.logger.spdlog_logger());
    spdlog::set_pattern("%v");

    f.logger.info("x");
    CHECK(f.last().rfind("jspdlog:", 0) == 0); // The payload protocol is visible, which is not JSON.

    spdlog::drop("replaced");
    spdlog::set_pattern("%+");
}
#endif

TEST_CASE("interop: formatting errors go to the spdlog error handler", "[interop]")
{
    const test_support::fixture f;
    std::vector<std::string> errors;
    f.logger.spdlog_logger()->set_error_handler([&errors](const std::string &msg) { errors.push_back(msg); });

    CHECK_NOTHROW(f.logger.info("value {}", throws_when_formatted{}));
    CHECK_NOTHROW(f.logger.info({"k", 1}, "value {}", throws_when_formatted{}));

    CHECK(f.sink->lines.empty());
    REQUIRE(errors.size() == 2);
    for (const auto &e : errors)
    {
        CHECK(e.find("formatter failed") != std::string::npos);
    }

    // The logger keeps working after an error.
    f.logger.info("ok");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"ok"})");
}

#if !JSPDLOG_SPDLOG_V2
TEST_CASE("interop: registering with the spdlog v1 registry keeps the JSON format", "[interop]")
{
    test_support::fixture f("registered");
    spdlog::register_logger(f.logger.spdlog_logger());
    CHECK(spdlog::get("registered") == f.logger.spdlog_logger());

    spdlog::set_level(spdlog::level::debug);
    f.logger.debug("x");
    CHECK(f.last() == R"({"logger":"registered","level":"debug","message":"x"})");
    f.logger.trace("filtered by the registry level");
    CHECK(f.sink->lines.size() == 1);

    spdlog::drop("registered");
    spdlog::set_level(spdlog::level::info);
}
#endif
