// jspdlog itself has zero JSON-library dependencies. This optional test
// proves that any JSON library (here: nlohmann/json) plugs in cleanly via
// the raw_json wrapper, and covers jspdlog/nlohmann.h, which makes the values
// of nlohmann/json field values.

#include <jspdlog/nlohmann.h>

#include "test_support.h"

#include <spdlog/sinks/ostream_sink.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE("raw_json: nlohmann arrays and objects round-trip into the log line", "[interop]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("interop", std::move(sink));
    logger.set_level(spdlog::level::trace);

    const nlohmann::json items = {1, true, "a"};
    const nlohmann::json features = {{"streaming", true}, {"limit", 10}};

    logger.info(
        jspdlog::fields{"items", jspdlog::raw_json{items.dump()}, "features", jspdlog::raw_json{features.dump()}}, "ok"
    );

    const std::string out = oss.str();
    REQUIRE(out.find(R"("items":[1,true,"a"])") != std::string::npos);
    REQUIRE(out.find(R"("features":{"limit":10,"streaming":true})") != std::string::npos);
    REQUIRE(out.find(R"("message":"ok")") != std::string::npos);

    // The full line must round-trip through nlohmann::json::parse, proving it
    // is valid JSON.
    const auto eol_pos = out.find_first_of("\r\n");
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
        jspdlog::fields{
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

TEST_CASE("serializer: nlohmann objects, arrays and scalars are written as JSON", "[interop]")
{
    const test_support::fixture f;
    const nlohmann::json object = {{"streaming", true}, {"limit", 10}, {"note", "a \"quote\"\n"}};
    const nlohmann::json array = {1, 2.5, "a", nullptr, nlohmann::json::array()};
    const nlohmann::json text = "text";
    const nlohmann::json number = 7;
    const nlohmann::json null;

    f.logger.info({"object", object, "array", array, "text", text, "number", number, "null", null}, "ok");
    // A local, because MSVC mis-stringifies raw literals with backslashes inside assertion macros.
    const std::string expected =
        R"({"logger":"t","level":"info","object":{"limit":10,"note":"a \"quote\"\n","streaming":true},)"
        R"("array":[1,2.5,"a",null,[]],"text":"text","number":7,"null":null,"message":"ok"})";
    CHECK(f.last() == expected);
}

TEST_CASE("serializer: nlohmann values work in fields, bound context and containers", "[interop]")
{
    const test_support::fixture f;
    const nlohmann::json one = {{"id", 1}};
    const std::vector<nlohmann::json> list = {one, nlohmann::json{{"id", 2}}};
    const std::map<std::string, nlohmann::json> map = {{"first", one}};

    jspdlog::fields stored{"stored", one};
    stored.insert("list", list);
    const auto child = f.logger.with(stored);
    child.info({"map", map, "lazy", [] { return nlohmann::json{{"id", 1}}; }});
    CHECK(
        f.last() == R"({"logger":"t","level":"info","stored":{"id":1},"list":[{"id":1},{"id":2}],)"
                    R"("map":{"first":{"id":1}},"lazy":{"id":1}})"
    );
}

TEST_CASE("serializer: nlohmann::ordered_json keeps its order", "[interop]")
{
    nlohmann::ordered_json ordered;
    ordered["zebra"] = 1;
    ordered["apple"] = nlohmann::ordered_json{{"y", 2}, {"b", 3}};
    const jspdlog::fields f{"ordered", ordered};
    CHECK(f.to_json() == R"({"ordered":{"zebra":1,"apple":{"y":2,"b":3}}})");
}

TEST_CASE("serializer: invalid UTF-8 in a nlohmann string is replaced", "[interop]")
{
    // A lone byte, a truncated sequence inside and at the end, and a surrogate: one U+FFFD per maximal invalid
    // subsequence, which is also what jspdlog writes for its own strings by default.
    const std::string bad = "a\xFF"
                            "b\xE2\x82"
                            "c\xED\xA0\x80"
                            "d\xE2\x82";
    const std::string u = "\xEF\xBF\xBD";
    const std::string replaced = "a" + u + "b" + u + "c" + u + u + u + "d" + u;

    nlohmann::json j;
    j[bad] = bad;
    const jspdlog::fields f{"j", j};
    CHECK(f.to_json() == "{\"j\":{\"" + replaced + "\":\"" + replaced + "\"}}");
}

TEST_CASE("serializer: a discarded nlohmann value is null", "[interop]")
{
    const nlohmann::json discarded = nlohmann::json::parse("{not json", nullptr, false);
    REQUIRE(discarded.is_discarded());
    const jspdlog::fields f{"j", discarded};
    CHECK(f.to_json() == R"({"j":null})");
}
