#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
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

TEST_CASE("fields: string-like user types are strings", "[fields]")
{
    struct view_like
    {
        // Implicit on purpose: the conversion to std::string_view is what makes it a string.
        // NOLINTNEXTLINE(*-explicit-constructor,hicpp-explicit-conversions)
        operator std::string_view() const { return "like"; }
    };
    CHECK(fields{"v", view_like{}}.to_json() == R"({"v":"like"})");
    fields f;
    f.insert("k", view_like{});
    CHECK(f.to_json() == R"({"k":"like"})");
}

// Types without a JSON form (wide characters, pointers, unknown classes) are rejected at compile time: see
// test/compile_fail/value_*.cpp.

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
    const std::string backslash = R"({"a\\b":1})";
    CHECK(fields{"a\\b", 1}.to_json() == backslash);
    CHECK(fields{std::string_view{"sv"}, 1}.to_json() == R"({"sv":1})");
}

TEST_CASE("fields: text beyond the 4 GiB of the payload protocol is rejected", "[fields]")
{
    // Checked on the size alone: building fields that large would need more memory than a test should use.
    CHECK_NOTHROW(jspdlog::detail::check_fields_size(jspdlog::detail::MAX_FIELDS_SIZE));
#if SIZE_MAX > 0xffffffff
    CHECK_THROWS_AS(jspdlog::detail::check_fields_size(jspdlog::detail::MAX_FIELDS_SIZE + 1), std::length_error);
#endif
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

TEST_CASE("fields: replacing a value splices the text", "[fields]")
{
    SECTION("first, middle and last, with a longer and a shorter value")
    {
        fields p{"a", 1, "b", 22, "c", 333};
        p.insert("a", "much longer");
        CHECK(p.to_json() == R"({"a":"much longer","b":22,"c":333})");
        p.insert("b", 7);
        CHECK(p.to_json() == R"({"a":"much longer","b":7,"c":333})");
        p.insert("c", fields{"x", 1, "y", 2});
        CHECK(p.to_json() == R"({"a":"much longer","b":7,"c":{"x":1,"y":2}})");
        p.insert("a", 0);
        CHECK(p.to_json() == R"({"a":0,"b":7,"c":{"x":1,"y":2}})");
        CHECK(p.size() == 3);
    }
    SECTION("lookups still find every key after the text moved")
    {
        fields p{"a", 1, "b", 2, "c", 3};
        p.insert("a", std::string(100, 'x'));
        CHECK(p.contains("a"));
        CHECK(p.contains("b"));
        CHECK(p.contains("c"));
        p.insert("c", 4);
        p.insert("b", std::string(50, 'y'));
        CHECK(p.size() == 3);
        CHECK(p.to_json() == R"({"a":")" + std::string(100, 'x') + R"(","b":")" + std::string(50, 'y') + R"(","c":4})");
    }
}

TEST_CASE("fields: keys that need escaping next to plain keys", "[fields]")
{
    const std::string quoted = R"({"a":1,"k\"ey":2,"b":3,"new\nline":4,"c":5})"; // outside CHECK(): MSVC quirk
    fields p;
    p.insert("a", 1);
    p.insert("k\"ey", 2);
    p.insert("b", 3);
    p.insert("new\nline", 4);
    p.insert("c", 5);
    CHECK(p.to_json() == quoted);
    CHECK(p.contains("k\"ey"));
    CHECK(p.contains("new\nline"));
    CHECK_FALSE(p.contains("key"));

    SECTION("replacing keeps the raw keys findable")
    {
        p.insert("a", std::string(40, 'z'));
        p.insert("k\"ey", "two");
        p.insert("b", 30);
        CHECK(p.size() == 5);
        CHECK(p.contains("k\"ey"));
        CHECK(p.contains("b"));
        p.insert("new\nline", 40);
        const std::string expected =
            R"({"a":")" + std::string(40, 'z') + R"(","k\"ey":"two","b":30,"new\nline":40,"c":5})";
        CHECK(p.to_json() == expected);
    }
    SECTION("non-ASCII keys are stored raw and replaced in place")
    {
        p.insert("d\xC3\xA9j\xC3\xA0", 1);
        p.insert("d\xC3\xA9j\xC3\xA0", 2);
        CHECK(p.size() == 6);
        CHECK(p.to_json() == "{\"a\":1,\"k\\\"ey\":2,\"b\":3,\"new\\nline\":4,\"c\":5,\"d\xC3\xA9j\xC3\xA0\":2}");
    }
}

