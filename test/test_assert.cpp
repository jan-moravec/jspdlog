// Built into its own executable: it defines JSPDLOG_ASSERT before the header, which changes the inline checks.

#include <stdexcept>
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

namespace
{
jspdlog::fields fields_without_error_key()
{
    return jspdlog::fields{"a", 1};
}
} // namespace

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
    SECTION("per call, with a lazy value")
    {
        f.logger.info({"message", [] { return 1; }}, "x");
        f.logger.info({"ok", 1, "logger", [] { return 2; }});
        CHECK(failures().size() == 2);
    }
    SECTION("per call, with a fields object")
    {
        f.logger.info(jspdlog::fields{"level", 1});
        CHECK(failures().size() == 1);
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

namespace
{

struct leaves_object_open
{
};
struct writes_nothing
{
};
struct writes_two_values
{
};
struct key_without_value
{
};

void jspdlog_serialize(jspdlog::json_writer &w, const leaves_object_open & /*unused*/)
{
    w.begin_object();
}
void jspdlog_serialize(jspdlog::json_writer & /*unused*/, const writes_nothing & /*unused*/) {}
void jspdlog_serialize(jspdlog::json_writer &w, const writes_two_values & /*unused*/)
{
    w.value(1);
    w.value(2);
}
void jspdlog_serialize(jspdlog::json_writer &w, const key_without_value & /*unused*/)
{
    w.begin_object();
    w.key("k");
    w.end_object();
}

} // namespace

TEST_CASE("assert: a serializer must write exactly one complete value", "[assert]")
{
    failures().clear();
    SECTION("a well-formed serializer")
    {
        jspdlog::fields f;
        f.insert("a", std::vector<int>{1, 2});
        CHECK(failures().empty());
    }
    SECTION("an object left open")
    {
        jspdlog::fields f;
        f.insert("a", leaves_object_open{});
        CHECK(failures().size() == 1);
    }
    SECTION("no value")
    {
        jspdlog::fields f;
        f.insert("a", writes_nothing{});
        CHECK(failures().size() == 1);
    }
    SECTION("two values")
    {
        jspdlog::fields f;
        f.insert("a", writes_two_values{});
        CHECK_FALSE(failures().empty());
    }
    SECTION("a key without a value")
    {
        jspdlog::fields f;
        f.insert("a", key_without_value{});
        CHECK_FALSE(failures().empty());
    }
}

TEST_CASE("assert: a per-call field with the error key of an exception call is reported", "[assert]")
{
    failures().clear();
    const fixture f;
    const std::runtime_error ex("boom");

    f.logger.error(ex, {"ok", 1}, "x");
    f.logger.error(ex, fields_without_error_key());
    CHECK(failures().empty());

    f.logger.error(ex, {"error", 1});
    REQUIRE(failures().size() == 1);
    CHECK(failures().front() == "jspdlog: a field key equals the error key of an exception call");

    // Without an exception the key is an ordinary one.
    failures().clear();
    f.logger.error({"error", 1});
    CHECK(failures().empty());
}

TEST_CASE("assert: flat call site keys are reserved instead of the source key", "[assert]")
{
    failures().clear();
    auto opts = test_support::deterministic_options();
    opts.source_key = "src";
    opts.source_flat = true;
    const fixture f("t", opts);

    f.logger.info({"src", 1}); // the text of source_key is not used in flat mode
    CHECK(failures().empty());
    f.logger.info({"file", 1});
    f.logger.info({"line", 1});
    f.logger.info({"function", 1});
    CHECK(failures().size() == 3);
}
