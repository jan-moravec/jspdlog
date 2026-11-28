// json_formatter on its own: header combinations, timestamps, call sites, the line terminator and payloads that do not
// come from a json_logger.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <spdlog/spdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>

#include <unistd.h>
#endif

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
    // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
    const std::string expected = R"({"logger":"t","level":"info","message":"a \"quoted\" {\"x\":1} line"})";
    CHECK(f.last() == expected);

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
        t.substr(0, 31),                                               // token prefix only
        t + "M",                                                       // header cut short
        t + "M0",                                                      // header cut short after the flags
        t + R"(M0zzzzzzzz,"a":1"m")",                                  // size not hex
        t + R"(M0FFFFFFFF,"a":1"m")",                                  // size not lowercase hex
        t + R"(M400000006,"a":1"m")",                                  // flags past the known bits
        t + R"(Ma00000006,"a":1"m")",                                  // flags past the known bits, as a letter
        t + R"(MA00000006,"a":1"m")",                                  // flags not lowercase hex
        t + R"(Mx00000006,"a":1"m")",                                  // flags not hex
        t + "M000000010,\"a\":1",                                      // size past the end
        t + "M000000006,\"a\":1",                                      // message kind without a message
        t + "F000000001,\"a\":1",                                      // F with trailing bytes
        t + "X000000006,\"a\":1",                                      // unknown kind
        t + "M000000000x",                                             // message not quoted
        t + "M000000000\"x",                                           // message without a closing quote
        t + "F000000004\"a\":",                                        // fields without a leading comma
        std::string("jspdlog:") + std::string(24, 'A') + "F000000000", // wrong token
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
    f.logger.info(jspdlog::fields{});
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
    // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
    const std::string expected_fields = R"({"logger":"t","level":"info","k":1,"msg\"":"x"})";
    const std::string expected_plain = R"({"logger":"t","level":"info","msg\"":"y"})";
    CHECK(f.last() == expected_fields);
    f.logger.spdlog_logger()->info("y");
    CHECK(f.last() == expected_plain);
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

    // A json_logger call records its own call site.
    f.logger.info("z");
    CHECK(f.last().find(R"("source":{"file":"test_formatter.cpp","line":)") != std::string::npos);
    CHECK(f.last().find(R"(,"message":"z"})") != std::string::npos);
}

TEST_CASE("formatter: the call site keys rename the object members", "[formatter]")
{
    auto opts = deterministic_options();
    opts.source_key = "source";
    opts.source_file_key = "f";
    opts.source_line_key = "l";
    opts.source_function_key = "fn";
    const fixture f("t", opts);
    f.logger.spdlog_logger()->log(spdlog::source_loc{"dir/a.cpp", 12, "run"}, jspdlog::level::info, "x");
    const std::string expected =
        R"({"logger":"t","level":"info","source":{"f":"a.cpp","l":12,"fn":"run"},"message":"x"})";
    CHECK(f.last() == expected);
}