TEST_CASE("fields: merge with overlaps and escaped keys", "[fields]")
{
    fields lhs{"a", 1, "k\"1", "left", "b", 2};
    const fields rhs{"k\"1", "right-and-longer", "c", 3, "a", 10, "k\"2", 4};
    lhs.merge(rhs);
    const std::string expected = R"({"a":10,"k\"1":"right-and-longer","b":2,"c":3,"k\"2":4})";
    CHECK(lhs.to_json() == expected);
    CHECK(lhs.size() == 5);
    CHECK(lhs.contains("k\"2"));

    SECTION("the right-hand side is unchanged and a copy is independent")
    {
        const fields copy = lhs;
        lhs.insert("a", "changed");
        CHECK(copy.to_json() == expected);
        CHECK(rhs.size() == 4);
    }
    SECTION("an empty side")
    {
        fields empty;
        empty.merge(rhs);
        CHECK(empty.to_json() == rhs.to_json());
        fields again = rhs;
        again.merge(fields{});
        CHECK(again.to_json() == rhs.to_json());
    }
}

namespace
{
struct throws_halfway
{
};

// Writes part of an object, then fails.
void jspdlog_serialize(jspdlog::json_writer &w, const throws_halfway & /*unused*/)
{
    w.begin_object();
    w.member("x", 1);
    throw std::runtime_error("serializer failed");
}
} // namespace

TEST_CASE("fields: a serializer that throws leaves the set unchanged", "[fields]")
{
    fields p{"a", 1, "b", 2};
    CHECK_THROWS_AS(p.insert("c", throws_halfway{}), std::runtime_error);
    CHECK(p.to_json() == R"({"a":1,"b":2})");
    CHECK_FALSE(p.contains("c"));
    CHECK_THROWS_AS(p.insert("a", throws_halfway{}), std::runtime_error);
    CHECK(p.to_json() == R"({"a":1,"b":2})");
    p.insert("c", 3);
    CHECK(p.to_json() == R"({"a":1,"b":2,"c":3})");
}

TEST_CASE("fields: checked keys", "[fields]")
{
    using namespace jspdlog::literals;

    SECTION("insert, the constructor and a literal give the same text as a string key")
    {
        fields a;
        a.insert("user_id"_key, 42);
        a.insert(jspdlog::key{"ok"}, true);
        const fields b{"user_id"_key, 42, "name"_key, "alice"};
        CHECK(a.to_json() == R"({"user_id":42,"ok":true})");
        CHECK(b.to_json() == fields{"user_id", 42, "name", "alice"}.to_json());
        CHECK(a.contains("user_id"));
    }
    SECTION("a checked key replaces a string key of the same name, and the other way round")
    {
        fields p{"a", 1, "b", 2};
        p.insert("a"_key, "one");
        p.insert("b", "two");
        CHECK(p.to_json() == R"({"a":"one","b":"two"})");
    }
    SECTION("a key is usable in a constant expression")
    {
        constexpr auto KEY = "user_id"_key;
        static_assert(KEY.view() == "user_id");
        CHECK(std::string{std::string_view{KEY}} == "user_id");
    }
    SECTION("an empty key is plain")
    {
        CHECK(fields{""_key, 1}.to_json() == R"({"":1})");
    }
}

#ifndef __cpp_consteval
TEST_CASE("fields: a key that needs escaping throws before C++20", "[fields]")
{
    CHECK_THROWS_AS(jspdlog::key{"a\"b"}, std::invalid_argument);
    CHECK_THROWS_AS(jspdlog::key{"a\b"}, std::invalid_argument);
    CHECK_THROWS_AS(jspdlog::key{"a\nb"}, std::invalid_argument);
    CHECK_THROWS_AS(jspdlog::key{"d\xC3\xA9j\xC3\xA0"}, std::invalid_argument);
}
#endif
