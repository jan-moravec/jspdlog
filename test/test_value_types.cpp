#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <ratio>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#if __has_include(<version>)
#include <version>
#endif
#ifdef __cpp_lib_span
#include <span>
#endif

using jspdlog::fields;

namespace
{

enum class color
{
    red,
    green,
};
// Unscoped on purpose: {fmt} 9 formats every unscoped enum as an int, which must still be written as a number.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum plain
{
    one = 1,
};
enum class named
{
    up,
};

#if FMT_VERSION >= 100000
enum class numbered
{
    first,
};
enum class labelled
{
    first,
};
int format_as(numbered e)
{
    return static_cast<int>(e) + 10;
}
std::string_view format_as(labelled /*unused*/)
{
    return "first-label";
}
#endif

namespace shop
{
struct money
{
    std::int64_t cents;
    std::string currency;
};

void jspdlog_serialize(jspdlog::json_writer &w, const money &m)
{
    w.begin_object();
    w.member("cents", m.cents);
    w.member("currency", m.currency);
    w.end_object();
}

struct order
{
    std::string id;
    std::vector<money> lines;
    std::optional<money> discount;
};

void jspdlog_serialize(jspdlog::json_writer &w, const order &o)
{
    w.begin_object();
    w.member("id", o.id);
    w.member("lines", o.lines);
    w.member("discount", o.discount);
    w.end_object();
}

// A range with its own form: the serializer wins over the range branch.
struct bag
{
    std::vector<int> items;
    [[nodiscard]] auto begin() const { return items.begin(); }
    [[nodiscard]] auto end() const { return items.end(); }
};

void jspdlog_serialize(jspdlog::json_writer &w, const bag &b)
{
    w.begin_object();
    w.member("count", b.items.size());
    w.end_object();
}

// Converts to string_view, so it is a string.
struct label
{
    // Implicit on purpose: this is the case the test covers.
    // NOLINTNEXTLINE(*-explicit-constructor,hicpp-explicit-conversions)
    operator std::string_view() const { return "label"; }
};

// Its move constructor throws, which is how a variant becomes valueless.
struct throwing
{
    throwing() = default;
    throwing(const throwing &) = default;
    // NOLINTNEXTLINE(performance-noexcept-move-constructor,bugprone-unsafe-to-allow-exceptions)
    throwing(throwing && /*unused*/) noexcept(false) { throw std::runtime_error("move"); }
    throwing &operator=(const throwing &) = default;
    throwing &operator=(throwing &&) = default;
    ~throwing() = default;
};

void jspdlog_serialize(jspdlog::json_writer &w, const throwing & /*unused*/)
{
    w.value("throwing");
}

// Hands out proxy objects, as std::vector<bool> does in libc++, but declares bool as its value type.
struct bit
{
    bool on;
    // Implicit on purpose: a proxy converts to the value type.
    // NOLINTNEXTLINE(*-explicit-constructor,hicpp-explicit-conversions)
    operator bool() const { return on; }
};
struct bits
{
    using value_type = bool;
    std::vector<bit> items;
    [[nodiscard]] auto begin() const { return items.begin(); }
    [[nodiscard]] auto end() const { return items.end(); }
};
} // namespace shop

namespace third
{
struct handle
{
    int id;
};
} // namespace third

std::string line_of(const fields &f)
{
    return f.to_json();
}

} // namespace

#if JSPDLOG_STD_FORMAT
template <>
struct std::formatter<named, char> : std::formatter<std::string_view, char>
{
    auto format(named /*unused*/, std::format_context &ctx) const
    {
        return std::formatter<std::string_view, char>::format("up", ctx);
    }
};
#else
template <>
struct fmt::formatter<named> : fmt::formatter<std::string_view>
{
    auto format(named /*unused*/, fmt::format_context &ctx) const
    {
        return fmt::formatter<std::string_view>::format("up", ctx);
    }
};
#endif

template <>
struct jspdlog::serializer<third::handle>
{
    static void write(jspdlog::json_writer &w, const third::handle &h) { w.value(h.id * 2); }
};

TEST_CASE("value types: optional writes the value or null", "[value_types]")
{
    const test_support::fixture f;
    f.logger.info({"a", std::optional<int>{3}, "b", std::optional<int>{}});
    CHECK(f.last() == R"({"logger":"t","level":"info","a":3,"b":null})");
}

TEST_CASE("value types: enums are numbers unless they have a formatter", "[value_types]")
{
    // plain is unscoped: {fmt} 9 (bundled by spdlog 1.12) would format it as "1", but it must stay a number.
    const test_support::fixture f;
    f.logger.info({"c", color::green, "p", one, "n", named::up});
    CHECK(f.last() == R"({"logger":"t","level":"info","c":1,"p":1,"n":"up"})");
}