TEST_CASE("formatter: source_flat writes the call site as fields of the line", "[formatter]")
{
    auto opts = deterministic_options();
    opts.source_key = "unused";
    opts.source_flat = true;
    const fixture f("t", opts);
    f.logger.spdlog_logger()->log(spdlog::source_loc{"dir/a.cpp", 12, "run"}, jspdlog::level::info, "x");
    const std::string expected =
        R"({"logger":"t","level":"info","file":"a.cpp","line":12,"function":"run","message":"x"})";
    CHECK(f.last() == expected);

    // A call through json_logger records its own site, and bound fields follow the call site.
    f.logger.with({"k", 1}).info({"n", 2}, "y");
    const std::string line = f.last();
    CHECK(line.find(R"("level":"info","file":"test_formatter.cpp","line":)") != std::string::npos);
    CHECK(line.find(R"(,"k":1,"n":2,"message":"y"})") != std::string::npos);

    // The renamed keys, and a line without a call site.
    opts.source_file_key = "log.origin.file.name";
    opts.source_line_key = "log.origin.file.line";
    opts.source_function_key = "log.origin.function";
    const fixture g("t", opts);
    g.logger.spdlog_logger()->log(spdlog::source_loc{"a.cpp", 3, "fn"}, jspdlog::level::info, "x");
    const std::string renamed =
        R"({"logger":"t","level":"info","log.origin.file.name":"a.cpp","log.origin.file.line":3,)"
        R"("log.origin.function":"fn","message":"x"})";
    CHECK(g.last() == renamed);
    g.logger.spdlog_logger()->log(spdlog::source_loc{}, jspdlog::level::info, "y");
    CHECK(g.last() == R"({"logger":"t","level":"info","message":"y"})");

    opts.source_key = std::nullopt;
    const fixture off("t", opts);
    off.logger.info("z");
    CHECK(off.last() == R"({"logger":"t","level":"info","message":"z"})");
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
        // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
        const std::regex utc(R"re(^\{"ts":"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z",)re");
        CHECK(std::regex_search(line, utc));
    }
    SECTION("local time ends in an offset")
    {
        opts.time_type = spdlog::pattern_time_type::local;
        jspdlog::json_formatter formatter(opts);
        const std::string line = format_payload(formatter, "x");
        INFO(line);
        // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
        const std::regex local(R"re(^\{"ts":"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}[+-]\d\d:\d\d",)re");
        CHECK(std::regex_search(line, local));
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

TEST_CASE("formatter: process is the id of the current process", "[formatter]")
{
    auto opts = no_header_options();
    opts.process_key = "pid";
    jspdlog::json_formatter formatter(opts);
    const auto expected = [](auto pid) {
        return R"({"pid":)" + std::to_string(pid) +
               R"(,"message":"x"})"
               "\n";
    };
    // Twice, so the second line comes from the cache.
    CHECK(format_payload(formatter, "x") == expected(spdlog::details::os::pid()));
    CHECK(format_payload(formatter, "x") == expected(spdlog::details::os::pid()));

#ifndef _WIN32
    // A forked child must not write the parent's cached id. The child reports through its exit status only.
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0)
    {
        const bool own = format_payload(formatter, "x") == expected(::getpid());
        ::_exit(own ? 0 : 1);
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    CHECK(format_payload(formatter, "x") == expected(::getpid()));
#endif
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
    payload += R"(M000000006,"k":1"m")";
    const spdlog::details::log_msg msg("name", jspdlog::level::info, payload);
    spdlog::memory_buf_t buf;
    copy->format(msg, buf);
    CHECK(std::string(buf.data(), buf.size()) == "{\"k\":1,\"message\":\"m\"}\n");
}

TEST_CASE("formatter: a payload with fields but no message", "[formatter]")
{
    jspdlog::json_formatter formatter(no_header_options());
    const auto &token = jspdlog::detail::process_token();
    const std::string payload = std::string(token.data(), token.size()) + "F000000006,\"k\":1";
    CHECK(format_payload(formatter, payload) == "{\"k\":1}\n");
}

namespace
{

// A payload of this process with the given flags digit and fields, and no message.
std::string payload_of(char flags, const std::string &fields)
{
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    const auto &token = jspdlog::detail::process_token();
    std::string payload(token.data(), token.size());
    payload += 'F';
    payload += flags;
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        payload += HEX_DIGITS[(fields.size() >> static_cast<unsigned>(shift)) & 0xFU];
    }
    return payload + fields;
}

jspdlog::json_options renamed_exception_keys()
{
    jspdlog::json_options opts = no_header_options();
    opts.error_key = "E";
    opts.error_type_key = "T\"";
    opts.error_message_key = "M";
    opts.error_cause_key = "C";
    return opts;
}

} // namespace

TEST_CASE("formatter: key markers become the exception keys of the sink", "[formatter]")
{
    jspdlog::json_formatter formatter(renamed_exception_keys());
    const std::string fields = ",\x01"
                               "E{\x01"
                               "T1,\x01"
                               "M2,\x01"
                               "C{\x01"
                               "T3}}";
    // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
    const std::string expected = R"({"E":{"T\"":1,"M":2,"C":{"T\"":3}}})"
                                 "\n";
    CHECK(format_payload(formatter, payload_of('1', fields)) == expected);
    CHECK(format_payload(formatter, payload_of('3', fields)) == expected);
}

