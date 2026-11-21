#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

using jspdlog::json_properties;
using jspdlog::raw_json;

TEST_CASE("json_properties: scalar types", "[json_properties]")
{
    CHECK(json_properties{"v", nullptr}.to_json() == R"({"v":null})");
    CHECK(json_properties{"v", "text"}.to_json() == R"({"v":"text"})");
    CHECK(json_properties{"v", std::string{"text"}}.to_json() == R"({"v":"text"})");
    CHECK(json_properties{"v", std::string_view{"text"}}.to_json() == R"({"v":"text"})");
    CHECK(json_properties{"v", static_cast<const char *>(nullptr)}.to_json() == R"({"v":null})");
    CHECK(json_properties{"v", true}.to_json() == R"({"v":true})");
    CHECK(json_properties{"v", false}.to_json() == R"({"v":false})");
    CHECK(json_properties{"v", 'a'}.to_json() == R"({"v":"a"})");
    CHECK(json_properties{"v", static_cast<signed char>(-5)}.to_json() == R"({"v":-5})");
    CHECK(json_properties{"v", static_cast<std::uint8_t>(200)}.to_json() == R"({"v":200})");
    CHECK(json_properties{"v", short{-3}}.to_json() == R"({"v":-3})");
    CHECK(json_properties{"v", std::numeric_limits<std::int64_t>::min()}.to_json() == R"({"v":-9223372036854775808})");
    CHECK(json_properties{"v", std::numeric_limits<std::uint64_t>::max()}.to_json() == R"({"v":18446744073709551615})");
}

TEST_CASE("json_properties: floating point", "[json_properties]")
{
    CHECK(json_properties{"v", 1.5}.to_json() == R"({"v":1.5})");
    CHECK(json_properties{"v", 2.0}.to_json() == R"({"v":2.0})");
    CHECK(json_properties{"v", 1.5f}.to_json() == R"({"v":1.5})");
    CHECK(json_properties{"v", 3.0L}.to_json() == R"({"v":3.0})");
    CHECK(json_properties{"v", -0.0}.to_json() == R"({"v":-0.0})");
    CHECK(json_properties{"v", 1e300}.to_json() == R"({"v":1e+300})");
    CHECK(json_properties{"v", std::numeric_limits<double>::quiet_NaN()}.to_json() == R"({"v":null})");
    CHECK(json_properties{"v", std::numeric_limits<double>::infinity()}.to_json() == R"({"v":null})");
}

TEST_CASE("json_properties: raw_json and nesting", "[json_properties]")
{
    CHECK(json_properties{"v", raw_json{"[1,2]"}}.to_json() == R"({"v":[1,2]})");
    CHECK(json_properties{"v", raw_json{""}}.to_json() == R"({"v":null})");
    CHECK(json_properties{"v", json_properties{"a", 1, "b", "x"}}.to_json() == R"({"v":{"a":1,"b":"x"}})");
    CHECK(json_properties{"v", json_properties{}}.to_json() == R"({"v":{}})");
    CHECK(json_properties{"v", json_properties{"w", json_properties{"x", 1}}}.to_json() == R"({"v":{"w":{"x":1}}})");
}

TEST_CASE("json_properties: keys and strings are escaped", "[json_properties]")
{
    const std::string expected = R"({"k\"ey":"va\\l\n"})"; // outside CHECK(): MSVC raw-literal quirk
    CHECK(json_properties{"k\"ey", "va\\l\n"}.to_json() == expected);
    CHECK(json_properties{std::string_view{"sv"}, 1}.to_json() == R"({"sv":1})");
}

TEST_CASE("json_properties: ordering, replacement and merge", "[json_properties]")
{
    SECTION("keys are sorted and later inserts replace earlier ones")
    {
        json_properties p{"b", 1, "a", 2, "b", 3};
        CHECK(p.size() == 2);
        CHECK(p.to_json() == R"({"a":2,"b":3})");
    }
    SECTION("merge and operator+ let the right-hand side win")
    {
        const json_properties lhs{"a", 1, "b", 1};
        const json_properties rhs{"b", 2, "c", 2};
        CHECK((lhs + rhs).to_json() == R"({"a":1,"b":2,"c":2})");

        json_properties merged = lhs;
        merged.merge(rhs);
        CHECK(merged.to_json() == R"({"a":1,"b":2,"c":2})");
    }
    SECTION("self-merge is harmless")
    {
        json_properties p{"a", 1};
        p.merge(p);
        CHECK(p.to_json() == R"({"a":1})");
    }
    SECTION("empty / clear")
    {
        json_properties p;
        CHECK(p.empty());
        CHECK(p.to_json() == "{}");
        p.insert("a", 1);
        CHECK_FALSE(p.empty());
        p.clear();
        CHECK(p.empty());
    }
}
