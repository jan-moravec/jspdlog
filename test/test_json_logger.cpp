// Ported from the original HID LoggingTest.cpp, adapted to:
//   * the new misuse-proof jspdlog::json_logger constructor
//     (name + spdlog sink instead of LoggingFactory::CreateStringStreamLogger)
//   * with_properties() instead of LoggingFactory::CopyLogger
//   * snake_case API throughout
//   * Catch2 v3 instead of GoogleTest

#include <jspdlog/jspdlog.h>

#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/ostream_sink.h>

#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{

// Builds an ostream-sink-backed json_logger. Trace-level so that every test
// call is emitted. Mirrors the original CreateStringStreamLogger helper.
jspdlog::json_logger make_stream_logger(const std::string &name, std::ostringstream &oss)
{
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger(name, std::move(sink));
    logger.set_level(spdlog::level::trace);
    return logger;
}

std::string add_endline(const std::string &text)
{
    return text + spdlog::details::os::default_eol;
}

bool matches(const std::string &actual, const std::string &pattern)
{
    return std::regex_match(actual, std::regex(pattern));
}

// Regex fragment that matches the ISO-8601 timestamp jspdlog emits. The
// timezone offset uses spdlog's %z, which renders as either `+02:00` or
// `+0200` depending on the platform, so the optional colon stays.
constexpr const char *kTimestampRe =
    R"([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2})";

// Builds a regex matching one full JSON log line. `name` and `level` are
// inserted as literals (caller must regex-escape any special characters);
// `tail` is whatever appears between the `thread` field and the closing
// `}` -- typically a leading comma plus one or more `,"key":value`
// fragments such as `,"message":"hi"` or `,"k":1,"message":"hi"`.
std::string expected_line(const std::string &name, const std::string &level, const std::string &tail)
{
    return add_endline(
        std::string(R"(\{"timestamp":")") + kTimestampRe + R"(","logger":")" + name + R"(","level":")" + level +
        R"(","process":[0-9]+,"thread":[0-9]+)" + tail + R"(\})"
    );
}

// Sink that always throws on sink_it_, used to drive spdlog's error handler.
// spdlog catches the exception, calls the error handler, and resumes.
class throwing_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit throwing_sink(std::string what)
        : what_(std::move(what))
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg & /*msg*/) override { throw std::runtime_error(what_); }
    void flush_() override {}

private:
    std::string what_;
};

} // namespace

TEST_CASE("json_logger: basic print methods", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("BasicPrintMethods", oss);

    logger.info("Test 1");
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "info", R"(,"message":"Test 1")")));
    oss.str({});

    logger.debug(1.23);
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "debug", R"(,"message":"1.23")")));
    oss.str({});

    logger.warn("{} {}", "Test", 3);
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "warning", R"(,"message":"Test 3")")));
    oss.str({});

    logger.info("Test {}", "Test");
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "info", R"(,"message":"Test Test")")));
    oss.str({});

    logger.trace(jspdlog::json_properties{"property", 1.23});
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "trace", R"(,"property":1.23)")));
    oss.str({});

    logger.error(jspdlog::json_properties{"property1", "abcd", "property2", true}, "{} 4", "Test");
    REQUIRE(matches(
        oss.str(),
        expected_line("BasicPrintMethods", "error", R"(,"property1":"abcd","property2":true,"message":"Test 4")")
    ));
}

TEST_CASE("json_logger: with_properties adds bound properties without affecting parent", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("AddingProperties", oss);
    auto logger1 = root.with_properties({"property1", 321});

    logger1.trace("Test 1");
    REQUIRE(matches(oss.str(), expected_line("AddingProperties", "trace", R"(,"property1":321,"message":"Test 1")")));
    oss.str({});

    {
        auto logger2 = logger1.with_properties({"property2", false});

        logger1.info("Test 2");
        REQUIRE(matches(oss.str(), expected_line("AddingProperties", "info", R"(,"property1":321,"message":"Test 2")"))
        );
        oss.str({});

        logger2.info("Test 3");
        REQUIRE(matches(
            oss.str(),
            expected_line("AddingProperties", "info", R"(,"property1":321,"property2":false,"message":"Test 3")")
        ));
        oss.str({});
    }

    logger1.info("Test 4");
    REQUIRE(matches(oss.str(), expected_line("AddingProperties", "info", R"(,"property1":321,"message":"Test 4")")));
}

