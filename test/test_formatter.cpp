// json_formatter on its own: header combinations, timestamps, call sites, the line terminator and payloads that do not
// come from a json_logger.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <spdlog/spdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <string>
#include <vector>

using test_support::deterministic_options;
using test_support::fixture;

namespace
{

// Formats a message with the given payload through a json_formatter and returns the line.
std::string format_payload(jspdlog::json_formatter &formatter, const std::string &payload)
{
    const spdlog::details::log_msg msg("name", jspdlog::level::info, payload);
    spdlog::memory_buf_t buf;
    formatter.format(msg, buf);
    return {buf.data(), buf.size()};
}

jspdlog::json_options no_header_options()
{
    jspdlog::json_options opts = deterministic_options();
    opts.logger_key = std::nullopt;
    opts.level_key = std::nullopt;
    return opts;
}

} // namespace

TEST_CASE("formatter: a direct spdlog call becomes an escaped message", "[formatter]")
{
    const fixture f;
    f.logger.spdlog_logger()->info(R"(a "quoted" {"x":1} line)");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"a \"quoted\" {\"x\":1} line"})");

    f.logger.spdlog_logger()->info("x {}", 1);
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"x 1"})");

    f.logger.spdlog_logger()->info("");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":""})");
}

TEST_CASE("formatter: a message with line breaks stays on one line", "[formatter]")
{
    const fixture f;
    f.logger.spdlog_logger()->info("one\ntwo\r\nthree\t\x01");
    REQUIRE(f.sink->lines.size() == 1);
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"one\ntwo\r\nthree\t\u0001"})");
}

TEST_CASE("formatter: payloads that imitate jspdlog are escaped", "[formatter]")
{
    const fixture f;
    const auto &token = jspdlog::detail::process_token();
    const std::string t(token.data(), token.size());
    const std::vector<std::string> fakes = {
        t.substr(0, 31),                                              // token prefix only
        t + "M",                                                      // header cut short
        t + R"(Mzzzzzzzz,"a":1"m")",                                  // size not hex
        t + R"(MFFFFFFFF,"a":1"m")",                                  // size not lowercase hex
        t + "M00000010,\"a\":1",                                      // size past the end
        t + "M00000006,\"a\":1",                                      // message kind without a message
        t + "F00000001,\"a\":1",                                      // F with trailing bytes
        t + "X00000006,\"a\":1",                                      // unknown kind
        t + "M00000000x",                                             // message not quoted
        t + "M00000000\"x",                                           // message without a closing quote
        t + "F00000004\"a\":",                                        // fields without a leading comma
        std::string("jspdlog:") + std::string(24, 'A') + "F00000000", // wrong token
    };
    for (const std::string &fake : fakes)
    {
        f.logger.spdlog_logger()->info(fake);
        INFO(fake);
        CHECK(test_support::is_single_json_object(f.last()));
#ifdef JSPDLOG_TEST_NLOHMANN
        CHECK(test_support::message_of(f.last()) == fake);
#endif
    }
}

TEST_CASE("formatter: the token is stable and printable", "[formatter]")
{
    const auto &token = jspdlog::detail::process_token();
    CHECK(&token == &jspdlog::detail::process_token());
    CHECK(std::string(token.data(), 8) == "jspdlog:");
    for (const char c : token)
    {
        CHECK(c >= 0x20);
        CHECK(c < 0x7f);
    }
}

TEST_CASE("formatter: every header key off, with fields and message", "[formatter]")
{
    auto opts = deterministic_options();
    opts.logger_key = std::nullopt;
    opts.level_key = std::nullopt;
    const fixture f("t", opts);

    f.logger.info({"k", 1}, "x");
    CHECK(f.last() == R"({"k":1,"message":"x"})");
    f.logger.info({"k", 1});
    CHECK(f.last() == R"({"k":1})");
    f.logger.info("x");
    CHECK(f.last() == R"({"message":"x"})");
    f.logger.info(jspdlog::json_properties{});
    CHECK(f.last() == "{}");
    f.logger.spdlog_logger()->info("direct");
    CHECK(f.last() == R"({"message":"direct"})");
}

