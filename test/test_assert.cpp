// Built into its own executable: it defines JSPDLOG_ASSERT before the header, which changes the inline checks.

#include <string>
#include <vector>

namespace
{
std::vector<std::string> &failures()
{
    static std::vector<std::string> list;
    return list;
}
} // namespace

#define JSPDLOG_ASSERT(cond, msg)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            failures().emplace_back(msg);                                                                              \
        }                                                                                                              \
    } while (false)

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

using test_support::fixture;

TEST_CASE("assert: field keys that clash with header keys are reported", "[assert]")
{
    failures().clear();
    auto opts = test_support::deterministic_options();
    opts.source_key = "src";
    fixture f("t", opts);

    SECTION("clean keys")
    {
        f.logger.with({"a", 1}).info({"b", 2}, "x");
        f.logger.info({"c", 3});
        CHECK(failures().empty());
    }
    SECTION("with")
    {
        (void)f.logger.with({"level", "x"});
        CHECK(failures().size() == 1);
    }
    SECTION("per call, for each reserved key")
    {
        f.logger.info({"logger", 1});
        f.logger.info({"message", 1}, "x");
        f.logger.info({"src", 1});
        CHECK(failures().size() == 3);
    }
    SECTION("keys that are switched off are free to use")
    {
        f.logger.info({"timestamp", 1, "thread", 2});
        CHECK(failures().empty());
    }
    SECTION("a call without fields is not checked")
    {
        f.logger.info("x");
        CHECK(failures().empty());
    }
}
