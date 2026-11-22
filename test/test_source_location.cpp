#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using test_support::fixture;

namespace
{

jspdlog::json_options source_options()
{
    auto opts = test_support::deterministic_options();
    opts.source_key = "src";
    return opts;
}

} // namespace

TEST_CASE("source location: every call form records its own line", "[source]")
{
    fixture f("t", source_options());
    const std::string text = "runtime";

    int line = __LINE__ + 1;
    f.logger.info("plain");
    CHECK(
        f.last().find(R"("file":"test_source_location.cpp","line":)" + std::to_string(line) + ",") != std::string::npos
    );

    line = __LINE__ + 1;
    f.logger.warn("{} formatted", 1);
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.error(text);
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.debug({"k", 1});
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.critical({"k", 1}, "with {}", "fields");
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);

    line = __LINE__ + 1;
    f.logger.trace({"k", 1}, text);
    CHECK(f.last().find(R"("line":)" + std::to_string(line) + ",") != std::string::npos);
}

TEST_CASE("source location: log(level, ...) and the level functions record their own lines", "[source]")
{
    fixture f("t", source_options());

    const int log_line = __LINE__ + 1;
    f.logger.log(jspdlog::level::info, "x");
    const std::string first = f.last();
    const int info_line = __LINE__ + 1;
    f.logger.info("x");
    const std::string second = f.last();

    CHECK(first.find(R"("line":)" + std::to_string(log_line) + ",") != std::string::npos);
    CHECK(second.find(R"("line":)" + std::to_string(info_line) + ",") != std::string::npos);
    CHECK(first.find("test_source_location.cpp") != std::string::npos);
}

TEST_CASE("source location: written between the header and the fields, and only when asked for", "[source]")
{
    fixture f("t", source_options());
    f.logger.with({"a", 1}).info("m");
    const std::string line = f.last();
    CHECK(line.find(R"({"logger":"t","level":"info","src":{)") == 0);
    CHECK(line.find(R"(},"a":1,"message":"m"})") != std::string::npos);

    fixture off;
    off.logger.info("m");
    CHECK(off.last() == R"({"logger":"t","level":"info","message":"m"})");
}