TEST_CASE("formatter: message_key renames the message for jspdlog and foreign calls", "[formatter]")
{
    auto opts = deterministic_options();
    opts.message_key = "msg\"";
    const fixture f("t", opts);

    f.logger.info({"k", 1}, "x");
    CHECK(f.last() == R"({"logger":"t","level":"info","k":1,"msg\"":"x"})");
    f.logger.spdlog_logger()->info("y");
    CHECK(f.last() == R"({"logger":"t","level":"info","msg\"":"y"})");
}

TEST_CASE("formatter: the header keys can be renamed", "[formatter]")
{
    auto opts = deterministic_options();
    opts.logger_key = "name";
    opts.level_key = "severity";
    const fixture f("t", opts);
    f.logger.warn("x");
    CHECK(f.last() == R"({"name":"t","severity":"warning","message":"x"})");
}

TEST_CASE("formatter: source_key writes the base name of the call site", "[formatter]")
{
    auto opts = deterministic_options();
    opts.source_key = "source";
    const fixture f("t", opts);

    SPDLOG_LOGGER_INFO(f.logger.spdlog_logger(), "from a macro");
    REQUIRE(f.sink->lines.size() == 1);
    const std::string line = f.last();
    INFO(line);
    CHECK(line.find(R"("source":{"file":"test_formatter.cpp","line":)") != std::string::npos);
    CHECK(line.find(R"(,"function":)") != std::string::npos);
    CHECK(line.find(R"(,"message":"from a macro"})") != std::string::npos);
    CHECK(test_support::is_single_json_object(line));

    // Both kinds of path separator are removed, and a missing function becomes an empty string.
    const char *const path = "dir\\sub/file.cpp";
    f.logger.spdlog_logger()->log(spdlog::source_loc{path, 7, nullptr}, jspdlog::level::info, "x");
    const std::string expected =
        R"({"logger":"t","level":"info","source":{"file":"file.cpp","line":7,"function":""},"message":"x"})";
    CHECK(f.last() == expected);

    // A different file name is picked up after the cache was filled.
    f.logger.spdlog_logger()->log(spdlog::source_loc{"other.cpp", 8, "fn"}, jspdlog::level::info, "y");
    const std::string expected_other =
        R"({"logger":"t","level":"info","source":{"file":"other.cpp","line":8,"function":"fn"},"message":"y"})";
    CHECK(f.last() == expected_other);

    // A file name buffer reused with different text is read again.
    std::string reused = "first.cpp";
    f.logger.spdlog_logger()->log(spdlog::source_loc{reused.c_str(), 1, "f"}, jspdlog::level::info, "a");
    reused.replace(0, 5, "other");
    f.logger.spdlog_logger()->log(spdlog::source_loc{reused.c_str(), 2, "f"}, jspdlog::level::info, "b");
    CHECK(f.last().find(R"("file":"other.cpp","line":2)") != std::string::npos);

    // A json_logger call has no call site yet.
    f.logger.info("z");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"z"})");
}

TEST_CASE("formatter: source_key is off by default", "[formatter]")
{
    const fixture f;
    SPDLOG_LOGGER_INFO(f.logger.spdlog_logger(), "x");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"x"})");
}