TEST_CASE("formatter: markers are not replaced in a payload without the flag", "[formatter]")
{
    jspdlog::json_formatter formatter(renamed_exception_keys());
    const std::string fields = ",\"a\":{\x01"
                               "T1}";
    CHECK(format_payload(formatter, payload_of('0', fields)) == "{" + fields.substr(1) + "}\n");
}

TEST_CASE("formatter: a damaged payload with the real token does not break the formatter", "[formatter]")
{
    // Only corrupted memory or a fuzzer gives these. The line need not be valid JSON, but every byte is accounted for.
    jspdlog::json_formatter formatter(renamed_exception_keys());
    SECTION("a marker cut short at the end of the fields")
    {
        const std::string fields = ",\"a\":1,\x01";
        for (const char flags : {'1', '2', '3'})
        {
            CHECK(format_payload(formatter, payload_of(flags, fields)) == "{\"a\":1,\x01}\n");
        }
    }
    SECTION("an unknown marker letter")
    {
        const std::string fields = ",\x01Z1,\x01"
                                   "E2";
        CHECK(format_payload(formatter, payload_of('1', fields)) == "{\x01Z1,\"E\":2}\n");
        CHECK(format_payload(formatter, payload_of('3', fields)) == "{\x01Z1,\"E\":2}\n");
    }
    SECTION("fields that end inside a key or a value")
    {
        for (const std::string fields :
             {",", ",\"", ",\"a", ",\"a\"", ",\"a\":", R"(,"a":"b)", R"(,"a":"b\)", R"(,"a":[{"b":1)", ",\x01"})
        {
            for (const char flags : {'0', '1', '2', '3'})
            {
                CHECK(format_payload(formatter, payload_of(flags, fields)) == "{" + fields.substr(1) + "}\n");
            }
        }
    }
    SECTION("an exception flag without an exception")
    {
        const std::string fields = R"(,"E":1,"E":2)";
        CHECK(format_payload(formatter, payload_of('3', fields)) == "{\"E\":1,\"E\":2}\n");
    }
    SECTION("no fields")
    {
        CHECK(format_payload(formatter, payload_of('3', "")) == "{}\n");
    }
}

TEST_CASE("formatter: the exception is the only field under the error key", "[formatter]")
{
    jspdlog::json_formatter formatter(renamed_exception_keys());
    const std::string error = ",\x01"
                              "E9";
    const auto line = [&](const std::string &fields) { return format_payload(formatter, payload_of('3', fields)); };

    CHECK(line(",\"a\":1" + error + ",\"b\":2") == "{\"a\":1,\"E\":9,\"b\":2}\n");
    // It moves to the first other field with the key, whether that comes before or after it.
    CHECK(line(",\"E\":1,\"a\":2" + error) == "{\"E\":9,\"a\":2}\n");
    CHECK(line(error + ",\"a\":2,\"E\":1,\"b\":3") == "{\"a\":2,\"E\":9,\"b\":3}\n");
    CHECK(line(",\"E\":1,\"E\":2" + error + ",\"E\":3,\"z\":4") == "{\"E\":9,\"z\":4}\n");
    // Only top-level keys count, whatever the values hold, including raw JSON with white space.
    const std::string raw = R"(,"a": [ 1, "]", {"E": 2} ] ,"b":",\"E\":3" ,"c": true )";
    std::string field_after = raw;
    field_after.append(error).append(R"(,"E":0)");
    std::string field_before = raw;
    field_before.append(R"(,"E":0)").append(error);
    std::string expected = "{";
    expected.append(raw, 1).append(",\"E\":9}\n");
    CHECK(line(field_after) == expected);
    CHECK(line(field_before) == expected);
}