TEST_CASE("value types: std::byte is a number on every fmt", "[value_types]")
{
    const test_support::fixture f;
    f.logger.info({"b", std::byte{42}, "buf", std::vector<std::byte>{std::byte{0}, std::byte{255}}});
    CHECK(f.last() == R"({"logger":"t","level":"info","b":42,"buf":[0,255]})");
}

#if FMT_VERSION >= 100000
TEST_CASE("value types: enums with format_as", "[value_types]")
{
    CHECK(line_of({"n", numbered::first, "l", labelled::first}) == R"({"n":10,"l":"first-label"})");
}
#endif

TEST_CASE("value types: durations are milliseconds with a type-stable JSON type", "[value_types]")
{
    using namespace std::chrono;
    const test_support::fixture f;
    f.logger.info({"s", seconds(2), "us", microseconds(1500), "ns", nanoseconds(1000000), "neg", milliseconds(-5)});
    CHECK(f.last() == R"({"logger":"t","level":"info","s":2000,"us":1.5,"ns":1.0,"neg":-5})");
}

TEST_CASE("value types: more durations", "[value_types]")
{
    using namespace std::chrono;
    CHECK(line_of({"m", minutes(2), "h", hours(1), "ms", milliseconds(0)}) == R"({"m":120000,"h":3600000,"ms":0})");
    CHECK(line_of({"d", duration<double>(1.5), "f", duration<float, std::milli>(2.0F)}) == R"({"d":1500.0,"f":2.0})");
    // A huge duration stops at the limit instead of wrapping.
    CHECK(
        line_of({"big", duration<std::int64_t, std::ratio<3600>>(std::numeric_limits<std::int64_t>::max())}) ==
        R"({"big":9223372036854775807})"
    );
    CHECK(
        line_of({"small", duration<std::int64_t, std::ratio<3600>>(std::numeric_limits<std::int64_t>::min())}) ==
        R"({"small":-9223372036854775808})"
    );
    // The JSON type does not depend on the value.
    CHECK(line_of({"a", microseconds(1000)}) == R"({"a":1.0})");
    CHECK(line_of({"a", microseconds(1)}) == R"({"a":0.001})");
}

TEST_CASE("value types: time points are RFC 3339 UTC with milliseconds", "[value_types]")
{
    using namespace std::chrono;
    const system_clock::time_point t = system_clock::time_point{} + seconds(1795420800) + milliseconds(123);
    const system_clock::time_point before_epoch = system_clock::time_point{} - milliseconds(1);
    const test_support::fixture f;
    f.logger.info({"t", t, "b", before_epoch});
    CHECK(f.last() == R"({"logger":"t","level":"info","t":"2026-11-23T08:00:00.123Z","b":"1969-12-31T23:59:59.999Z"})");
}

TEST_CASE("value types: time points of other durations and years outside 0 to 9999", "[value_types]")
{
    using namespace std::chrono;
    using seconds_point = time_point<system_clock, seconds>;
    using millis_point = time_point<system_clock, milliseconds>;
    CHECK(line_of({"s", seconds_point{seconds(1795420800)}}) == R"({"s":"2026-11-23T08:00:00.000Z"})");
    CHECK(line_of({"first", millis_point{seconds(-62167219200LL)}}) == R"({"first":"0000-01-01T00:00:00.000Z"})");
    CHECK(
        line_of({"last", millis_point{seconds(253402300799LL) + milliseconds(999)}}) ==
        R"({"last":"9999-12-31T23:59:59.999Z"})"
    );
    CHECK(line_of({"over", millis_point{seconds(253402300800LL)}}) == R"({"over":null})");
    CHECK(line_of({"under", millis_point{seconds(-62167219201LL)}}) == R"({"under":null})");
    CHECK(line_of({"huge", seconds_point{seconds(std::numeric_limits<std::int64_t>::max())}}) == R"({"huge":null})");
    CHECK(line_of({"low", seconds_point{seconds(std::numeric_limits<std::int64_t>::min())}}) == R"({"low":null})");
    // A floating-point representation: fractions are floored, and values beyond any year, NaN included, are null.
    using double_point = time_point<system_clock, duration<double>>;
    CHECK(line_of({"d", double_point{duration<double>(-0.0005)}}) == R"({"d":"1969-12-31T23:59:59.999Z"})");
    CHECK(
        line_of({"inf", double_point{duration<double>(std::numeric_limits<double>::infinity())}}) == R"({"inf":null})"
    );
    CHECK(
        line_of({"ninf", double_point{duration<double>(-std::numeric_limits<double>::infinity())}}) ==
        R"({"ninf":null})"
    );
    CHECK(
        line_of({"nan", double_point{duration<double>(std::numeric_limits<double>::quiet_NaN())}}) == R"({"nan":null})"
    );
    // Rounded toward negative infinity, as the header timestamp is.
    CHECK(
        line_of({"f", time_point<system_clock, microseconds>{microseconds(-1)}}) ==
        R"({"f":"1969-12-31T23:59:59.999Z"})"
    );
    CHECK(
        line_of({"leap", millis_point{seconds(951782400)}}) == R"({"leap":"2000-02-29T00:00:00.000Z"})"
    ); // 2000-02-29
}