TEST_CASE("formatter: timestamps are RFC 3339 with milliseconds", "[formatter]")
{
    auto opts = deterministic_options();
    opts.timestamp_key = "ts";

    SECTION("utc ends in Z")
    {
        opts.time_type = spdlog::pattern_time_type::utc;
        jspdlog::json_formatter formatter(opts);
        const std::string line = format_payload(formatter, "x");
        INFO(line);
        CHECK(std::regex_search(line, std::regex(R"re(^\{"ts":"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z",)re")));
    }
    SECTION("local time ends in an offset")
    {
        opts.time_type = spdlog::pattern_time_type::local;
        jspdlog::json_formatter formatter(opts);
        const std::string line = format_payload(formatter, "x");
        INFO(line);
        CHECK(
            std::regex_search(line, std::regex(R"re(^\{"ts":"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}[+-]\d\d:\d\d",)re"))
        );
    }
    SECTION("a known instant")
    {
        opts.time_type = spdlog::pattern_time_type::utc;
        jspdlog::json_formatter formatter(opts);
        spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
        // 2026-05-13T08:00:00.007Z
        msg.time = spdlog::log_clock::time_point{std::chrono::seconds(1778659200) + std::chrono::milliseconds(7)};
        spdlog::memory_buf_t buf;
        formatter.format(msg, buf);
        CHECK(std::string(buf.data(), buf.size()).rfind(R"({"ts":"2026-05-13T08:00:00.007Z",)", 0) == 0);

        // The cached second is rebuilt when the second changes.
        msg.time += std::chrono::milliseconds(1993);
        buf.clear();
        formatter.format(msg, buf);
        CHECK(std::string(buf.data(), buf.size()).rfind(R"({"ts":"2026-05-13T08:00:02.000Z",)", 0) == 0);
    }
#ifndef _WIN32 // The Windows C runtime does not convert times before the epoch.
    SECTION("times before the epoch")
    {
        opts.time_type = spdlog::pattern_time_type::utc;
        jspdlog::json_formatter formatter(opts);
        spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
        // 1969-12-31T23:59:59.250Z, whose second is -1.
        msg.time = spdlog::log_clock::time_point{-std::chrono::milliseconds(750)};
        spdlog::memory_buf_t buf;
        formatter.format(msg, buf);
        CHECK(std::string(buf.data(), buf.size()).rfind(R"({"ts":"1969-12-31T23:59:59.250Z",)", 0) == 0);
    }
#endif
}

TEST_CASE("formatter: process and thread are numbers", "[formatter]")
{
    auto opts = deterministic_options();
    opts.process_key = "pid";
    opts.thread_key = "tid";
    jspdlog::json_formatter formatter(opts);
    const std::string line = format_payload(formatter, "x");
    INFO(line);
    CHECK(
        std::regex_search(
            line, std::regex(R"re(^\{"logger":"name","level":"info","pid":\d+,"tid":\d+,"message":"x"\}\n$)re")
        )
    );
}

TEST_CASE("formatter: eol is appended", "[formatter]")
{
    auto opts = no_header_options();
    SECTION("explicit")
    {
        opts.eol = "\r\n";
        jspdlog::json_formatter formatter(opts);
        CHECK(format_payload(formatter, "x") == "{\"message\":\"x\"}\r\n");
    }
    SECTION("empty")
    {
        opts.eol = "";
        jspdlog::json_formatter formatter(opts);
        CHECK(format_payload(formatter, "x") == "{\"message\":\"x\"}");
    }
    SECTION("platform default")
    {
        opts.eol = std::nullopt;
        jspdlog::json_formatter formatter(opts);
        CHECK(format_payload(formatter, "x") == std::string("{\"message\":\"x\"}") + spdlog::details::os::default_eol);
    }
}

TEST_CASE("formatter: clone keeps the options and the token", "[formatter]")
{
    const jspdlog::json_formatter formatter(no_header_options());
    const auto copy = formatter.clone();
    const auto &token = jspdlog::detail::process_token();
    std::string payload(token.data(), token.size());
    payload += R"(M00000006,"k":1"m")";
    const spdlog::details::log_msg msg("name", jspdlog::level::info, payload);
    spdlog::memory_buf_t buf;
    copy->format(msg, buf);
    CHECK(std::string(buf.data(), buf.size()) == "{\"k\":1,\"message\":\"m\"}\n");
}

TEST_CASE("formatter: a payload with fields but no message", "[formatter]")
{
    jspdlog::json_formatter formatter(no_header_options());
    const auto &token = jspdlog::detail::process_token();
    const std::string payload = std::string(token.data(), token.size()) + "F00000006,\"k\":1";
    CHECK(format_payload(formatter, payload) == "{\"k\":1}\n");
}