namespace
{

// The line the formatter writes for a message at the given time, with only the timestamp header.
std::string format_at(jspdlog::json_options opts, spdlog::log_clock::time_point time)
{
    opts.logger_key = std::nullopt;
    opts.level_key = std::nullopt;
    opts.timestamp_key = "ts";
    opts.time_type = spdlog::pattern_time_type::utc;
    jspdlog::json_formatter formatter(opts);
    spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
    msg.time = time;
    spdlog::memory_buf_t buf;
    formatter.format(msg, buf);
    const std::string line(buf.data(), buf.size());
    // The payload "x" is not from a json_logger, so the line also holds the message. Keep the header part only.
    const auto cut = line.find(",\"message\"");
    return line.substr(0, cut) + "}\n";
}

// The nanoseconds of the second that a time point falls in, found without the formatter's arithmetic.
long long nanos_of_second(spdlog::log_clock::time_point time)
{
    constexpr long long NANOS_PER_SECOND = 1'000'000'000;
    const long long nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count() % NANOS_PER_SECOND;
    return nanos < 0 ? nanos + NANOS_PER_SECOND : nanos;
}

// The nanoseconds as nine decimal digits, cut to the first digits.
std::string fraction(long long nanos, std::size_t digits)
{
    std::string text = std::to_string(nanos);
    text.insert(0, 9 - text.size(), '0');
    return text.substr(0, digits);
}

// 2026-05-13T08:00:00Z plus 123456789 ns, cut to the resolution of the clock.
spdlog::log_clock::time_point known_time()
{
    const auto offset = std::chrono::seconds(1778659200) + std::chrono::nanoseconds(123456789);
    return spdlog::log_clock::time_point{std::chrono::duration_cast<spdlog::log_clock::duration>(offset)};
}

} // namespace

TEST_CASE("formatter: level names default to the names of spdlog", "[formatter]")
{
    const fixture f;
    f.logger.warn("w");
    CHECK(f.last() == R"({"logger":"t","level":"warning","message":"w"})");

    // Every level, through the function that spdlog's own formatters use, so SPDLOG_LEVEL_NAMES keeps working.
    for (int i = 0; i < static_cast<int>(jspdlog::level::off); ++i)
    {
        const auto lvl = static_cast<jspdlog::level>(i);
        f.logger.log(lvl, "x");
        const std::string expected = std::string(R"({"logger":"t","level":")") +
                                     std::string(jspdlog::detail::level_name(lvl)) + R"(","message":"x"})";
        CHECK(f.last() == expected);
    }
}

TEST_CASE("formatter: level_names replaces the names", "[formatter]")
{
    auto opts = deterministic_options();
    opts.level_names = {{"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"}};
    const fixture f("t", opts);

    f.logger.log(jspdlog::level::trace, "a");
    CHECK(f.last() == R"({"logger":"t","level":"TRACE","message":"a"})");
    f.logger.warn("b");
    CHECK(f.last() == R"({"logger":"t","level":"WARN","message":"b"})");
    f.logger.critical("c");
    CHECK(f.last() == R"({"logger":"t","level":"FATAL","message":"c"})");
    f.logger.spdlog_logger()->error("d");
    CHECK(f.last() == R"({"logger":"t","level":"ERROR","message":"d"})");
}

TEST_CASE("formatter: level names are escaped", "[formatter]")
{
    auto opts = deterministic_options();
    opts.level_names = {{"t\"", "d", "i", "w", "e", "c", "o"}};
    const fixture f("t", opts);
    f.logger.log(jspdlog::level::trace, "a");
    // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
    const std::string expected = R"({"logger":"t","level":"t\"","message":"a"})";
    CHECK(f.last() == expected);
}

