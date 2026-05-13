// Ported from the original HID LoggingTest.cpp, adapted to:
//   * the new misuse-proof jspdlog::json_logger constructor
//     (name + spdlog sink instead of LoggingFactory::CreateStringStreamLogger)
//   * with_properties() instead of LoggingFactory::CopyLogger
//   * snake_case API throughout
//   * Catch2 v3 instead of GoogleTest

#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/ostream_sink.h>

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

// Sink that always throws on sink_it_, used to drive spdlog's error handler.
// spdlog catches the exception, calls the error handler, and resumes.
class throwing_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit throwing_sink(std::string what) : what_(std::move(what))
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg & /*msg*/) override
    {
        throw std::runtime_error(what_);
    }
    void flush_() override
    {
    }

private:
    std::string what_;
};

} // namespace

TEST_CASE("json_logger: basic print methods", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("BasicPrintMethods", oss);

    logger.info("Test 1");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"BasicPrintMethods","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"Test 1"\})")));
    oss.str({});

    logger.debug(1.23);
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"BasicPrintMethods","level":"debug",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"1.23"\})")));
    oss.str({});

    logger.warn("{} {}", "Test", 3);
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"BasicPrintMethods","level":"warning",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"Test 3"\})")));
    oss.str({});

    logger.info("Test {}", "Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"BasicPrintMethods","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"Test Test"\})")));
    oss.str({});

    logger.trace(jspdlog::json_properties{"property", 1.23});
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"BasicPrintMethods","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":1.23\})")));
    oss.str({});

    logger.error(jspdlog::json_properties{"property1", "abcd", "property2", true}, "{} 4", "Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"BasicPrintMethods","level":"error",)"
                                R"("process":[0-9]+,"thread":[0-9]+,)"
                                R"("property1":"abcd","property2":true,"message":"Test 4"\})")));
}

TEST_CASE("json_logger: with_properties adds bound properties without affecting parent", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("AddingProperties", oss);
    auto logger1 = root.with_properties({"property1", 321});

    logger1.trace("Test 1");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"AddingProperties","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property1":321,"message":"Test 1"\})")));
    oss.str({});

    {
        auto logger2 = logger1.with_properties({"property2", false});

        logger1.info("Test 2");
        REQUIRE(matches(oss.str(),
                        add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"AddingProperties","level":"info",)"
                                    R"("process":[0-9]+,"thread":[0-9]+,"property1":321,"message":"Test 2"\})")));
        oss.str({});

        logger2.info("Test 3");
        REQUIRE(matches(oss.str(),
                        add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"AddingProperties","level":"info",)"
                                    R"("process":[0-9]+,"thread":[0-9]+,)"
                                    R"("property1":321,"property2":false,"message":"Test 3"\})")));
        oss.str({});
    }

    logger1.info("Test 4");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"AddingProperties","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property1":321,"message":"Test 4"\})")));
}

TEST_CASE("json_logger: with_properties can override an existing key", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("ReplaceProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    auto logger2 = logger1.with_properties({"property", false});

    logger1.trace("Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"ReplaceProperties","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":321,"message":"Test"\})")));
    oss.str({});

    logger2.trace("Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"ReplaceProperties","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":false,"message":"Test"\})")));
}

TEST_CASE("json_logger: escapes special characters in messages", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("EscapingString", oss);

    logger.info("First line\nsecond line\n\ttabbed line\n\"quoted line\"");
    // Hoisted into a local because MSVC's macro preprocessor mishandles
    // raw strings containing both backslash-quote sequences and our long
    // timestamp regex when stringified by Catch2's REQUIRE.
    const std::string expected = add_endline(
        R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"EscapingString","level":"info",)"
        R"("process":[0-9]+,"thread":[0-9]+,)"
        R"("message":"First line\\nsecond line\\n\\ttabbed line\\n\\\"quoted line\\\""\})");
    REQUIRE(matches(oss.str(), expected));
}

TEST_CASE("json_logger: a per-call property can override a bound property", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("DuplicateProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    logger1.set_level(spdlog::level::critical);

    logger1.critical("Test 1");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"DuplicateProperties","level":"critical",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":321,"message":"Test 1"\})")));
    oss.str({});

    auto logger2 = logger1.with_properties({"property", false});
    logger2.critical("Test 2");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"DuplicateProperties","level":"critical",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":false,"message":"Test 2"\})")));
    oss.str({});

    logger2.critical({"property", nullptr}, "Test 3");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"DuplicateProperties","level":"critical",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":null,"message":"Test 3"\})")));
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

    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"Adopted","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"after adopt"\})")));
}

TEST_CASE("json_logger: set_error_handler invokes the user callback with the spdlog message",
          "[json_logger][error_handler]")
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

    // Passing an empty std::function restores the default handler. We can't
    // observe the default easily, but we can verify that subsequent log calls
    // no longer reach our (now-disconnected) lambda.
    logger.set_error_handler({});
    captured.clear();
    logger.info("trigger again");
    REQUIRE(captured.empty());
}

TEST_CASE("json_logger: forward_errors_to surfaces sink errors as JSON warn lines on the destination",
          "[json_logger][error_handler]")
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
    REQUIRE(matches(out,
                    add_endline(R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2}","logger":"FailureLogger","level":"warning",)"
                                R"("process":[0-9]+,"thread":[0-9]+,)"
                                R"("source":"ProducerLogger","message":"[^"]*connection reset[^"]*"\})")));
}

TEST_CASE("json_logger: forward_errors_to snapshots the destination at call time",
          "[json_logger][error_handler]")
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
