// Ported from the original HID LoggingTest.cpp, adapted to:
//   * the new misuse-proof jspdlog::json_logger constructor
//     (name + spdlog sink instead of LoggingFactory::CreateStringStreamLogger)
//   * with_properties() instead of LoggingFactory::CopyLogger
//   * snake_case API throughout
//   * Catch2 v3 instead of GoogleTest

#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>
#include <spdlog/details/os.h>
#include <spdlog/sinks/ostream_sink.h>

#include <regex>
#include <sstream>
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

} // namespace

TEST_CASE("json_logger: basic print methods", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("BasicPrintMethods", oss);

    logger.info("Test 1");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"BasicPrintMethods","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"Test 1"\})")));
    oss.str({});

    logger.debug(1.23);
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"BasicPrintMethods","level":"debug",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"1.23"\})")));
    oss.str({});

    logger.warn("{} {}", "Test", 3);
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"BasicPrintMethods","level":"warning",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"Test 3"\})")));
    oss.str({});

    logger.info("Test {}", "Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"BasicPrintMethods","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"Test Test"\})")));
    oss.str({});

    logger.trace(jspdlog::json_properties{"property", 1.23});
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"BasicPrintMethods","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":1.23\})")));
    oss.str({});

    logger.error(jspdlog::json_properties{"property1", "abcd", "property2", true}, "{} 4", "Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"BasicPrintMethods","level":"error",)"
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
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"AddingProperties","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property1":321,"message":"Test 1"\})")));
    oss.str({});

    {
        auto logger2 = logger1.with_properties({"property2", false});

        logger1.info("Test 2");
        REQUIRE(matches(oss.str(),
                        add_endline(R"(\{"timestamp":"[^"]+","logger":"AddingProperties","level":"info",)"
                                    R"("process":[0-9]+,"thread":[0-9]+,"property1":321,"message":"Test 2"\})")));
        oss.str({});

        logger2.info("Test 3");
        REQUIRE(matches(oss.str(),
                        add_endline(R"(\{"timestamp":"[^"]+","logger":"AddingProperties","level":"info",)"
                                    R"("process":[0-9]+,"thread":[0-9]+,)"
                                    R"("property1":321,"property2":false,"message":"Test 3"\})")));
        oss.str({});
    }

    logger1.info("Test 4");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"AddingProperties","level":"info",)"
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
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"ReplaceProperties","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":321,"message":"Test"\})")));
    oss.str({});

    logger2.trace("Test");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"ReplaceProperties","level":"trace",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":false,"message":"Test"\})")));
}

TEST_CASE("json_logger: escapes special characters in messages", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("EscapingString", oss);

    logger.info("First line\nsecond line\n\ttabbed line\n\"quoted line\"");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"EscapingString","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,)"
                                R"("message":"First line\\nsecond line\\n\\ttabbed line\\n\\\"quoted line\\\""\})")));
}

TEST_CASE("json_logger: a per-call property can override a bound property", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("DuplicateProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    logger1.set_level(spdlog::level::critical);

    logger1.critical("Test 1");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"DuplicateProperties","level":"critical",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":321,"message":"Test 1"\})")));
    oss.str({});

    auto logger2 = logger1.with_properties({"property", false});
    logger2.critical("Test 2");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"DuplicateProperties","level":"critical",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"property":false,"message":"Test 2"\})")));
    oss.str({});

    logger2.critical({"property", nullptr}, "Test 3");
    REQUIRE(matches(oss.str(),
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"DuplicateProperties","level":"critical",)"
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
                    add_endline(R"(\{"timestamp":"[^"]+","logger":"Adopted","level":"info",)"
                                R"("process":[0-9]+,"thread":[0-9]+,"message":"after adopt"\})")));
}