TEST_CASE("formatter: severity_key writes a number after the level", "[formatter]")
{
    auto opts = deterministic_options();
    opts.severity_key = "severity";
    SECTION("default numbers")
    {
        const fixture f("t", opts);
        f.logger.log(jspdlog::level::trace, "a");
        CHECK(f.last() == R"({"logger":"t","level":"trace","severity":1,"message":"a"})");
        f.logger.debug("a");
        CHECK(f.last() == R"({"logger":"t","level":"debug","severity":5,"message":"a"})");
        f.logger.info("a");
        CHECK(f.last() == R"({"logger":"t","level":"info","severity":9,"message":"a"})");
        f.logger.warn("a");
        CHECK(f.last() == R"({"logger":"t","level":"warning","severity":13,"message":"a"})");
        f.logger.error("a");
        CHECK(f.last() == R"({"logger":"t","level":"error","severity":17,"message":"a"})");
        f.logger.critical("a");
        CHECK(f.last() == R"({"logger":"t","level":"critical","severity":21,"message":"a"})");
    }
    SECTION("custom numbers and names")
    {
        opts.severity_numbers = {0, 10, 20, 30, 40, 50, -1};
        opts.level_names = {{"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"}};
        const fixture f("t", opts);
        f.logger.warn("a");
        CHECK(f.last() == R"({"logger":"t","level":"WARN","severity":30,"message":"a"})");
        f.logger.log(jspdlog::level::trace, "a");
        CHECK(f.last() == R"({"logger":"t","level":"TRACE","severity":0,"message":"a"})");
    }
    SECTION("without the level key")
    {
        opts.level_key = std::nullopt;
        const fixture f("t", opts);
        f.logger.info({"k", 1}, "a");
        CHECK(f.last() == R"({"logger":"t","severity":9,"k":1,"message":"a"})");
    }
    SECTION("between the level and the process")
    {
        opts.process_key = "pid";
        opts.timestamp_key = "ts";
        jspdlog::json_formatter formatter(opts);
        const std::string line = format_payload(formatter, "x");
        INFO(line);
        // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
        const std::regex order(R"re(^\{"ts":"[^"]*","logger":"name","level":"info","severity":9,"pid":\d+,)re");
        CHECK(std::regex_search(line, order));
    }
    SECTION("off by default")
    {
        const fixture f;
        f.logger.info("a");
        CHECK(f.last().find("severity") == std::string::npos);
    }
}

TEST_CASE("formatter: the timestamp formats", "[formatter]")
{
    const auto time = known_time();
    const long long nanos = nanos_of_second(time);
    auto opts = deterministic_options();

    SECTION("milliseconds, the default")
    {
        const std::string expected = R"({"ts":"2026-05-13T08:00:00.)" + fraction(nanos, 3) + "Z\"}\n";
        CHECK(format_at(opts, time) == expected);
        CHECK(fraction(nanos, 3) == "123");
    }
    SECTION("microseconds")
    {
        opts.timestamp_style = jspdlog::timestamp_format::rfc3339_micros;
        const std::string expected = R"({"ts":"2026-05-13T08:00:00.)" + fraction(nanos, 6) + "Z\"}\n";
        CHECK(format_at(opts, time) == expected);
        CHECK(fraction(nanos, 6) == "123456");
    }
    SECTION("nanoseconds")
    {
        opts.timestamp_style = jspdlog::timestamp_format::rfc3339_nanos;
        const std::string expected = R"({"ts":"2026-05-13T08:00:00.)" + fraction(nanos, 9) + "Z\"}\n";
        CHECK(format_at(opts, time) == expected);
        // The clock cuts the digits it cannot count: 123456789 with 1 ns, 123456700 with 100 ns, 123456000 with 1 us.
        CHECK(fraction(nanos, 6) == "123456");
    }
    SECTION("epoch milliseconds are a number")
    {
        opts.timestamp_style = jspdlog::timestamp_format::epoch_millis;
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
        CHECK(millis == 1778659200123);
        CHECK(format_at(opts, time) == "{\"ts\":" + std::to_string(millis) + "}\n");
    }
    SECTION("epoch milliseconds ignore the time type")
    {
        opts.timestamp_style = jspdlog::timestamp_format::epoch_millis;
        opts.timestamp_key = "ts";
        opts.time_type = spdlog::pattern_time_type::local;
        jspdlog::json_formatter formatter(opts);
        spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
        msg.time = time;
        spdlog::memory_buf_t buf;
        formatter.format(msg, buf);
        const std::string line(buf.data(), buf.size());
        CHECK(line.rfind("{\"ts\":1778659200123,", 0) == 0);
    }
    SECTION("the cached second is rebuilt, and the fraction changes within a second")
    {
        opts.timestamp_style = jspdlog::timestamp_format::rfc3339_micros;
        opts.timestamp_key = "ts";
        opts.time_type = spdlog::pattern_time_type::utc;
        jspdlog::json_formatter formatter(opts);
        spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
        spdlog::memory_buf_t buf;
        msg.time = spdlog::log_clock::time_point{std::chrono::seconds(1778659200) + std::chrono::microseconds(5)};
        formatter.format(msg, buf);
        CHECK(std::string(buf.data(), buf.size()).rfind(R"({"ts":"2026-05-13T08:00:00.000005Z",)", 0) == 0);
        msg.time = spdlog::log_clock::time_point{std::chrono::seconds(1778659201) + std::chrono::microseconds(999999)};
        buf.clear();
        formatter.format(msg, buf);
        CHECK(std::string(buf.data(), buf.size()).rfind(R"({"ts":"2026-05-13T08:00:01.999999Z",)", 0) == 0);
    }
}

