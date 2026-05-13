// jspdlog itself has zero JSON-library dependencies. This optional test
// proves that any JSON library (here: nlohmann/json) plugs in cleanly via
// the raw_json wrapper.

#include <jspdlog/jspdlog.h>
#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include <sstream>

TEST_CASE("raw_json: nlohmann arrays and objects round-trip into the log line", "[interop]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("interop", std::move(sink));
    logger.set_level(spdlog::level::trace);

    const nlohmann::json items = {1, true, "a"};
    const nlohmann::json features = {{"streaming", true}, {"limit", 10}};

    logger.info(jspdlog::json_properties{"items", jspdlog::raw_json{items.dump()}, "features",
                                          jspdlog::raw_json{features.dump()}},
                "ok");

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