TEST_CASE("json_logger: with_properties can override an existing key", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("ReplaceProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    auto logger2 = logger1.with_properties({"property", false});

    logger1.trace("Test");
    REQUIRE(matches(oss.str(), expected_line("ReplaceProperties", "trace", R"(,"property":321,"message":"Test")")));
    oss.str({});

    logger2.trace("Test");
    REQUIRE(matches(oss.str(), expected_line("ReplaceProperties", "trace", R"(,"property":false,"message":"Test")")));
}

TEST_CASE("json_logger: escapes special characters in messages", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("EscapingString", oss);

    logger.info("First line\nsecond line\n\ttabbed line\n\"quoted line\"");
    // Hoisted into a local because MSVC's macro preprocessor mishandles
    // raw strings containing both backslash-quote sequences and our long
    // timestamp regex when stringified by Catch2's REQUIRE.
    const std::string expected = expected_line(
        "EscapingString", "info", R"(,"message":"First line\\nsecond line\\n\\ttabbed line\\n\\\"quoted line\\\"")"
    );
    REQUIRE(matches(oss.str(), expected));
}

TEST_CASE("json_logger: a per-call property can override a bound property", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("DuplicateProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    logger1.set_level(spdlog::level::critical);

    logger1.critical("Test 1");
    REQUIRE(
        matches(oss.str(), expected_line("DuplicateProperties", "critical", R"(,"property":321,"message":"Test 1")"))
    );
    oss.str({});

    auto logger2 = logger1.with_properties({"property", false});
    logger2.critical("Test 2");
    REQUIRE(
        matches(oss.str(), expected_line("DuplicateProperties", "critical", R"(,"property":false,"message":"Test 2")"))
    );
    oss.str({});

    logger2.critical({"property", nullptr}, "Test 3");
    REQUIRE(
        matches(oss.str(), expected_line("DuplicateProperties", "critical", R"(,"property":null,"message":"Test 3")"))
    );
}

TEST_CASE("json_logger: logger name with special characters stays valid JSON", "[json_logger]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    // Name contains a literal quote, a backslash and a tab. These must be
    // JSON-escaped where they appear in the pattern, not interpolated raw.
    jspdlog::json_logger logger("weird\"name\\with\ttab", std::move(sink));
    logger.info("hi");

    // Hoist the expected fragment out of the REQUIRE macro: MSVC's stringifier
    // mishandles raw string literals containing backslashes when stringified
    // by the Catch2 expression-capture machinery.
    const std::string expected_name_field = R"("logger":"weird\"name\\with\ttab")";
    REQUIRE(oss.str().find(expected_name_field) != std::string::npos);
}

TEST_CASE("json_logger: logger name with percent signs stays valid JSON", "[json_logger]")
{
    // `%` is the spdlog pattern-flag prefix. Without an extra layer of escaping,
    // a name like "50%off" would cause spdlog to interpret `%o` as a flag (or,
    // worse, names containing `%v`/`%n` would splice the message/name right
    // into the "logger" field and break the JSON structure).
    SECTION("plain percent")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_logger logger("50%off", std::move(sink));
        logger.info("hi");
        REQUIRE(oss.str().find(R"("logger":"50%off")") != std::string::npos);
        REQUIRE(oss.str().find(R"("message":"hi")") != std::string::npos);
    }

    SECTION("percent v does not splice the message")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_logger logger("svc-%v-edge", std::move(sink));
        logger.info("payload");
        REQUIRE(oss.str().find(R"("logger":"svc-%v-edge")") != std::string::npos);
        // Exactly one occurrence of the message text -- not duplicated into
        // the "logger" field by a spurious %v expansion.
        const std::string out = oss.str();
        const auto first = out.find("payload");
        REQUIRE(first != std::string::npos);
        REQUIRE(out.find("payload", first + 1) == std::string::npos);
    }

    SECTION("percent n does not splice the logger name")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_logger logger("a%nb", std::move(sink));
        logger.info("ok");
        REQUIRE(oss.str().find(R"("logger":"a%nb")") != std::string::npos);
    }
}

TEST_CASE("json_logger: pattern is re-applied even when adopting an existing spdlog logger", "[json_logger]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto spdlog_logger = std::make_shared<spdlog::logger>("Adopted", sink);

    // Set a hostile pattern that would otherwise break JSON output.
    spdlog_logger->set_pattern("non-json: %v");
    spdlog_logger->set_level(spdlog::level::trace);

    auto logger = jspdlog::json_logger::adopt(std::move(spdlog_logger));
    logger.info("after adopt");

    REQUIRE(matches(oss.str(), expected_line("Adopted", "info", R"(,"message":"after adopt")")));
}