TEST_CASE("formatter: times before the epoch round down", "[formatter]")
{
    // -750.123456 ms is 249.876544 ms into 1969-12-31T23:59:59, and -751 as a floor in milliseconds.
    const spdlog::log_clock::time_point time{
        std::chrono::duration_cast<spdlog::log_clock::duration>(-std::chrono::nanoseconds(750'123'456))
    };
    auto opts = deterministic_options();

    SECTION("epoch milliseconds")
    {
        opts.timestamp_style = jspdlog::timestamp_format::epoch_millis;
        CHECK(format_at(opts, time) == "{\"ts\":-751}\n");
    }
    const long long nanos = nanos_of_second(time);
    SECTION("milliseconds")
    {
        const std::string expected = R"({"ts":"1969-12-31T23:59:59.)" + fraction(nanos, 3) + "Z\"}\n";
        CHECK(format_at(opts, time) == expected);
        CHECK(fraction(nanos, 3) == "249");
    }
    SECTION("microseconds")
    {
        opts.timestamp_style = jspdlog::timestamp_format::rfc3339_micros;
        const std::string expected = R"({"ts":"1969-12-31T23:59:59.)" + fraction(nanos, 6) + "Z\"}\n";
        CHECK(format_at(opts, time) == expected);
        CHECK((fraction(nanos, 6) == "249876" || fraction(nanos, 6) == "249877")); // 1 us clocks truncate toward zero
    }
    SECTION("nanoseconds")
    {
        opts.timestamp_style = jspdlog::timestamp_format::rfc3339_nanos;
        const std::string expected = R"({"ts":"1969-12-31T23:59:59.)" + fraction(nanos, 9) + "Z\"}\n";
        CHECK(format_at(opts, time) == expected);
    }
}

TEST_CASE("formatter: break_down covers RFC 3339 and nothing else", "[formatter]")
{
    constexpr long long LAST_SECOND = 253402300799;  // 9999-12-31T23:59:59Z
    constexpr long long FIRST_SECOND = -62167219200; // 0000-01-01T00:00:00Z
    jspdlog::detail::broken_down parts;

    CHECK(jspdlog::detail::break_down(LAST_SECOND, false, parts));
    CHECK((parts.year == 9999 && parts.month == 12 && parts.day == 31));
    CHECK((parts.hour == 23 && parts.minute == 59 && parts.second == 59 && parts.utc));
    CHECK_FALSE(jspdlog::detail::break_down(LAST_SECOND + 1, false, parts));

    CHECK(jspdlog::detail::break_down(FIRST_SECOND, false, parts));
    CHECK((parts.year == 0 && parts.month == 1 && parts.day == 1 && parts.hour == 0));
    CHECK_FALSE(jspdlog::detail::break_down(FIRST_SECOND - 1, false, parts));

    CHECK(jspdlog::detail::break_down(-86400, false, parts)); // before 1970, also on Windows
    CHECK((parts.year == 1969 && parts.month == 12 && parts.day == 31 && parts.utc));

    // Far beyond what any C library converts, in local time too.
    for (const bool local : {false, true})
    {
        CHECK_FALSE(jspdlog::detail::break_down(std::numeric_limits<long long>::max(), local, parts));
        CHECK(parts.utc);
        CHECK_FALSE(jspdlog::detail::break_down(std::numeric_limits<long long>::min(), local, parts));
        CHECK(parts.utc);
    }
}