TEST_CASE("value types: containers nest", "[value_types]")
{
    const std::map<std::string, std::vector<std::optional<int>>> m{{"x", {1, std::nullopt}}};
    const std::vector<jspdlog::fields> rows{{"id", 1}, {"id", 2}};
    const std::map<int, std::string> by_id{{1, "a"}};
    const test_support::fixture f;
    f.logger.info({"m", m, "rows", rows, "by_id", by_id, "pair", std::pair{1, "x"}, "arr", std::array{1.5, 2.0}});
    CHECK(
        f.last() == R"({"logger":"t","level":"info","m":{"x":[1,null]},"rows":[{"id":1},{"id":2}],)"
                    R"("by_id":[[1,"a"]],"pair":[1,"x"],"arr":[1.5,2.0]})"
    );
}

TEST_CASE("value types: variants", "[value_types]")
{
    using v = std::variant<int, std::string>;
    CHECK(line_of({"v", v{7}}) == R"({"v":7})");
    CHECK(line_of({"v", v{std::string{"x"}}}) == R"({"v":"x"})");
    CHECK(line_of({"v", std::monostate{}}) == R"({"v":null})");
    CHECK(line_of({"v", std::variant<std::monostate, int>{}}) == R"({"v":null})");

    std::variant<int, shop::throwing> valueless{1};
    CHECK_THROWS_AS(valueless.emplace<shop::throwing>(shop::throwing{}), std::runtime_error);
    REQUIRE(valueless.valueless_by_exception());
    CHECK(line_of({"v", valueless}) == R"({"v":null})");
    // Written as the alternative, which here is itself nested.
    CHECK(line_of({"v", std::variant<int, std::vector<int>>{std::vector<int>{1, 2}}}) == R"({"v":[1,2]})");
}

TEST_CASE("value types: ranges", "[value_types]")
{
    CHECK(line_of({"v", std::vector<bool>{true, false}}) == R"({"v":[true,false]})");
    // Elements of a range whose value type is bool are converted, so proxy references work.
    CHECK(line_of({"v", shop::bits{{{true}, {false}}}}) == R"({"v":[true,false]})");
    CHECK(line_of({"v", std::vector<int>{}}) == R"({"v":[]})");
    CHECK(line_of({"v", std::list<std::string>{"a", "b"}}) == R"({"v":["a","b"]})");
    CHECK(line_of({"v", std::set<int>{3, 1, 2}}) == R"({"v":[1,2,3]})");
    CHECK(line_of({"v", std::vector<std::string_view>{"a", "b"}}) == R"({"v":["a","b"]})");
    CHECK(line_of({"v", std::vector<unsigned char>{1, 200}}) == R"({"v":[1,200]})");
    CHECK(line_of({"v", std::vector<char>{'a', 'b'}}) == R"({"v":["a","b"]})");
    CHECK(line_of({"v", std::vector<std::vector<int>>{{1}, {}, {2, 3}}}) == R"({"v":[[1],[],[2,3]]})");
    const int c_array[3] = {1, 2, 3};
    CHECK(line_of({"v", c_array}) == R"({"v":[1,2,3]})");
#ifdef __cpp_lib_span
    const std::vector<int> backing{4, 5};
    CHECK(line_of({"v", std::span<const int>{backing}}) == R"({"v":[4,5]})");
#endif
}

TEST_CASE("value types: maps and tuples", "[value_types]")
{
    CHECK(line_of({"v", std::map<std::string, int>{{"a", 1}, {"b", 2}}}) == R"({"v":{"a":1,"b":2}})");
    CHECK(line_of({"v", std::map<std::string_view, int>{{"a", 1}}}) == R"({"v":{"a":1}})");
    CHECK(line_of({"v", std::unordered_map<std::string, int>{{"a", 1}}}) == R"({"v":{"a":1}})");
    CHECK(line_of({"v", std::map<std::string, int>{}}) == R"({"v":{}})");
    CHECK(line_of({"v", std::multimap<std::string, int>{{"a", 1}, {"a", 2}}}) == R"({"v":{"a":1,"a":2}})");
    CHECK(line_of({"v", std::map<int, std::string>{{1, "a"}, {2, "b"}}}) == R"({"v":[[1,"a"],[2,"b"]]})");
    CHECK(line_of({"v", std::make_tuple(1, "x", 2.5, nullptr)}) == R"({"v":[1,"x",2.5,null]})");
    CHECK(line_of({"v", std::tuple<>{}}) == R"({"v":[]})");
    CHECK(line_of({"v", std::array<int, 0>{}}) == R"({"v":[]})");
    const std::map<std::string, std::vector<std::map<std::string, std::optional<int>>>> deep{{"k", {{{"n", 1}}, {}}}};
    CHECK(line_of({"v", deep}) == R"({"v":{"k":[{"n":1},{}]}})");
}

