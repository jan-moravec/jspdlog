#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using jspdlog::fields;
using jspdlog::raw_json;

TEST_CASE("fields: scalar types", "[fields]")
{
    CHECK(fields{"v", nullptr}.to_json() == R"({"v":null})");
    CHECK(fields{"v", "text"}.to_json() == R"({"v":"text"})");
    CHECK(fields{"v", std::string{"text"}}.to_json() == R"({"v":"text"})");
    CHECK(fields{"v", std::string_view{"text"}}.to_json() == R"({"v":"text"})");
    CHECK(fields{"v", static_cast<const char *>(nullptr)}.to_json() == R"({"v":null})");
    CHECK(fields{"v", true}.to_json() == R"({"v":true})");
    CHECK(fields{"v", false}.to_json() == R"({"v":false})");
    CHECK(fields{"v", 'a'}.to_json() == R"({"v":"a"})");
    CHECK(fields{"v", static_cast<signed char>(-5)}.to_json() == R"({"v":-5})");
    CHECK(fields{"v", static_cast<std::uint8_t>(200)}.to_json() == R"({"v":200})");
    CHECK(fields{"v", short{-3}}.to_json() == R"({"v":-3})");
    CHECK(fields{"v", std::numeric_limits<std::int64_t>::min()}.to_json() == R"({"v":-9223372036854775808})");
    CHECK(fields{"v", std::numeric_limits<std::uint64_t>::max()}.to_json() == R"({"v":18446744073709551615})");
}

namespace
{

template <typename T, typename = void>
struct can_insert : std::false_type
{
};

template <typename T>
struct can_insert<T, std::void_t<decltype(std::declval<fields &>().insert(std::string{}, std::declval<T>()))>>
    : std::true_type
{
};

} // namespace

TEST_CASE("fields: types without a JSON mapping are rejected", "[fields]")
{
    STATIC_CHECK(can_insert<int>::value);
    STATIC_CHECK(can_insert<const char *>::value);
    STATIC_CHECK_FALSE(can_insert<wchar_t>::value);
    STATIC_CHECK_FALSE(can_insert<char16_t>::value);
    STATIC_CHECK_FALSE(can_insert<char32_t>::value);
    STATIC_CHECK_FALSE(can_insert<const int *>::value);
    STATIC_CHECK_FALSE(can_insert<void *>::value);
    STATIC_CHECK_FALSE(can_insert<const wchar_t *>::value);
#ifdef __cpp_char8_t
    STATIC_CHECK_FALSE(can_insert<char8_t>::value);
#endif
}

TEST_CASE("fields: floating point", "[fields]")
{
    CHECK(fields{"v", 1.5}.to_json() == R"({"v":1.5})");
    CHECK(fields{"v", 2.0}.to_json() == R"({"v":2.0})");
    CHECK(fields{"v", 1.5f}.to_json() == R"({"v":1.5})");
    CHECK(fields{"v", 3.0L}.to_json() == R"({"v":3.0})");
    CHECK(fields{"v", -0.0}.to_json() == R"({"v":-0.0})");
    CHECK(fields{"v", 1e300}.to_json() == R"({"v":1e+300})");
    CHECK(fields{"v", std::numeric_limits<double>::quiet_NaN()}.to_json() == R"({"v":null})");
    CHECK(fields{"v", std::numeric_limits<double>::infinity()}.to_json() == R"({"v":null})");
}

TEST_CASE("fields: raw_json and nesting", "[fields]")
{
    CHECK(fields{"v", raw_json{"[1,2]"}}.to_json() == R"({"v":[1,2]})");
    CHECK(fields{"v", raw_json{""}}.to_json() == R"({"v":null})");
    CHECK(fields{"v", fields{"a", 1, "b", "x"}}.to_json() == R"({"v":{"a":1,"b":"x"}})");
    CHECK(fields{"v", fields{}}.to_json() == R"({"v":{}})");
    CHECK(fields{"v", fields{"w", fields{"x", 1}}}.to_json() == R"({"v":{"w":{"x":1}}})");
}

TEST_CASE("fields: keys and strings are escaped", "[fields]")
{
    const std::string expected = R"({"k\"ey":"va\\l\n"})"; // outside CHECK(): MSVC raw-literal quirk
    CHECK(fields{"k\"ey", "va\\l\n"}.to_json() == expected);
    CHECK(fields{std::string_view{"sv"}, 1}.to_json() == R"({"sv":1})");
}

TEST_CASE("fields: ordering, replacement and merge", "[fields]")
{
    SECTION("keys keep insertion order and later inserts replace earlier ones in place")
    {
        fields p{"b", 1, "a", 2, "b", 3};
        CHECK(p.size() == 2);
        CHECK(p.to_json() == R"({"b":3,"a":2})");
        CHECK(p.contains("a"));
        CHECK_FALSE(p.contains("c"));
    }
    SECTION("merge and operator+ let the right-hand side win")
    {
        const fields lhs{"a", 1, "b", 1};
        const fields rhs{"b", 2, "c", 2};
        CHECK((lhs + rhs).to_json() == R"({"a":1,"b":2,"c":2})");
        CHECK((rhs + lhs).to_json() == R"({"b":1,"c":2,"a":1})");

        fields merged = lhs;
        merged.merge(rhs);
        CHECK(merged.to_json() == R"({"a":1,"b":2,"c":2})");
    }
    SECTION("self-merge is harmless")
    {
        fields p{"a", 1};
        p.merge(p);
        CHECK(p.to_json() == R"({"a":1})");
    }
    SECTION("empty / clear")
    {
        fields p;
        CHECK(p.empty());
        CHECK(p.to_json() == "{}");
        p.insert("a", 1);
        CHECK_FALSE(p.empty());
        p.clear();
        CHECK(p.empty());
    }
}