TEST_CASE("formatter: a timestamp before 1970 is valid in UTC and local time", "[formatter]")
{
    const auto before = spdlog::log_clock::time_point{} - std::chrono::hours(24);
    CHECK(format_at(deterministic_options(), before) == "{\"ts\":\"1969-12-31T00:00:00.000Z\"}\n");

    // Local time, or UTC where the C library cannot convert the second, as on Windows.
    auto opts = no_header_options();
    opts.timestamp_key = "ts";
    opts.time_type = spdlog::pattern_time_type::local;
    jspdlog::json_formatter formatter(opts);
    spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
    msg.time = before;
    spdlog::memory_buf_t buf;
    formatter.format(msg, buf);
    const std::string line(buf.data(), buf.size());
    INFO(line);
    const std::regex local(R"re(^\{"ts":"1969-12-3[01]T\d\d:\d\d:\d\d\.000(Z|[+-]\d\d:\d\d)",)re");
    CHECK(std::regex_search(line, local));
#ifdef _WIN32
    CHECK(line.rfind(R"({"ts":"1969-12-31T00:00:00.000Z",)", 0) == 0);
#endif
}

TEST_CASE("formatter: the ends of the clock give a valid line", "[formatter]")
{
    for (const auto time : {spdlog::log_clock::time_point::min(), spdlog::log_clock::time_point::max()})
    {
        for (const auto style : {jspdlog::timestamp_format::rfc3339_nanos, jspdlog::timestamp_format::epoch_millis})
        {
            auto opts = deterministic_options();
            opts.timestamp_style = style;
            std::string line = format_at(opts, time);
            line.pop_back(); // the line terminator
            INFO(line);
            CHECK(test_support::is_single_json_object(line));
        }
    }
}

TEST_CASE("formatter: options that would write a key twice are rejected", "[formatter]")
{
    jspdlog::json_options o;
    const auto rejected = [](const jspdlog::json_options &options) {
        CHECK_THROWS_AS(jspdlog::json_formatter(options), std::invalid_argument);
    };

    SECTION("two header keys")
    {
        o.level_key = "logger";
        rejected(o);
    }
    SECTION("a header key and the message key")
    {
        o.message_key = "timestamp";
        rejected(o);
    }
    SECTION("a header key and the error key")
    {
        o.error_key = "level";
        rejected(o);
    }
    SECTION("the message key and the error key")
    {
        o.error_key = "message";
        rejected(o);
    }
    SECTION("the severity key and the source key")
    {
        o.severity_key = "source";
        o.source_key = "source";
        rejected(o);
    }
    SECTION("a flat call site key and a header key")
    {
        o.source_key = "unused";
        o.source_flat = true;
        o.source_line_key = "thread";
        rejected(o);
    }
    SECTION("two flat call site keys")
    {
        o.source_key = "unused";
        o.source_flat = true;
        o.source_function_key = "file";
        rejected(o);
    }
    SECTION("two keys of the call site object")
    {
        o.source_key = "source";
        o.source_function_key = "line";
        rejected(o);
    }
    SECTION("the type and message keys of an exception")
    {
        o.error_type_key = "message";
        rejected(o);
    }
    SECTION("the type and cause keys of an exception")
    {
        o.error_type_key = "cause";
        rejected(o);
    }
    SECTION("the message and cause keys of an exception")
    {
        o.error_cause_key = "message";
        rejected(o);
    }
}

