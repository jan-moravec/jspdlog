// The schema presets: one exact line each, from a message with a fixed time, the keys that a parsed line has, and how
// an exception call is written under each.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <set>
#include <stdexcept>
#include <string>

namespace
{

// 2026-11-25T10:20:30.123456Z, a time that a clock counting microseconds (libc++) can hold.
constexpr std::int64_t FIXED_SECONDS = 1795602030;
constexpr std::int64_t FIXED_NANOS = 123456000;
constexpr unsigned FIXED_THREAD = 77;

// Formats a plain message at the error level with a fixed time, thread and call site.
std::string format_fixed(const jspdlog::json_options &options)
{
    jspdlog::json_formatter formatter(options);
    spdlog::details::log_msg msg("name", jspdlog::level::err, "hello");
    msg.time = spdlog::log_clock::time_point{std::chrono::duration_cast<spdlog::log_clock::duration>(
        std::chrono::seconds(FIXED_SECONDS) + std::chrono::nanoseconds(FIXED_NANOS)
    )};
    msg.thread_id = FIXED_THREAD;
    msg.source = spdlog::source_loc{"dir/a.cpp", 12, "fn"};
    spdlog::memory_buf_t buf;
    formatter.format(msg, buf);
    std::string line(buf.data(), buf.size());
    REQUIRE(!line.empty());
    REQUIRE(line.back() == '\n');
    line.pop_back();
    return line;
}

// The id of the process stands in for @PID@ in the expected line.
std::string with_pid(std::string expected)
{
    const std::string pid = std::to_string(jspdlog::detail::process_id());
    for (auto at = expected.find("@PID@"); at != std::string::npos; at = expected.find("@PID@", at))
    {
        expected.replace(at, 5, pid);
    }
    return expected;
}

#ifdef JSPDLOG_TEST_NLOHMANN
std::set<std::string> keys_of(const std::string &line)
{
    const auto parsed = nlohmann::json::parse(line);
    std::set<std::string> keys;
    for (const auto &item : parsed.items())
    {
        keys.insert(item.key());
    }
    return keys;
}
#endif

} // namespace

TEST_CASE("presets: ecs", "[presets]")
{
    auto options = jspdlog::presets::ecs();
    options.source_key = "source";
    const std::string expected = with_pid(
        R"({"@timestamp":"2026-11-25T10:20:30.123Z","log.logger":"name","log.level":"error","process.pid":@PID@,)"
        R"("process.thread.id":77,"log.origin.file.name":"a.cpp","log.origin.file.line":12,)"
        R"("log.origin.function":"fn","message":"hello"})"
    );
    CHECK(format_fixed(options) == expected);

    // The call site stays off until it is asked for.
    const std::string off = format_fixed(jspdlog::presets::ecs());
    CHECK(off.find("log.origin") == std::string::npos);
#ifdef JSPDLOG_TEST_NLOHMANN
    const std::set<std::string> keys{
        "@timestamp",
        "log.logger",
        "log.level",
        "process.pid",
        "process.thread.id",
        "log.origin.file.name",
        "log.origin.file.line",
        "log.origin.function",
        "message",
    };
    CHECK(keys_of(format_fixed(options)) == keys);
#endif
}