TEST_CASE("value types: strings and scalars take the same path as before", "[value_types]")
{
    CHECK(line_of({"v", shop::label{}}) == R"({"v":"label"})");
    CHECK(line_of({"v", "literal"}) == R"({"v":"literal"})");
    const char buffer[8] = "abc";
    CHECK(line_of({"v", buffer}) == R"({"v":"abc"})");
    const char *text = "text";
    CHECK(line_of({"v", text}) == R"({"v":"text"})");
    const char unterminated[3] = {'x', 'y', 'z'};
    CHECK(line_of({"v", unterminated}) == R"({"v":"xyz"})");
    // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros.
    const std::string escaped = R"({"v":["a\"b"]})";
    CHECK(line_of({"v", std::vector<std::string>{"a\"b"}}) == escaped);
}

TEST_CASE("value types: customization points", "[value_types]")
{
    const shop::money price{1250, "EUR"};
    CHECK(line_of({"price", price}) == R"({"price":{"cents":1250,"currency":"EUR"}})");
    CHECK(line_of({"h", third::handle{21}}) == R"({"h":42})");
    CHECK(line_of({"hs", std::vector<third::handle>{{1}, {2}}}) == R"({"hs":[2,4]})");

    const shop::order order{"o-1", {{100, "USD"}, {200, "USD"}}, std::nullopt};
    CHECK(
        line_of({"order", order}) ==
        R"({"order":{"id":"o-1","lines":[{"cents":100,"currency":"USD"},{"cents":200,"currency":"USD"}],)"
        R"("discount":null}})"
    );
    CHECK(line_of({"b", shop::bag{{1, 2, 3}}}) == R"({"b":{"count":3}})");

    const std::map<std::string, std::optional<shop::money>> by_name{{"x", shop::money{1, "A"}}, {"y", std::nullopt}};
    CHECK(line_of({"m", by_name}) == R"({"m":{"x":{"cents":1,"currency":"A"},"y":null}})");
}

TEST_CASE("value types: a serializer can nest fields and raw_json", "[value_types]")
{
    CHECK(line_of({"v", std::vector<fields>{{"a", 1}, {}}}) == R"({"v":[{"a":1},{}]})");
    CHECK(line_of({"v", std::optional<fields>{fields{"a", 1}}}) == R"({"v":{"a":1}})");
    CHECK(line_of({"v", std::vector<jspdlog::raw_json>{{"[1]"}, {""}}}) == R"({"v":[[1],null]})");
}

TEST_CASE("value types: every line is one JSON object", "[value_types]")
{
    using namespace std::chrono;
    const test_support::fixture f;
    f.logger.with({"static", std::vector<int>{1, 2}})
        .info(
            {"opt",
             std::optional<std::string>{"a\nb"},
             "d",
             milliseconds(5),
             "t",
             system_clock::now(),
             "m",
             std::map<std::string, int>{{"k\"", 1}}},
            "message"
        );
    CHECK(test_support::is_single_json_object(f.last()));
#ifdef JSPDLOG_TEST_NLOHMANN
    const auto parsed = nlohmann::json::parse(f.last());
    CHECK(parsed["static"] == nlohmann::json::array({1, 2}));
    CHECK(parsed["opt"] == "a\nb");
    CHECK(parsed["d"] == 5);
    CHECK(parsed["t"].is_string());
    CHECK(parsed["m"]["k\""] == 1);
#endif
}

TEST_CASE("value types: insert replaces a key and keeps its position", "[value_types]")
{
    fields f{"a", std::optional<int>{1}, "b", 2};
    f.insert("a", std::vector<int>{1, 2});
    CHECK(f.to_json() == R"({"a":[1,2],"b":2})");
}

TEST_CASE("value types: the writer can be used directly", "[value_types]")
{
    std::string out;
    jspdlog::json_writer w(out);
    w.begin_object();
    w.key("a");
    w.value(1);
    w.key("b");
    w.begin_array();
    w.value(1);
    w.value("x");
    w.begin_object();
    w.end_object();
    w.end_array();
    w.member("c", std::optional<int>{});
    w.key("d");
    w.raw("");
    w.end_object();
    CHECK(out == R"({"a":1,"b":[1,"x",{}],"c":null,"d":null})");
}