TEST_CASE("json_logger: set_pattern_time keeps the JSON pattern intact when switching to UTC", "[json_logger]")
{
    // spdlog's set_pattern() implicitly resets the time mode to local. After
    // adopt() (which calls set_pattern internally) the adopted logger has
    // therefore lost any UTC configuration it used to have. set_pattern_time
    // exists to restore that mode without dropping the pinned JSON pattern,
    // so the timestamp offset should now be `+00:00` or `+0000` (and we must
    // not accidentally clobber the JSON structure while doing it). We can't
    // reuse expected_line() here because that helper allows any offset; we
    // specifically want to assert the offset is +00:00 / +0000.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto spdlog_logger = std::make_shared<spdlog::logger>("Utc", sink);
    spdlog_logger->set_level(spdlog::level::trace);

    auto logger = jspdlog::json_logger::adopt(std::move(spdlog_logger));
    logger.set_pattern_time(spdlog::pattern_time_type::utc);
    logger.info("zulu");

    const std::string out = oss.str();
    REQUIRE(matches(
        out,
        add_endline(
            R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\+00:?00","logger":"Utc","level":"info",)"
            R"("process":[0-9]+,"thread":[0-9]+,"message":"zulu"\})"
        )
    ));
}

TEST_CASE(
    "json_logger: set_error_handler invokes the user callback with the spdlog message", "[json_logger][error_handler]"
)
{
    auto sink = std::make_shared<throwing_sink>("disk full");
    jspdlog::json_logger logger("ErrHandler", std::move(sink));
    logger.set_level(spdlog::level::trace);

    std::string captured;
    int call_count = 0;
    logger.set_error_handler([&](std::string_view msg) {
        captured.assign(msg.data(), msg.size());
        ++call_count;
    });

    // spdlog catches the throw inside the sink, formats a brief error and
    // hands it to the error handler. Throttling means we expect exactly one
    // call for a single log attempt.
    logger.info("trigger");
    REQUIRE(call_count == 1);
    REQUIRE(captured.find("disk full") != std::string::npos);

    // Passing an empty std::function clears the handler entirely; subsequent
    // errors are dropped silently. We verify that by checking that another
    // log call (which still triggers the throwing sink) no longer reaches
    // our (now-disconnected) lambda.
    logger.set_error_handler({});
    captured.clear();
    logger.info("trigger again");
    REQUIRE(captured.empty());
}

TEST_CASE(
    "json_logger: forward_errors_to surfaces sink errors as JSON warn lines on the destination",
    "[json_logger][error_handler]"
)
{
    // Source's sink throws on every write; destination is a normal stream sink
    // we can inspect.
    auto throwing = std::make_shared<throwing_sink>("connection reset");
    jspdlog::json_logger source("ProducerLogger", std::move(throwing));
    source.set_level(spdlog::level::trace);

    std::ostringstream dest_oss;
    auto dest = make_stream_logger("FailureLogger", dest_oss);

    jspdlog::forward_errors_to(source, dest);

    source.info("original message");

    const std::string out = dest_oss.str();
    REQUIRE(matches(
        out,
        expected_line(
            "FailureLogger", "warning", R"(,"source":"ProducerLogger","message":"[^"]*connection reset[^"]*")"
        )
    ));
}

TEST_CASE("json_logger: forward_errors_to snapshots the destination at call time", "[json_logger][error_handler]")
{
    auto throwing = std::make_shared<throwing_sink>("oops");
    jspdlog::json_logger source("Producer", std::move(throwing));

    std::ostringstream oss;
    auto dest = make_stream_logger("Sink", oss);

    jspdlog::forward_errors_to(source, dest);

    // Mutating the original `dest` after wiring up forwarding must not affect
    // what the forwarder writes (the lambda holds its own copy).
    auto enriched = dest.with_properties({"environment", "test"});
    (void)enriched;

    source.info("trigger");

    const std::string out = oss.str();
    REQUIRE(out.find(R"("source":"Producer")") != std::string::npos);
    REQUIRE(out.find(R"("environment")") == std::string::npos);
}

TEST_CASE(
    "json_logger: forward_errors_to does not infinite-loop when source and destination share a sink",
    "[json_logger][error_handler]"
)
{
    // Reproduces the original footgun: if the destination uses the same
    // always-throwing sink as the source, writing the forwarded warn line
    // would trigger another sink exception, spdlog would catch it and
    // re-enter the same error handler, and so on without bound. The
    // re-entrancy guard in forward_errors_to bails out on the second entry,
    // so this call must simply return.
    auto throwing = std::make_shared<throwing_sink>("recursive boom");
    jspdlog::json_logger source("Self", throwing);
    jspdlog::json_logger destination("Self", throwing);

    jspdlog::forward_errors_to(source, destination);

    // The bug we're guarding against is infinite recursion; reaching the
    // line after source.info() (under a default test timeout) is the
    // assertion.
    REQUIRE_NOTHROW(source.info("trigger"));
}