TEST_CASE("presets: gcp", "[presets]")
{
    const std::string expected = with_pid(
        R"({"time":"2026-11-25T10:20:30.123456000Z","logger":"name","severity":"ERROR","process":@PID@,"thread":77,)"
        R"("message":"hello"})"
    );
    CHECK(format_fixed(jspdlog::presets::gcp()) == expected);

    // The call site goes where Cloud Logging looks for it, with the member names of its field.
    auto options = jspdlog::presets::gcp();
    options.source_key = "logging.googleapis.com/sourceLocation";
    const std::string with_source = with_pid(
        R"({"time":"2026-11-25T10:20:30.123456000Z","logger":"name","severity":"ERROR","process":@PID@,"thread":77,)"
        R"("logging.googleapis.com/sourceLocation":{"file":"a.cpp","line":12,"function":"fn"},"message":"hello"})"
    );
    CHECK(format_fixed(options) == with_source);

    // Every level has a name Cloud Logging knows.
    const auto names = jspdlog::presets::gcp().level_names;
    REQUIRE(names.has_value());
    const std::set<std::string> known{"DEFAULT", "DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "CRITICAL"};
    for (const std::string &name : names.value_or(jspdlog::presets::level_name_table{}))
    {
        CHECK(known.count(name) == 1);
    }
#ifdef JSPDLOG_TEST_NLOHMANN
    CHECK(
        keys_of(format_fixed(jspdlog::presets::gcp())) ==
        std::set<std::string>{"time", "logger", "severity", "process", "thread", "message"}
    );
#endif
}

TEST_CASE("presets: datadog", "[presets]")
{
    const std::string expected =
        R"({"timestamp":1795602030123,"logger.name":"name","status":"error","logger.thread_name":77,)"
        R"("message":"hello"})";
    CHECK(format_fixed(jspdlog::presets::datadog()) == expected);
#ifdef JSPDLOG_TEST_NLOHMANN
    CHECK(
        keys_of(format_fixed(jspdlog::presets::datadog())) ==
        std::set<std::string>{"timestamp", "logger.name", "status", "logger.thread_name", "message"}
    );
#endif
}

TEST_CASE("presets: otel", "[presets]")
{
    auto options = jspdlog::presets::otel();
    options.source_key = "source";
    const std::string expected = with_pid(
        R"({"timestamp":"2026-11-25T10:20:30.123456000Z","logger":"name","severity_text":"ERROR","severity_number":17,)"
        R"("process.pid":@PID@,"thread.id":77,"code.file.path":"a.cpp","code.line.number":12,)"
        R"("code.function.name":"fn","body":"hello"})"
    );
    CHECK(format_fixed(options) == expected);
    CHECK(format_fixed(jspdlog::presets::otel()).find("code.") == std::string::npos);
#ifdef JSPDLOG_TEST_NLOHMANN
    const std::set<std::string> keys{
        "timestamp",
        "logger",
        "severity_text",
        "severity_number",
        "process.pid",
        "thread.id",
        "code.file.path",
        "code.line.number",
        "code.function.name",
        "body",
    };
    CHECK(keys_of(format_fixed(options)) == keys);
#endif
}

TEST_CASE("presets: no preset uses a key twice, with the call site off or on", "[presets]")
{
    for (auto options : {
             jspdlog::json_options{},
             jspdlog::presets::ecs(),
             jspdlog::presets::gcp(),
             jspdlog::presets::datadog(),
             jspdlog::presets::otel(),
         })
    {
        CHECK_NOTHROW(jspdlog::detail::validate(options));
        options.source_key = "source";
        CHECK_NOTHROW(jspdlog::detail::validate(options));
        options.severity_key = "severity_value";
        CHECK_NOTHROW(jspdlog::detail::validate(options));
    }
}

TEST_CASE("presets: the names of the levels", "[presets]")
{
    // The same level written under each preset's names: trace, debug, info, warn, error, critical.
    const auto name_of = [](const jspdlog::json_options &options, jspdlog::level lvl) {
        jspdlog::json_formatter formatter(options);
        const spdlog::details::log_msg msg("n", lvl, "x");
        spdlog::memory_buf_t buf;
        formatter.format(msg, buf);
        const std::string line(buf.data(), buf.size());
        const std::string key = *options.level_key;
        const auto at = line.find("\"" + key + "\":\"");
        REQUIRE(at != std::string::npos);
        const auto from = at + key.size() + 4;
        return line.substr(from, line.find('"', from) - from);
    };
    using jspdlog::level;
    CHECK(name_of(jspdlog::presets::ecs(), level::warn) == "warn");
    CHECK(name_of(jspdlog::presets::gcp(), level::warn) == "WARNING");
    CHECK(name_of(jspdlog::presets::gcp(), level::trace) == "DEBUG");
    CHECK(name_of(jspdlog::presets::datadog(), level::trace) == "trace");
    CHECK(name_of(jspdlog::presets::otel(), level::warn) == "WARN");
    CHECK(name_of(jspdlog::presets::otel(), level::critical) == "FATAL");
}

TEST_CASE("presets: an exception call is written under the key of the schema", "[presets]")
{
    const std::runtime_error ex("boom");
    const auto line_of = [&](jspdlog::json_options options) {
        options.timestamp_key = std::nullopt;
        options.process_key = std::nullopt;
        options.thread_key = std::nullopt;
        options.logger_key = std::nullopt;
        options.eol = "\n";
        const auto sink = std::make_shared<test_support::capture_sink>();
        const jspdlog::json_logger logger("t", sink, options);
        logger.error(ex, {"k", 1}, "m");
        return sink->last();
    };
    CHECK(
        line_of(jspdlog::presets::ecs()) ==
        R"({"log.level":"error","error":{"type":"std::runtime_error","message":"boom"},"k":1,"message":"m"})"
    );
    CHECK(
        line_of(jspdlog::presets::gcp()) ==
        R"({"severity":"ERROR","error":{"type":"std::runtime_error","message":"boom"},"k":1,"message":"m"})"
    );
    CHECK(
        line_of(jspdlog::presets::datadog()) ==
        R"({"status":"error","error":{"kind":"std::runtime_error","message":"boom"},"k":1,"message":"m"})"
    );
    CHECK(
        line_of(jspdlog::presets::otel()) ==
        R"({"severity_text":"ERROR","severity_number":17,"exception":{"type":"std::runtime_error","message":"boom"},)"
        R"("k":1,"body":"m"})"
    );
}

TEST_CASE("presets: an exception that is a field value has the member names of the schema", "[presets]")
{
    auto options = jspdlog::presets::datadog();
    options.timestamp_key = std::nullopt;
    options.thread_key = std::nullopt;
    options.logger_key = std::nullopt;
    const auto sink = std::make_shared<test_support::capture_sink>();
    const jspdlog::json_logger logger("t", sink, options);
    const std::runtime_error ex("boom");
    const std::string expected = R"({"status":"info","e":{"kind":"std::runtime_error","message":"boom"}})";

    logger.info({"e", ex});
    CHECK(sink->last() == expected);
    const jspdlog::fields held{"e", ex};
    logger.info(held);
    CHECK(sink->last() == expected);
    logger.with({"e", ex}).info(jspdlog::fields{});
    CHECK(sink->last() == expected);
    CHECK(held.to_json() == R"({"e":{"type":"std::runtime_error","message":"boom"}})");
}

TEST_CASE("presets: a logger with a preset, bound schema fields and the call site", "[presets]")
{
    auto options = jspdlog::presets::ecs();
    options.source_key = "source";
    options.timestamp_key = std::nullopt;
    options.process_key = std::nullopt;
    options.thread_key = std::nullopt;
    const auto sink = std::make_shared<test_support::capture_sink>();
    const jspdlog::json_logger logger("svc", sink, options);
    logger.with({"ecs.version", "8.11.0"}).info({"user", 7}, "hi");
    const std::string line = sink->last();
    CHECK(
        line.find(
            R"({"log.logger":"svc","log.level":"info","log.origin.file.name":"test_presets.cpp","log.origin.file.line":)"
        ) == 0
    );
    CHECK(line.find(R"(,"log.origin.function":)") != std::string::npos);
    CHECK(line.find(R"(,"ecs.version":"8.11.0","user":7,"message":"hi"})") != std::string::npos);
}
