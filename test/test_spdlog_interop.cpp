// How json_logger behaves next to the rest of spdlog: shared sinks, cloned loggers, the error handler and the
// registry. README.md, section "Working with spdlog", documents these rules.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#if !JSPDLOG_SPDLOG_V2
#include <spdlog/spdlog.h>
#endif

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

TEST_CASE("interop: formatting errors go to the spdlog error handler", "[interop]")
{
    const test_support::fixture f;
    std::vector<std::string> errors;
    f.logger.spdlog_logger()->set_error_handler([&errors](const std::string &msg) { errors.push_back(msg); });

    CHECK_NOTHROW(f.logger.info("value {}", throws_when_formatted{}));
    CHECK_NOTHROW(f.logger.info(throws_when_formatted{}));
    CHECK_NOTHROW(f.logger.info({"k", 1}, "value {}", throws_when_formatted{}));
    CHECK_NOTHROW(f.logger.info({"k", 1}, throws_when_formatted{}));

    CHECK(f.sink->lines.empty());
    REQUIRE(errors.size() == 4);
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
