// jspdlog itself has zero JSON-library dependencies. This optional test
// proves that any JSON library (here: nlohmann/json) plugs in cleanly via
// the raw_json wrapper.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/ostream_sink.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <limits>
#include <sstream>
#include <string>

TEST_CASE("raw_json: nlohmann arrays and objects round-trip into the log line", "[interop]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("interop", std::move(sink));
    logger.set_level(spdlog::level::trace);

    const nlohmann::json items = {1, true, "a"};
    const nlohmann::json features = {{"streaming", true}, {"limit", 10}};

    logger.info(
        jspdlog::json_properties{
            "items", jspdlog::raw_json{items.dump()}, "features", jspdlog::raw_json{features.dump()}
        },
        "ok"
    );

    const std::string out = oss.str();
    REQUIRE(out.find(R"("items":[1,true,"a"])") != std::string::npos);
    REQUIRE(out.find(R"("features":{"limit":10,"streaming":true})") != std::string::npos);
    REQUIRE(out.find(R"("message":"ok")") != std::string::npos);

    // The full line must round-trip through nlohmann::json::parse, proving it
    // is valid JSON.
    auto eol_pos = out.find_first_of("\r\n");
    const std::string line = out.substr(0, eol_pos);
    auto parsed = nlohmann::json::parse(line);
    REQUIRE(parsed["logger"] == "interop");
    REQUIRE(parsed["level"] == "info");
    REQUIRE(parsed["items"] == items);
    REQUIRE(parsed["features"] == features);
    REQUIRE(parsed["message"] == "ok");
}

TEST_CASE("raw_json: pathological inputs still produce parseable JSON", "[interop]")
{
    // The "structurally impossible to emit invalid JSON" guarantee should
    // survive every input we know to be tricky for the various escaping
    // layers: a logger name containing characters that need JSON escaping
    // *and* a literal `%v` that spdlog would otherwise expand, a message
    // containing a control byte that has to go through `\u00XX`, an empty
    // raw_json that has to fall back to `null`, and a NaN double that has
    // to fall back to `null`. We round-trip the resulting line through
    // nlohmann::json::parse and inspect the parsed fields. If any of the
    // escaping paths broke, parse() would throw and this test would fail
    // loudly rather than silently producing a malformed log line.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("weird\"name\\with\ttab-%v", std::move(sink));
    logger.set_level(spdlog::level::trace);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    logger.info(
        jspdlog::json_properties{
            "empty_payload",
            jspdlog::raw_json{""},
            "nan_value",
            nan,
        },
        "with control \x01 byte"
    );

    const std::string out = oss.str();
    const auto eol_pos = out.find_first_of("\r\n");
    const std::string line = out.substr(0, eol_pos);

    // Both the parse() call and every subsequent field access must succeed.
    const auto parsed = nlohmann::json::parse(line);
    REQUIRE(parsed["logger"] == "weird\"name\\with\ttab-%v");
    REQUIRE(parsed["level"] == "info");
    REQUIRE(parsed["empty_payload"].is_null());
    REQUIRE(parsed["nan_value"].is_null());
    REQUIRE(parsed["message"] == std::string("with control \x01 byte"));
}