TEST_CASE("formatter: keys in different objects, and keys that are off, may be equal", "[formatter]")
{
    jspdlog::json_options o;
    SECTION("the defaults")
    {
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
    SECTION("a header key that is off")
    {
        o.level_key = std::nullopt;
        o.message_key = "level";
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
    SECTION("the call site object and the line")
    {
        o.source_key = "source";
        o.source_file_key = "level";
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
    SECTION("the text of source_key is not used in flat mode")
    {
        o.source_key = "level";
        o.source_flat = true;
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
    SECTION("the call site is off")
    {
        o.source_file_key = "line";
        CHECK_NOTHROW(jspdlog::json_formatter(o));
        o.source_flat = true;
        o.source_line_key = "level";
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
    SECTION("the exception object and the line")
    {
        o.error_type_key = "level";
        o.error_cause_key = "logger";
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
    SECTION("level names and severity numbers")
    {
        o.level_names = {{"x", "x", "x", "x", "x", "x", "x"}};
        o.severity_numbers = {1, 1, 1, 1, 1, 1, 1};
        CHECK_NOTHROW(jspdlog::json_formatter(o));
    }
}

TEST_CASE("formatter: everything that creates a formatter checks the options", "[formatter]")
{
    jspdlog::json_options o;
    o.level_key = "logger";
    std::string what;
    try
    {
        const jspdlog::json_formatter formatter(o);
    }
    catch (const std::invalid_argument &ex)
    {
        what = ex.what();
    }
    CHECK(what == "jspdlog: json_options key \"logger\" is used twice");

    const auto sink = std::make_shared<test_support::capture_sink>();
    CHECK_THROWS_AS(jspdlog::json_logger("t", sink, o), std::invalid_argument);
    CHECK_THROWS_AS(jspdlog::json_logger("t", {sink, sink}, o), std::invalid_argument);

    // A rejected call leaves the formatter that was installed.
    const auto logger = test_support::make_plain_logger("t", sink);
    CHECK_THROWS_AS(jspdlog::json_logger::adopt(logger, o), std::invalid_argument);
    CHECK_THROWS_AS(jspdlog::install_json_formatter(*logger, o), std::invalid_argument);
    CHECK_THROWS_AS(jspdlog::install_json_formatter(*sink, o), std::invalid_argument);
    logger->info("x");
    CHECK(sink->last() == R"({"logger":"t","level":"info","message":"x"})");

    jspdlog::error_report_options report;
    report.format = o;
    CHECK_THROWS_AS(jspdlog::make_error_handler("t", report), std::invalid_argument);
}

#ifdef _MSC_VER // 100 ns ticks reach about 29000 years either side of 1970. Nanosecond clocks end in 2262.
TEST_CASE("formatter: a year outside 0000 to 9999 is null", "[formatter]")
{
    const auto opts = deterministic_options();
    const auto after = spdlog::log_clock::time_point{std::chrono::seconds(253402300800)}; // 10000-01-01
    CHECK(format_at(opts, after) == "{\"ts\":null}\n");
    // The cached second is rebuilt after a null, and a null after a string.
    jspdlog::json_options cached = no_header_options();
    cached.timestamp_key = "ts";
    cached.time_type = spdlog::pattern_time_type::utc;
    jspdlog::json_formatter formatter(cached);
    spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
    std::vector<std::string> lines;
    for (const auto time : {after, after, after - std::chrono::seconds(1), after})
    {
        msg.time = time;
        spdlog::memory_buf_t buf;
        formatter.format(msg, buf);
        lines.emplace_back(buf.data(), buf.size());
    }
    const std::string null_line = "{\"ts\":null,\"message\":\"x\"}\n";
    CHECK(lines[0] == null_line);
    CHECK(lines[1] == null_line);
    CHECK(lines[2] == "{\"ts\":\"9999-12-31T23:59:59.000Z\",\"message\":\"x\"}\n");
    CHECK(lines[3] == null_line);
}
#endif
